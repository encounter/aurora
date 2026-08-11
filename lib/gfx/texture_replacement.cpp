#include "texture_replacement.hpp"

#include "../io.hpp"
#include "../gx/gx.hpp"
#include "../gx/texture.hpp"
#include "../internal.hpp"
#include "../thread.hpp"
#include "../webgpu/gpu.hpp"
#include "dds_io.hpp"
#include "png_io.hpp"
#include "texture_convert.hpp"

#include <aurora/texture.hpp>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/hash/hash.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aurora::texture {
namespace {
constexpr Module Log{"aurora::texture"};

constexpr uint64_t kReplacementCacheBudgetBytes = 4294967296; // 4GB
constexpr uint64_t kReplacementWildcardTextureHash = kWildcardTextureHash;
constexpr uint64_t kReplacementWildcardTlutHash = kWildcardTlutHash;

enum class EntryKind {
  Raw,
  File,
  Virtual,
};

struct VirtualReadState {
  std::mutex mutex;
  std::condition_variable cv;
  bool cancelled = false;
  uint32_t inFlight = 0;
};

struct ReplacementEntry {
  uint64_t id = 0;
  int32_t priority = 0;
  uint64_t sequence = 0;
  EntryKind kind = EntryKind::Raw;
  std::span<const uint8_t> bytes;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mipCount = 1;
  uint32_t gxFormat = 0;
  std::string label;
  std::filesystem::path path;
  std::string virtualPath;
  VirtualFileSource source;
  std::shared_ptr<VirtualReadState> virtualReadState;
  std::optional<gfx::ConvertedTexture> thumbnail;
  bool thumbnailIncludesBase = false;
};

enum class Tier {
  Thumbnail,
  Full,
};

struct SelectedCache {
  gfx::TextureHandle handle;
  uint64_t id = 0;
  uint64_t bytes = 0;
  std::list<ReplacementKey>::iterator lruIt;
  Tier tier = Tier::Full;
};

struct EntryLoadSnapshot {
  ReplacementKey key;
  uint64_t id = 0;
  EntryKind kind = EntryKind::File;
  std::string label;
  std::filesystem::path path;
  std::string virtualPath;
  VirtualFileSource source;
  std::shared_ptr<VirtualReadState> virtualReadState;
};

struct LoadJob {
  Tier tier = Tier::Full;
  EntryLoadSnapshot entry;
};

struct LoadCompletion {
  Tier tier = Tier::Full;
  EntryLoadSnapshot entry;
  std::optional<gfx::ConvertedTexture> texture;
  bool thumbnailStored = false;
};

struct SourceKeyHash {
  size_t operator()(const TextureSourceKey& key) const noexcept {
    return absl::HashOf(key.textureHash, key.tlutHash, key.width, key.height, key.format, key.hasTlut);
  }
};

struct ReplacementKeyHash {
  size_t operator()(const ReplacementKey& key) const noexcept {
    if (const auto* ptrKey = std::get_if<TexturePointerKey>(&key)) {
      return absl::HashOf(0u, ptrKey->data);
    }
    const auto& sourceKey = std::get<TextureSourceKey>(key);
    return absl::HashOf(1u, sourceKey.textureHash, sourceKey.tlutHash, sourceKey.width, sourceKey.height,
                        sourceKey.format, sourceKey.hasTlut);
  }
};

std::mutex s_registryMutex;
absl::flat_hash_map<ReplacementKey, std::vector<ReplacementEntry>, ReplacementKeyHash> s_entriesByKey;
absl::flat_hash_map<ReplacementKey, SelectedCache, ReplacementKeyHash> s_cacheByKey;
absl::flat_hash_set<uint64_t> s_failedIds;
absl::flat_hash_set<TextureSourceKey, SourceKeyHash> s_reportedMisses;
std::list<ReplacementKey> s_replacementLru;
uint64_t s_replacementCacheBytes = 0;
uint64_t s_nextRegistrationId = 1;
uint64_t s_nextSequence = 1;
uint32_t s_sourceEntryCount = 0;

std::mutex s_jobMutex;
std::condition_variable s_jobCv;
std::deque<LoadJob> s_highPriorityJobs;
std::deque<LoadJob> s_lowPriorityJobs;
std::deque<LoadCompletion> s_workerCompletions;
std::vector<LoadCompletion> s_readyPublishes;
std::unordered_map<uint64_t, uint64_t> s_pendingFullLoads;
std::unordered_set<uint64_t> s_pendingThumbnailLoads;
std::vector<thread::Thread> s_workers;
uint64_t s_requestSequence = 0;
bool s_workersPaused = false;
uint32_t s_workerCountOverride = 0;

const ReplacementEntry* find_selected_entry_locked(const ReplacementKey& key) noexcept;
ReplacementEntry* find_entry_locked(const ReplacementKey& key, uint64_t id) noexcept;

unsigned char ascii_lower(unsigned char ch) noexcept {
  if (ch >= 'A' && ch <= 'Z') {
    return static_cast<unsigned char>(ch - 'A' + 'a');
  }
  return ch;
}

bool iequals_ascii(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (ascii_lower(static_cast<unsigned char>(lhs[i])) != ascii_lower(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }
  return true;
}

int compare_ascii_ci(std::string_view lhs, std::string_view rhs) noexcept {
  const size_t count = std::min(lhs.size(), rhs.size());
  for (size_t i = 0; i < count; ++i) {
    const auto lhsCh = ascii_lower(static_cast<unsigned char>(lhs[i]));
    const auto rhsCh = ascii_lower(static_cast<unsigned char>(rhs[i]));
    if (lhsCh < rhsCh) {
      return -1;
    }
    if (lhsCh > rhsCh) {
      return 1;
    }
  }
  if (lhs.size() < rhs.size()) {
    return -1;
  }
  if (lhs.size() > rhs.size()) {
    return 1;
  }
  return 0;
}

std::vector<std::string> path_components(const std::filesystem::path& root, const std::filesystem::path& path) {
  std::vector<std::string> components;
  auto relative = path.lexically_relative(root);
  if (relative.empty()) {
    relative = path.filename();
  }
  for (const auto& component : relative) {
    components.push_back(io::fs_path_to_string(component));
  }
  return components;
}

struct ReplacementCandidate {
  std::filesystem::path path;
  std::vector<std::string> components;
};

bool compare_replacement_candidates(const ReplacementCandidate& lhs, const ReplacementCandidate& rhs) noexcept {
  const size_t count = std::min(lhs.components.size(), rhs.components.size());
  for (size_t i = 0; i < count; ++i) {
    const auto cmp = compare_ascii_ci(lhs.components[i], rhs.components[i]);
    if (cmp != 0) {
      return cmp < 0;
    }
    if (lhs.components[i] != rhs.components[i]) {
      return lhs.components[i] < rhs.components[i];
    }
  }
  return lhs.components.size() < rhs.components.size();
}

bool is_relative_to(const std::filesystem::path& path, const std::filesystem::path& root) noexcept {
  if (root.empty()) {
    return false;
  }
  auto pathIt = path.begin();
  auto rootIt = root.begin();
  for (; rootIt != root.end(); ++rootIt, ++pathIt) {
    if (pathIt == path.end() ||
        !iequals_ascii(io::fs_path_to_string(*pathIt), io::fs_path_to_string(*rootIt))) {
      return false;
    }
  }
  return true;
}

bool is_sidecar_mip(std::string_view stem) noexcept {
  constexpr std::string_view tag = "_mip";
  size_t i = stem.size();
  while (i > 0 && stem[i - 1] >= '0' && stem[i - 1] <= '9') {
    --i;
  }

  if (i == stem.size() || i < tag.size()) {
    return false;
  }

  return stem.substr(i - tag.size(), tag.size()) == tag;
}

std::optional<uint64_t> parse_hex(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  uint64_t value = 0;
  for (const char ch : text) {
    value <<= 4;
    if (ch >= '0' && ch <= '9') {
      value |= static_cast<uint64_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      value |= static_cast<uint64_t>(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      value |= static_cast<uint64_t>(ch - 'A' + 10);
    } else {
      return std::nullopt;
    }
  }
  return value;
}

std::optional<uint32_t> parse_u32(std::string_view text, int base = 10) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }

  uint32_t value = 0;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value, base);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::pair<uint32_t, uint32_t>> parse_dimensions(std::string_view text) noexcept {
  const size_t sep = text.find('x');
  if (sep == std::string_view::npos) {
    return std::nullopt;
  }

  const auto width = parse_u32(text.substr(0, sep));
  const auto height = parse_u32(text.substr(sep + 1));
  if (!width.has_value() || !height.has_value()) {
    return std::nullopt;
  }
  return std::pair{*width, *height};
}

uint32_t texture_base_level_size(const GXTexObj_& obj) noexcept {
  switch (obj.format()) {
  case GX_TF_R8_PC:
    return obj.width() * obj.height();
  case GX_TF_RG8_PC:
    return obj.width() * obj.height() * 2;
  case GX_TF_RGBA8_PC:
    return obj.width() * obj.height() * 4;
  case GX_TF_BC1_PC:
    return ((obj.width() + 3) / 4) * ((obj.height() + 3) / 4) * 8;
  default:
    return GXGetTexBufferSize(obj.width(), obj.height(), obj.format(), false, 0);
  }
}

ArrayRef<uint8_t> tlut_bytes(const GXTlutObj_& tlut) noexcept {
  return {static_cast<const uint8_t*>(tlut.data), static_cast<size_t>(tlut.numEntries) * sizeof(uint16_t)};
}

std::optional<uint64_t> compute_referenced_tlut_hash(const GXTexObj_& obj, ArrayRef<uint8_t> tlutData) noexcept {
  const uint32_t textureSize = texture_base_level_size(obj);
  const auto* textureData = static_cast<const uint8_t*>(obj.data);
  if (!gx::is_palette_format(obj.format()) || !obj.has_data() || textureSize == 0 || tlutData.empty()) {
    return std::nullopt;
  }

  uint32_t minIndex = 0xffff;
  uint32_t maxIndex = 0;
  switch (obj.format()) {
  case GX_TF_C4:
    for (uint32_t i = 0; i < textureSize; ++i) {
      const uint32_t lowNibble = textureData[i] & 0xf;
      const uint32_t highNibble = textureData[i] >> 4;
      minIndex = std::min({minIndex, lowNibble, highNibble});
      maxIndex = std::max({maxIndex, lowNibble, highNibble});
    }
    break;
  case GX_TF_C8:
    for (uint32_t i = 0; i < textureSize; ++i) {
      const uint32_t index = textureData[i];
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  case GX_TF_C14X2:
    for (uint32_t i = 0; i + sizeof(uint16_t) <= textureSize; i += sizeof(uint16_t)) {
      uint16_t value = 0;
      std::memcpy(&value, textureData + i, sizeof(value));
      const uint32_t index = bswap(value) & 0x3fff;
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  default:
    return std::nullopt;
  }

  size_t tlutSize = 2 * (static_cast<size_t>(maxIndex) + 1 - minIndex);
  const size_t tlutOffset = 2 * static_cast<size_t>(minIndex);
  if (tlutOffset + tlutSize > tlutData.size()) {
    return std::nullopt;
  }
  return XXH64(tlutData.data() + tlutOffset, tlutSize, 0);
}

std::optional<uint64_t> compute_referenced_tlut_hash(const GXTexObj_& obj) noexcept {
  if (!gx::is_palette_format(obj.format()) || obj.tlut >= gx::g_gxState.loadedTluts.size()) {
    return std::nullopt;
  }

  const auto& tlut = gx::g_gxState.loadedTluts[obj.tlut];
  if (tlut.data == nullptr) {
    return std::nullopt;
  }

  return compute_referenced_tlut_hash(obj, tlut_bytes(tlut));
}

const GXTlutObj_* get_loaded_tlut(const GXTexObj_& obj) noexcept {
  if (!gx::is_palette_format(obj.format()) || obj.tlut >= gx::g_gxState.loadedTluts.size()) {
    return nullptr;
  }

  const auto& tlut = gx::g_gxState.loadedTluts[obj.tlut];
  return tlut.data != nullptr ? &tlut : nullptr;
}

TextureSourceKey build_source_key_base(const GXTexObj_& obj) noexcept {
  TextureSourceKey key{
      .width = obj.width(),
      .height = obj.height(),
      .format = obj.format(),
      .hasTlut = gx::is_palette_format(obj.format()),
  };

  const uint32_t textureSize = texture_base_level_size(obj);
  if (obj.has_data() && textureSize != 0) {
    key.textureHash = XXH64(obj.data, textureSize, 0);
  }
  return key;
}

TextureSourceKey build_source_key(const GXTexObj_& obj) noexcept {
  auto key = build_source_key_base(obj);
  if (key.hasTlut) {
    key.tlutHash = compute_referenced_tlut_hash(obj).value_or(0);
  }
  return key;
}

TextureSourceKey build_source_key(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept {
  auto key = build_source_key_base(obj);
  if (key.hasTlut && tlut.data != nullptr) {
    key.tlutHash = compute_referenced_tlut_hash(obj, tlut_bytes(tlut)).value_or(0);
  }
  return key;
}

std::string format_replacement_filename(const TextureSourceKey& key) {
  if (key.hasTlut) {
    return fmt::format("tex1_{}x{}_{:016x}_{:016x}_{}.dds", key.width, key.height, key.textureHash, key.tlutHash,
                       key.format);
  }
  return fmt::format("tex1_{}x{}_{:016x}_{}.dds", key.width, key.height, key.textureHash, key.format);
}

std::string format_source_key_for_log(const TextureSourceKey& key) {
  const auto textureHash =
      key.textureHash == kReplacementWildcardTextureHash ? std::string{"$"} : fmt::format("{:016x}", key.textureHash);
  if (!key.hasTlut) {
    return fmt::format("{}x{} tex={} fmt={}", key.width, key.height, textureHash, key.format);
  }

  const auto tlutHash =
      key.tlutHash == kReplacementWildcardTlutHash ? std::string{"$"} : fmt::format("{:016x}", key.tlutHash);
  return fmt::format("{}x{} tex={} tlut={} fmt={}", key.width, key.height, textureHash, tlutHash, key.format);
}

std::optional<gfx::ConvertedTexture> load_texture_file(const std::filesystem::path& path) {
  if (iequals_ascii(io::fs_path_to_string(path.extension()), ".png")) {
    return gfx::png::load_png_file(path);
  }
  return gfx::dds::load_dds_file(path);
}

bool remove_mipmaps(gfx::ConvertedTexture& texture) noexcept {
  if (texture.mips <= 1) {
    return true;
  }

  const uint64_t size = gfx::calc_texture_size(texture.format, texture.width, texture.height, 1);
  if (size == 0 || size > texture.data.size()) {
    return false;
  }

  ByteBuffer data{size};
  std::memcpy(data.data(), texture.data.data(), size);
  texture.mips = 1;
  texture.data = std::move(data);
  return true;
}

constexpr bool is_unsupported_texture_format(wgpu::TextureFormat format) {
  switch (format) {
  case wgpu::TextureFormat::BC1RGBAUnorm:
  case wgpu::TextureFormat::BC1RGBAUnormSrgb:
  case wgpu::TextureFormat::BC2RGBAUnorm:
  case wgpu::TextureFormat::BC2RGBAUnormSrgb:
  case wgpu::TextureFormat::BC3RGBAUnorm:
  case wgpu::TextureFormat::BC3RGBAUnormSrgb:
  case wgpu::TextureFormat::BC4RUnorm:
  case wgpu::TextureFormat::BC4RSnorm:
  case wgpu::TextureFormat::BC5RGUnorm:
  case wgpu::TextureFormat::BC5RGSnorm:
  case wgpu::TextureFormat::BC6HRGBUfloat:
  case wgpu::TextureFormat::BC6HRGBFloat:
  case wgpu::TextureFormat::BC7RGBAUnorm:
  case wgpu::TextureFormat::BC7RGBAUnormSrgb:
    return !webgpu::g_bcTexturesSupported;
  case wgpu::TextureFormat::ASTC4x4Unorm:
  case wgpu::TextureFormat::ASTC4x4UnormSrgb:
  case wgpu::TextureFormat::ASTC5x4Unorm:
  case wgpu::TextureFormat::ASTC5x4UnormSrgb:
  case wgpu::TextureFormat::ASTC5x5Unorm:
  case wgpu::TextureFormat::ASTC5x5UnormSrgb:
  case wgpu::TextureFormat::ASTC6x5Unorm:
  case wgpu::TextureFormat::ASTC6x5UnormSrgb:
  case wgpu::TextureFormat::ASTC6x6Unorm:
  case wgpu::TextureFormat::ASTC6x6UnormSrgb:
  case wgpu::TextureFormat::ASTC8x5Unorm:
  case wgpu::TextureFormat::ASTC8x5UnormSrgb:
  case wgpu::TextureFormat::ASTC8x6Unorm:
  case wgpu::TextureFormat::ASTC8x6UnormSrgb:
  case wgpu::TextureFormat::ASTC8x8Unorm:
  case wgpu::TextureFormat::ASTC8x8UnormSrgb:
  case wgpu::TextureFormat::ASTC10x5Unorm:
  case wgpu::TextureFormat::ASTC10x5UnormSrgb:
  case wgpu::TextureFormat::ASTC10x6Unorm:
  case wgpu::TextureFormat::ASTC10x6UnormSrgb:
  case wgpu::TextureFormat::ASTC10x8Unorm:
  case wgpu::TextureFormat::ASTC10x8UnormSrgb:
  case wgpu::TextureFormat::ASTC10x10Unorm:
  case wgpu::TextureFormat::ASTC10x10UnormSrgb:
  case wgpu::TextureFormat::ASTC12x10Unorm:
  case wgpu::TextureFormat::ASTC12x10UnormSrgb:
  case wgpu::TextureFormat::ASTC12x12Unorm:
  case wgpu::TextureFormat::ASTC12x12UnormSrgb:
    return !webgpu::g_astcTexturesSupported;
  default:
    return false;
  }
}

bool validate_texture_size(wgpu::TextureFormat format, uint32_t width, uint32_t height,
                           std::string_view label) noexcept {
  if (gfx::is_block_aligned(format, width, height)) {
    return true;
  }

  const auto info = gfx::format_info(format);
  Log.warn(
      "texture_replacement: failed to load texture {} because {}x{} is not aligned to {}x{} texel blocks for "
      "format {}",
      label, width, height, info.blockWidth, info.blockHeight, static_cast<uint32_t>(format));
  return false;
}

struct FileTextureSource {
  const std::filesystem::path& path;
  std::filesystem::path mipPath;

  std::string name() const { return io::fs_path_to_string(path); }
  std::optional<gfx::ConvertedTexture> load_base() { return load_texture_file(path); }
  bool open_mip(uint32_t mipLevel) {
    mipPath = path.parent_path() /
              fmt::format("{}_mip{}{}", io::fs_path_to_string(path.stem()), mipLevel,
                          io::fs_path_to_string(path.extension()));
    std::error_code ec;
    return std::filesystem::is_regular_file(mipPath, ec);
  }
  std::string mip_name() const { return io::fs_path_to_string(mipPath); }
  std::optional<gfx::ConvertedTexture> load_mip() { return load_texture_file(mipPath); }
};

bool guarded_virtual_read(const std::shared_ptr<VirtualReadState>& state, const VirtualFileSource& source,
                          const char* path, std::vector<uint8_t>& outBytes) {
  if (!state || source.read == nullptr) {
    return false;
  }

  {
    std::unique_lock lock{state->mutex};
    state->cv.wait(lock, [&] { return state->cancelled || state->inFlight == 0; });
    if (state->cancelled) {
      return false;
    }
    state->inFlight = 1;
  }

  bool read = false;
  try {
    read = source.read(source.userData, path, outBytes);
  } catch (...) { Log.warn("texture_replacement: virtual read callback threw for {}", path); }
  bool cancelled = false;
  {
    std::lock_guard lock{state->mutex};
    cancelled = state->cancelled;
    state->inFlight = 0;
  }
  state->cv.notify_all();
  return read && !cancelled;
}

std::string derive_virtual_mip_name(std::string_view path, uint32_t mipLevel) {
  const size_t slash = path.rfind('/');
  const size_t nameStart = slash == std::string_view::npos ? 0 : slash + 1;
  size_t dot = path.rfind('.');
  if (dot == std::string_view::npos || dot < nameStart) {
    dot = path.size();
  }
  return fmt::format("{}_mip{}{}", path.substr(0, dot), mipLevel, path.substr(dot));
}

struct VirtualTextureSource {
  std::string_view path;
  VirtualFileSource source;
  std::shared_ptr<VirtualReadState> readState;
  std::vector<uint8_t> bytes;
  std::string mipPath;

  std::optional<gfx::ConvertedTexture> decode() const {
    const ArrayRef data{bytes.data(), bytes.size()};
    const size_t dot = path.rfind('.');
    if (dot != std::string_view::npos && iequals_ascii(path.substr(dot), ".png")) {
      return gfx::png::parse_png_bytes(data);
    }
    return gfx::dds::parse_dds_bytes(data);
  }
  std::string name() const { return std::string{path}; }
  std::optional<gfx::ConvertedTexture> load_base() {
    bytes.clear();
    const auto pathString = std::string{path};
    if (!guarded_virtual_read(readState, source, pathString.c_str(), bytes)) {
      return std::nullopt;
    }
    return decode();
  }
  bool open_mip(uint32_t mipLevel) {
    mipPath = derive_virtual_mip_name(path, mipLevel);
    bytes.clear();
    return guarded_virtual_read(readState, source, mipPath.c_str(), bytes);
  }
  std::string mip_name() const { return mipPath; }
  std::optional<gfx::ConvertedTexture> load_mip() { return decode(); }
};

template <typename Source>
std::optional<gfx::ConvertedTexture> load_encoded_replacement(Source&& src) noexcept {
  auto base = src.load_base();
  if (!base.has_value()) {
    Log.warn("texture_replacement: failed to load texture {}", src.name());
    return std::nullopt;
  }
  if (is_unsupported_texture_format(base->format)) {
    Log.warn("texture_replacement: failed to load texture {} due to unsupported format: {}", src.name(),
             static_cast<uint32_t>(base->format));
    return std::nullopt;
  }
  if (!validate_texture_size(base->format, base->width, base->height, src.name())) {
    return std::nullopt;
  }

  if (base->mips > 1) {
    return base;
  }

  std::vector<gfx::ConvertedTexture> more;
  for (uint32_t mipLevel = 1;; ++mipLevel) {
    if (!src.open_mip(mipLevel)) {
      break;
    }

    auto lvl = src.load_mip();
    const uint32_t ew = std::max(base->width >> mipLevel, 1u);
    const uint32_t eh = std::max(base->height >> mipLevel, 1u);
    const bool ok = lvl.has_value() && lvl->format == base->format && lvl->width == ew && lvl->height == eh;
    if (!ok) {
      if (!lvl.has_value()) {
        Log.warn("texture_replacement: could not load mip {}", src.mip_name());
      } else {
        Log.warn("texture_replacement: expected {}x{} for mip {}, got {}x{}", ew, eh, src.mip_name(), lvl->width,
                 lvl->height);
      }

      break;
    }
    // If a sidecar mip file contains mipmaps, keep only the top level mip.
    if (!remove_mipmaps(*lvl)) {
      Log.warn("texture_replacement: could not slice first mip {}", src.mip_name());
      break;
    }
    more.push_back(std::move(*lvl));
  }

  if (more.empty()) {
    return base;
  }

  const uint32_t mips = 1u + static_cast<uint32_t>(more.size());
  const uint64_t n = gfx::calc_texture_size(base->format, base->width, base->height, mips);
  if (n == 0) {
    return std::nullopt;
  }

  ByteBuffer blob{n};
  uint8_t* const dst = blob.data();
  uint64_t o = 0;
  const auto append = [&](const ByteBuffer& d) noexcept -> bool {
    if (o + d.size() > n) {
      return false;
    }
    std::memcpy(dst + o, d.data(), d.size());
    o += d.size();
    return true;
  };
  if (!append(base->data)) {
    return std::nullopt;
  }
  for (const auto& mip : more) {
    if (!append(mip.data)) {
      return std::nullopt;
    }
  }
  if (o != n) {
    return std::nullopt;
  }

  return gfx::ConvertedTexture{
      .format = base->format,
      .width = base->width,
      .height = base->height,
      .mips = mips,
      .data = std::move(blob),
  };
}

std::optional<gfx::ConvertedTexture> load_file_replacement(const EntryLoadSnapshot& entry) noexcept {
  return load_encoded_replacement(FileTextureSource{.path = entry.path});
}

std::optional<gfx::ConvertedTexture> load_virtual_replacement(const EntryLoadSnapshot& entry) noexcept {
  return load_encoded_replacement(VirtualTextureSource{
      .path = entry.virtualPath,
      .source = entry.source,
      .readState = entry.virtualReadState,
  });
}

EntryLoadSnapshot snapshot_entry(const ReplacementKey& key, const ReplacementEntry& entry) {
  return EntryLoadSnapshot{
      .key = key,
      .id = entry.id,
      .kind = entry.kind,
      .label = entry.label,
      .path = entry.path,
      .virtualPath = entry.virtualPath,
      .source = entry.source,
      .virtualReadState = entry.virtualReadState,
  };
}

bool is_dds_entry(const EntryLoadSnapshot& entry) {
  const std::string extension = entry.kind == EntryKind::File
                                    ? io::fs_path_to_string(entry.path.extension())
                                    : io::fs_path_to_string(std::filesystem::path{entry.virtualPath}.extension());
  return iequals_ascii(extension, ".dds");
}

std::optional<gfx::dds::MipTail> load_thumbnail(const EntryLoadSnapshot& entry) noexcept {
  if (!is_dds_entry(entry)) {
    return std::nullopt;
  }
  if (entry.kind == EntryKind::File) {
    return gfx::dds::load_dds_mip_tail(entry.path, gx::texture::ReplacementThumbnailDim);
  }
  if (entry.kind != EntryKind::Virtual) {
    return std::nullopt;
  }

  std::vector<uint8_t> bytes;
  if (!guarded_virtual_read(entry.virtualReadState, entry.source, entry.virtualPath.c_str(), bytes)) {
    return std::nullopt;
  }
  return gfx::dds::parse_dds_mip_tail({bytes.data(), bytes.size()}, gx::texture::ReplacementThumbnailDim);
}

void worker_main(std::stop_token token) {
  std::stop_callback notifyOnStop{token, [] { s_jobCv.notify_all(); }};
  while (true) {
    LoadJob job;
    {
      std::unique_lock lock{s_jobMutex};
      s_jobCv.wait(lock, [&] {
        return token.stop_requested() ||
               (!s_workersPaused && (!s_highPriorityJobs.empty() || !s_lowPriorityJobs.empty()));
      });
      if (token.stop_requested()) {
        return;
      }
      if (!s_highPriorityJobs.empty()) {
        job = std::move(s_highPriorityJobs.front());
        s_highPriorityJobs.pop_front();
      } else {
        job = std::move(s_lowPriorityJobs.front());
        s_lowPriorityJobs.pop_front();
      }
    }

    std::optional<gfx::ConvertedTexture> texture;
    bool thumbnailIncludesBase = false;
    bool thumbnailStored = false;
    if (job.tier == Tier::Thumbnail) {
      if (auto thumbnail = load_thumbnail(job.entry); thumbnail.has_value()) {
        texture = std::move(thumbnail->texture);
        thumbnailIncludesBase = thumbnail->includesBase;
      }
    } else if (job.entry.kind == EntryKind::File) {
      texture = load_file_replacement(job.entry);
    } else if (job.entry.kind == EntryKind::Virtual) {
      texture = load_virtual_replacement(job.entry);
    }

    if (job.tier == Tier::Thumbnail && texture.has_value() && !is_unsupported_texture_format(texture->format) &&
        validate_texture_size(texture->format, texture->width, texture->height, job.entry.label)) {
      std::lock_guard lock{s_registryMutex};
      if (auto* entry = find_entry_locked(job.entry.key, job.entry.id); entry != nullptr) {
        entry->thumbnail = std::move(texture);
        entry->thumbnailIncludesBase = thumbnailIncludesBase;
        thumbnailStored = true;
      }
    }

    {
      std::lock_guard lock{s_jobMutex};
      if (!token.stop_requested()) {
        if (job.tier == Tier::Thumbnail) {
          s_pendingThumbnailLoads.erase(job.entry.id);
        }
        s_workerCompletions.push_back({
            .tier = job.tier,
            .entry = std::move(job.entry),
            .texture = job.tier == Tier::Full ? std::move(texture) : std::nullopt,
            .thumbnailStored = thumbnailStored,
        });
      }
    }
    s_jobCv.notify_all();
  }
}

void start_worker_pool() {
  if constexpr (!gx::texture::AsyncTextureReplacements) {
    return;
  }
  std::lock_guard lock{s_jobMutex};
  if (!s_workers.empty()) {
    return;
  }
  const uint32_t hardwareThreads = std::max(std::thread::hardware_concurrency(), 1u);
  const uint32_t workerCount =
      s_workerCountOverride != 0 ? s_workerCountOverride : std::clamp(hardwareThreads / 2, 1u, 4u);
  for (uint32_t i = 0; i < workerCount; ++i) {
    s_workers.emplace_back(
        thread::Options{
            .name = fmt::format("Aurora texture worker {}", i),
            .priority = thread::Priority::Low,
        },
        worker_main);
  }
}

void stop_worker_pool() {
  if constexpr (!gx::texture::AsyncTextureReplacements) {
    return;
  }
  {
    std::lock_guard lock{s_jobMutex};
    s_highPriorityJobs.clear();
    s_lowPriorityJobs.clear();
  }
  for (auto& worker : s_workers) {
    worker.request_stop();
  }
  s_jobCv.notify_all();
  for (auto& worker : s_workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  {
    std::lock_guard lock{s_jobMutex};
    s_workers.clear();
    s_workerCompletions.clear();
    s_pendingFullLoads.clear();
    s_pendingThumbnailLoads.clear();
    s_requestSequence = 0;
    s_workersPaused = false;
  }
  s_readyPublishes.clear();
}

void queue_full_load(const EntryLoadSnapshot& entry) {
  if constexpr (!gx::texture::AsyncTextureReplacements) {
    return;
  }
  start_worker_pool();
  std::lock_guard lock{s_jobMutex};
  const uint64_t priority = ++s_requestSequence;
  const auto [it, inserted] = s_pendingFullLoads.emplace(entry.id, priority);
  if (!inserted) {
    it->second = priority;
    return;
  }
  s_highPriorityJobs.push_back({.tier = Tier::Full, .entry = entry});
  s_jobCv.notify_one();
}

void queue_thumbnail_load(const EntryLoadSnapshot& entry) {
  if constexpr (!gx::texture::AsyncTextureReplacements || gx::texture::ReplacementThumbnailDim == 0) {
    return;
  }
  if (!is_dds_entry(entry)) {
    return;
  }
  start_worker_pool();
  std::lock_guard lock{s_jobMutex};
  if (!s_pendingThumbnailLoads.insert(entry.id).second) {
    return;
  }
  s_lowPriorityJobs.push_back({.tier = Tier::Thumbnail, .entry = entry});
  s_jobCv.notify_one();
}

void finish_full_load(uint64_t id) {
  std::lock_guard lock{s_jobMutex};
  s_pendingFullLoads.erase(id);
}

uint64_t pending_full_load_count() {
  std::lock_guard lock{s_jobMutex};
  return s_pendingFullLoads.size();
}

uint64_t converted_upload_bytes(const gfx::ConvertedTexture& texture) noexcept {
  const auto info = gfx::format_info(texture.format);
  uint64_t total = 0;
  for (uint32_t mip = 0; mip < texture.mips; ++mip) {
    const uint64_t width = std::max(texture.width >> mip, 1u);
    const uint64_t height = std::max(texture.height >> mip, 1u);
    const uint64_t widthBlocks = (width + info.blockWidth - 1) / info.blockWidth;
    const uint64_t heightBlocks = (height + info.blockHeight - 1) / info.blockHeight;
    const uint64_t bytesPerRow = widthBlocks * info.blockSize;
    const uint64_t alignedBytesPerRow = AURORA_ALIGN(bytesPerRow, 256);
    if (heightBlocks != 0 && alignedBytesPerRow > UINT64_MAX / heightBlocks) {
      return UINT64_MAX;
    }
    const uint64_t mipBytes = alignedBytesPerRow * heightBlocks;
    if (mipBytes > UINT64_MAX - total) {
      return UINT64_MAX;
    }
    total += mipBytes;
  }
  return total;
}

bool publish_fits_budget(uint64_t publishedCount, uint64_t publishedBytes, uint64_t nextBytes) noexcept {
  if (publishedCount == 0) {
    return true;
  }
  return nextBytes <= gx::texture::ReplacementPublishBudgetBytes &&
         publishedBytes <= gx::texture::ReplacementPublishBudgetBytes - nextBytes;
}

std::string entry_path_for_log(const ReplacementEntry& entry) {
  return entry.kind == EntryKind::Virtual ? entry.virtualPath : io::fs_path_to_string(entry.path);
}

gfx::TextureHandle create_converted_texture_handle(const EntryLoadSnapshot& entry,
                                                   const gfx::ConvertedTexture& replacement) noexcept {
  const auto label = entry.label.empty() ? fmt::format("TextureReplacement {}", entry.id) : entry.label;
  const wgpu::Extent3D size{
      .width = replacement.width,
      .height = replacement.height,
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDescriptor{
      .label = label.c_str(),
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = replacement.format,
      .mipLevelCount = replacement.mips,
      .sampleCount = 1,
  };
  auto texture = webgpu::g_device.CreateTexture(&textureDescriptor);
  const auto viewLabel = fmt::format("{} view", label);
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = viewLabel.c_str(),
      .format = replacement.format,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = replacement.mips,
  };
  auto textureView = texture.CreateView(&textureViewDescriptor);
  auto handle = std::make_shared<gfx::TextureRef>(std::move(texture), std::move(textureView), wgpu::TextureView{}, size,
                                                  replacement.format, replacement.mips, gfx::InvalidTextureFormat);
  handle->isReplacement = true;
  aurora::gfx::write_texture(*handle, replacement.data);
  return handle;
}

gfx::TextureHandle create_raw_texture_handle(const ReplacementEntry& entry) noexcept {
  if (entry.bytes.empty() || entry.width == 0 || entry.height == 0 || entry.mipCount == 0) {
    return {};
  }

  const auto label = entry.label.empty() ? fmt::format("{}", entry.id) : entry.label;
  const auto format = gfx::to_wgpu(entry.gxFormat);
  if (is_unsupported_texture_format(format)) {
    Log.warn("texture_replacement: failed to load raw replacement {} due to unsupported format: {}", label,
             static_cast<uint32_t>(format));
    return {};
  }
  if (!validate_texture_size(format, entry.width, entry.height, label)) {
    return {};
  }

  const auto textureLabel = entry.label.empty() ? fmt::format("TextureReplacement {}", entry.id) : entry.label;
  auto handle = gfx::new_static_texture_2d(entry.width, entry.height, entry.mipCount, entry.gxFormat,
                                           {entry.bytes.data(), entry.bytes.size()}, false, textureLabel.c_str());
  if (handle) {
    handle->isReplacement = true;
  }
  return handle;
}

void erase_cache_locked(const ReplacementKey& key) noexcept {
  const auto it = s_cacheByKey.find(key);
  if (it == s_cacheByKey.end()) {
    return;
  }
  s_replacementCacheBytes -= std::min(s_replacementCacheBytes, it->second.bytes);
  s_replacementLru.erase(it->second.lruIt);
  s_cacheByKey.erase(it);
}

void touch_cached_replacement(decltype(s_cacheByKey)::iterator it) noexcept {
  if (it->second.lruIt != s_replacementLru.begin()) {
    s_replacementLru.splice(s_replacementLru.begin(), s_replacementLru, it->second.lruIt);
    it->second.lruIt = s_replacementLru.begin();
  }
}

void evict_replacement_cache_if_needed() noexcept {
  while (s_replacementCacheBytes > kReplacementCacheBudgetBytes && !s_replacementLru.empty()) {
    const auto key = s_replacementLru.back();
    const auto cache = s_cacheByKey.find(key);
    const uint64_t id = cache == s_cacheByKey.end() ? 0 : cache->second.id;
    erase_cache_locked(key);
    if (id != 0) {
      gx::texture::invalidate_replacement(id);
      gx::texture::invalidate_bindings();
    }
  }
}

void cache_replacement_locked(const ReplacementKey& key, uint64_t id, gfx::TextureHandle handle, Tier tier) noexcept {
  erase_cache_locked(key);
  if (!handle) {
    return;
  }
  const uint64_t replacementBytes =
      gfx::calc_texture_size(handle->format, handle->size.width, handle->size.height, handle->mipCount);
  s_replacementLru.push_front(key);
  s_cacheByKey.emplace(key, SelectedCache{
                                .handle = std::move(handle),
                                .id = id,
                                .bytes = replacementBytes,
                                .lruIt = s_replacementLru.begin(),
                                .tier = tier,
                            });
  s_replacementCacheBytes += replacementBytes;
  evict_replacement_cache_if_needed();
}

const ReplacementEntry* select_entry(const std::vector<ReplacementEntry>& entries) noexcept {
  const ReplacementEntry* selected = nullptr;
  for (const auto& entry : entries) {
    if (selected == nullptr || entry.priority > selected->priority ||
        (entry.priority == selected->priority && entry.sequence > selected->sequence)) {
      selected = &entry;
    }
  }
  return selected;
}

const ReplacementEntry* find_selected_entry_locked(const ReplacementKey& key) noexcept {
  const auto it = s_entriesByKey.find(key);
  if (it == s_entriesByKey.end()) {
    return nullptr;
  }
  return select_entry(it->second);
}

ReplacementEntry* find_entry_locked(const ReplacementKey& key, uint64_t id) noexcept {
  const auto it = s_entriesByKey.find(key);
  if (it == s_entriesByKey.end()) {
    return nullptr;
  }
  const auto entry = std::find_if(it->second.begin(), it->second.end(),
                                  [id](const ReplacementEntry& candidate) { return candidate.id == id; });
  return entry == it->second.end() ? nullptr : &*entry;
}

std::optional<ReplacementKey> find_source_replacement_key_locked(const TextureSourceKey& key) noexcept {
  ReplacementKey exactKey{key};
  if (s_entriesByKey.contains(exactKey)) {
    return exactKey;
  }

  if (key.hasTlut) {
    auto tlutWildcard = key;
    tlutWildcard.tlutHash = kReplacementWildcardTlutHash;
    ReplacementKey tlutWildcardKey{tlutWildcard};
    if (s_entriesByKey.contains(tlutWildcardKey)) {
      return tlutWildcardKey;
    }
  }

  auto textureWildcard = key;
  textureWildcard.textureHash = kReplacementWildcardTextureHash;
  ReplacementKey textureWildcardKey{textureWildcard};
  if (s_entriesByKey.contains(textureWildcardKey)) {
    return textureWildcardKey;
  }

  return std::nullopt;
}

gfx::TextureHandle load_entry_handle(const ReplacementKey& key, const ReplacementEntry& entry) noexcept {
  if (s_failedIds.contains(entry.id)) {
    return {};
  }

  gfx::TextureHandle handle;
  if (entry.kind == EntryKind::File || entry.kind == EntryKind::Virtual) {
    const auto snapshot = snapshot_entry(key, entry);
    const auto replacement =
        entry.kind == EntryKind::File ? load_file_replacement(snapshot) : load_virtual_replacement(snapshot);
    if (!replacement.has_value()) {
      s_failedIds.insert(entry.id);
      return {};
    }
    handle = create_converted_texture_handle(snapshot, *replacement);
  } else {
    handle = create_raw_texture_handle(entry);
    if (!handle) {
      s_failedIds.insert(entry.id);
      return {};
    }
  }
  return handle;
}

std::optional<gfx::texture_replacement::ReplacementResult>
find_replacement_for_key_locked(const ReplacementKey& key) noexcept {
  const auto* entry = find_selected_entry_locked(key);
  if (entry == nullptr) {
    return std::nullopt;
  }

  if (const auto cache = s_cacheByKey.find(key); cache != s_cacheByKey.end() && cache->second.id == entry->id) {
    touch_cached_replacement(cache);
    if (cache->second.tier == Tier::Thumbnail && !s_failedIds.contains(entry->id)) {
      queue_full_load(snapshot_entry(key, *entry));
    }
    return gfx::texture_replacement::ReplacementResult{.handle = cache->second.handle, .id = entry->id};
  }

  erase_cache_locked(key);
  if constexpr (gx::texture::AsyncTextureReplacements) {
    // Raw entry data is borrowed, so they must take the synchronous path.
    if (entry->kind != EntryKind::Raw) {
      gfx::TextureHandle thumbnailHandle;
      Tier tier = Tier::Thumbnail;
      if (entry->thumbnail.has_value() && !is_unsupported_texture_format(entry->thumbnail->format) &&
          validate_texture_size(entry->thumbnail->format, entry->thumbnail->width, entry->thumbnail->height,
                                entry->label)) {
        const auto snapshot = snapshot_entry(key, *entry);
        thumbnailHandle = create_converted_texture_handle(snapshot, *entry->thumbnail);
        tier = entry->thumbnailIncludesBase ? Tier::Full : Tier::Thumbnail;
        cache_replacement_locked(key, entry->id, thumbnailHandle, tier);
      }
      if (tier != Tier::Full && !s_failedIds.contains(entry->id)) {
        queue_full_load(snapshot_entry(key, *entry));
      }
      return gfx::texture_replacement::ReplacementResult{.handle = std::move(thumbnailHandle), .id = entry->id};
    }
  }

  auto handle = load_entry_handle(key, *entry);
  if (!handle) {
    return gfx::texture_replacement::ReplacementResult{.id = entry->id};
  }
  cache_replacement_locked(key, entry->id, handle, Tier::Full);
  return gfx::texture_replacement::ReplacementResult{.handle = std::move(handle), .id = entry->id};
}

bool dump_editable_texture_dds(const TextureSourceKey& key, const GXTexObj_& obj) noexcept {
  const ArrayRef texData{static_cast<const uint8_t*>(obj.data), UINT32_MAX};
  const uint32_t texWidth = obj.width();
  const uint32_t texHeight = obj.height();

  gfx::ConvertedTexture pixels;
  if (gx::is_palette_format(obj.format())) {
    const GXTlutObj_* tlut = get_loaded_tlut(obj);
    if (tlut == nullptr) {
      return false;
    }
    pixels = gfx::convert_texture_palette(obj.format(), texWidth, texHeight, 1, texData, tlut->format, tlut->numEntries,
                                          tlut_bytes(*tlut));
  } else {
    pixels = gfx::convert_texture(obj.format(), texWidth, texHeight, 1, texData);
  }

  const uint64_t rgbaBytes = gfx::calc_texture_size(wgpu::TextureFormat::RGBA8Unorm, texWidth, texHeight, 1);
  if (pixels.data.empty() || pixels.format != wgpu::TextureFormat::RGBA8Unorm || pixels.data.size() != rgbaBytes) {
    return false;
  }

  const auto dumpRoot = io::fs_path_from_string(g_config.cachePath) / "texture_dumps";
  const auto path = dumpRoot / format_replacement_filename(key);
  return gfx::dds::write_rgba8_dds(path, texWidth, texHeight, pixels.data);
}

bool report_missing_key(const TextureSourceKey& key, const GXTexObj_& obj) noexcept {
  if (!s_reportedMisses.insert(key).second) {
    return false;
  }

  Log.warn("texture_replacement: missing runtime key {}", format_source_key_for_log(key));

  size_t loggedCandidates = 0;
  size_t omittedCandidates = 0;
  for (const auto& [replacementKey, entries] : s_entriesByKey) {
    const auto* candidate = std::get_if<TextureSourceKey>(&replacementKey);
    if (candidate == nullptr || candidate->format != key.format || candidate->hasTlut != key.hasTlut) {
      continue;
    }

    const bool sameDimensions = candidate->width == key.width && candidate->height == key.height;
    const bool sameTextureHash = candidate->textureHash == key.textureHash;
    const bool sameWidth = candidate->width == key.width;
    if (!sameDimensions && !sameTextureHash && !sameWidth) {
      continue;
    }

    std::string_view reason = "same width/format";
    if (sameDimensions && sameTextureHash) {
      reason = "same texture/dimensions";
    } else if (sameDimensions) {
      reason = "same dimensions";
    } else if (sameTextureHash) {
      reason = "same texture hash";
    }

    const auto* selected = select_entry(entries);
    if (loggedCandidates < 8) {
      Log.warn("texture_replacement: candidate ({}) {} path={}", reason, format_source_key_for_log(*candidate),
               selected != nullptr ? entry_path_for_log(*selected) : std::string{});
      ++loggedCandidates;
    } else {
      ++omittedCandidates;
    }
  }
  if (omittedCandidates != 0) {
    Log.warn("texture_replacement: omitted {} additional candidate(s) for missing key {}", omittedCandidates,
             format_source_key_for_log(key));
  }

  if (g_config.allowTextureDumps) {
    dump_editable_texture_dds(key, obj);
  }
  return true;
}

bool is_source_key(const ReplacementKey& key) noexcept;

void cancel_virtual_entry_locked(const ReplacementEntry& entry,
                                 std::vector<std::shared_ptr<VirtualReadState>>& waitStates) {
  if (!entry.virtualReadState) {
    return;
  }
  {
    std::lock_guard lock{entry.virtualReadState->mutex};
    entry.virtualReadState->cancelled = true;
  }
  entry.virtualReadState->cv.notify_all();
  waitStates.push_back(entry.virtualReadState);
}

void wait_for_virtual_reads(const std::vector<std::shared_ptr<VirtualReadState>>& states) {
  for (const auto& state : states) {
    std::unique_lock lock{state->mutex};
    state->cv.wait(lock, [&] { return state->inFlight == 0; });
  }
}

void cancel_queued_jobs(uint64_t id) {
  std::lock_guard lock{s_jobMutex};
  const auto removeId = [id](const LoadJob& job) { return job.entry.id == id; };
  const auto oldHighSize = s_highPriorityJobs.size();
  const auto oldLowSize = s_lowPriorityJobs.size();
  s_highPriorityJobs.erase(std::remove_if(s_highPriorityJobs.begin(), s_highPriorityJobs.end(), removeId),
                           s_highPriorityJobs.end());
  s_lowPriorityJobs.erase(std::remove_if(s_lowPriorityJobs.begin(), s_lowPriorityJobs.end(), removeId),
                          s_lowPriorityJobs.end());
  if (s_highPriorityJobs.size() != oldHighSize) {
    s_pendingFullLoads.erase(id);
  }
  if (s_lowPriorityJobs.size() != oldLowSize) {
    s_pendingThumbnailLoads.erase(id);
  }
}

bool unregister_replacement_locked(const ReplacementRegistration& registration,
                                   std::vector<std::shared_ptr<VirtualReadState>>& waitStates) {
  const auto it = s_entriesByKey.find(registration.key);
  if (it == s_entriesByKey.end()) {
    return false;
  }

  auto& entries = it->second;
  const auto entry = std::find_if(entries.begin(), entries.end(),
                                  [&](const ReplacementEntry& candidate) { return candidate.id == registration.id; });
  if (entry == entries.end()) {
    return false;
  }
  cancel_virtual_entry_locked(*entry, waitStates);
  cancel_queued_jobs(entry->id);
  if (is_source_key(registration.key)) {
    --s_sourceEntryCount;
  }
  s_failedIds.erase(registration.id);
  erase_cache_locked(registration.key);
  entries.erase(entry);
  if (entries.empty()) {
    s_entriesByKey.erase(it);
  }
  return true;
}

void clear_replacement_runtime_state_locked(std::vector<std::shared_ptr<VirtualReadState>>& waitStates) noexcept {
  for (const auto& [_, entries] : s_entriesByKey) {
    for (const auto& entry : entries) {
      cancel_virtual_entry_locked(entry, waitStates);
      cancel_queued_jobs(entry.id);
    }
  }
  s_entriesByKey.clear();
  s_cacheByKey.clear();
  s_failedIds.clear();
  s_reportedMisses.clear();
  s_replacementLru.clear();
  s_replacementCacheBytes = 0;
  s_sourceEntryCount = 0;
}

bool is_source_key(const ReplacementKey& key) noexcept { return std::holds_alternative<TextureSourceKey>(key); }

ReplacementRegistration register_file_replacement(TextureSourceKey key, std::filesystem::path path,
                                                  ReplacementOptions options) {
  std::lock_guard lk(s_registryMutex);
  ReplacementKey replacementKey{key};
  ReplacementRegistration registration{
      .id = s_nextRegistrationId++,
      .key = replacementKey,
  };

  auto& entries = s_entriesByKey[replacementKey];
  entries.push_back({
      .id = registration.id,
      .priority = options.priority,
      .sequence = s_nextSequence++,
      .kind = EntryKind::File,
      .label = fmt::format("TextureReplacement {}", io::fs_path_to_string(path.filename())),
      .path = std::move(path),
  });
  ++s_sourceEntryCount;
  const auto snapshot = snapshot_entry(replacementKey, entries.back());
  queue_thumbnail_load(snapshot);
  erase_cache_locked(replacementKey);
  gx::clear_static_texture_cache();
  return registration;
}
} // namespace

std::optional<TextureSourceKey> parse_replacement_filename(std::string_view filename) noexcept {
  const size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos) {
    return std::nullopt;
  }

  if (!iequals_ascii(filename.substr(dot), ".dds") && !iequals_ascii(filename.substr(dot), ".png")) {
    return std::nullopt;
  }

  const std::string_view stem = filename.substr(0, dot);
  constexpr std::string_view prefix = "tex1_";
  if (!stem.starts_with(prefix)) {
    return std::nullopt;
  }

  std::array<std::string_view, 6> parts{};
  size_t partCount = 0;
  size_t offset = 0;
  bool consumedAll = false;
  while (offset <= stem.size() && partCount < parts.size()) {
    const size_t next = stem.find('_', offset);
    parts[partCount++] = stem.substr(offset, next == std::string_view::npos ? stem.size() - offset : next - offset);
    if (next == std::string_view::npos) {
      consumedAll = true;
      break;
    }
    offset = next + 1;
  }
  if (!consumedAll || partCount < 4 || partCount > 6 || parts[0] != "tex1") {
    return std::nullopt;
  }

  const auto dimensions = parse_dimensions(parts[1]);
  if (!dimensions.has_value()) {
    return std::nullopt;
  }

  size_t index = 2;
  if (parts[index] == "m") {
    ++index;
  }

  size_t remaining = partCount - index;
  if (remaining != 2 && remaining != 3) {
    return std::nullopt;
  }

  uint64_t textureHash = 0;
  if (parts[index] == "$") {
    textureHash = kReplacementWildcardTextureHash;
  } else {
    const auto parsedTex = parse_hex(parts[index]);
    if (!parsedTex.has_value()) {
      return std::nullopt;
    }
    textureHash = *parsedTex;
  }

  auto formatPart = parts[partCount - 1];
  if (formatPart == "arb") {
    formatPart = parts[partCount - 2];
    remaining -= 1;
  }
  const auto format = parse_u32(formatPart);
  if (!format.has_value()) {
    return std::nullopt;
  }

  uint64_t tlutHash = 0;
  const bool hasTlut = remaining == 3;
  if (hasTlut) {
    const std::string_view tlutPart = parts[index + 1];
    if (tlutPart == "$") {
      tlutHash = kReplacementWildcardTlutHash;
    } else {
      const auto parsedTlutHash = parse_hex(tlutPart);
      if (!parsedTlutHash.has_value()) {
        return std::nullopt;
      }
      tlutHash = *parsedTlutHash;
    }
  }

  return TextureSourceKey{
      .textureHash = textureHash,
      .tlutHash = tlutHash,
      .width = dimensions->first,
      .height = dimensions->second,
      .format = *format,
      .hasTlut = hasTlut,
  };
}

ReplacementRegistration register_replacement(ReplacementKey key, RawTextureReplacement replacement,
                                             ReplacementOptions options) {
  if (std::holds_alternative<TexturePointerKey>(key) && std::get<TexturePointerKey>(key).data == nullptr) {
    return {};
  }
  if (replacement.bytes.empty() || replacement.width == 0 || replacement.height == 0 || replacement.mipCount == 0) {
    return {};
  }

  std::lock_guard lk(s_registryMutex);
  ReplacementRegistration registration{
      .id = s_nextRegistrationId++,
      .key = key,
  };

  auto& entries = s_entriesByKey[key];
  entries.push_back({
      .id = registration.id,
      .priority = options.priority,
      .sequence = s_nextSequence++,
      .kind = EntryKind::Raw,
      .bytes = replacement.bytes,
      .width = replacement.width,
      .height = replacement.height,
      .mipCount = replacement.mipCount,
      .gxFormat = replacement.gxFormat,
      .label = std::string(replacement.label),
  });
  if (is_source_key(key)) {
    ++s_sourceEntryCount;
  }
  erase_cache_locked(key);
  gx::clear_static_texture_cache();
  return registration;
}

void unregister_replacement(const ReplacementRegistration& registration) {
  if (registration.id == 0) {
    return;
  }

  std::vector<std::shared_ptr<VirtualReadState>> waitStates;
  bool removed = false;
  {
    std::lock_guard lk(s_registryMutex);
    removed = unregister_replacement_locked(registration, waitStates);
    if (removed) {
      gx::clear_static_texture_cache();
    }
  }
  wait_for_virtual_reads(waitStates);
}

void unregister_replacements(std::span<const ReplacementRegistration> registrations) {
  std::vector<std::shared_ptr<VirtualReadState>> waitStates;
  bool removed = false;
  {
    std::lock_guard lk(s_registryMutex);
    for (const auto& registration : registrations) {
      if (registration.id != 0) {
        removed |= unregister_replacement_locked(registration, waitStates);
      }
    }
    if (removed) {
      gx::clear_static_texture_cache();
    }
  }
  wait_for_virtual_reads(waitStates);
}

void unregister_replacements(const ReplacementGroup& group) { unregister_replacements(group.registrations); }

void unregister_replacements(const ReplacementKey& key) {
  std::vector<std::shared_ptr<VirtualReadState>> waitStates;
  bool removed = false;
  {
    std::lock_guard lk(s_registryMutex);
    const auto it = s_entriesByKey.find(key);
    if (it != s_entriesByKey.end()) {
      if (is_source_key(key)) {
        s_sourceEntryCount -= std::min<uint32_t>(s_sourceEntryCount, static_cast<uint32_t>(it->second.size()));
      }
      for (const auto& entry : it->second) {
        cancel_virtual_entry_locked(entry, waitStates);
        cancel_queued_jobs(entry.id);
        s_failedIds.erase(entry.id);
      }
      erase_cache_locked(key);
      s_entriesByKey.erase(it);
      gx::clear_static_texture_cache();
      removed = true;
    }
  }
  if (removed) {
    wait_for_virtual_reads(waitStates);
  }
}

void clear_replacements() {
  std::vector<std::shared_ptr<VirtualReadState>> waitStates;
  {
    std::lock_guard lk(s_registryMutex);
    clear_replacement_runtime_state_locked(waitStates);
    gx::clear_static_texture_cache();
  }
  wait_for_virtual_reads(waitStates);
}

ReplacementRegistration register_virtual_replacement(std::string_view path, VirtualFileSource source,
                                                     ReplacementOptions options) {
  if (source.read == nullptr) {
    return {};
  }

  const size_t slash = path.rfind('/');
  const auto filename = slash == std::string_view::npos ? path : path.substr(slash + 1);
  const auto parsed = parse_replacement_filename(filename);
  if (!parsed.has_value()) {
    return {};
  }

  std::lock_guard lk(s_registryMutex);
  ReplacementKey replacementKey{*parsed};
  ReplacementRegistration registration{
      .id = s_nextRegistrationId++,
      .key = replacementKey,
  };

  auto& entries = s_entriesByKey[replacementKey];
  entries.push_back({
      .id = registration.id,
      .priority = options.priority,
      .sequence = s_nextSequence++,
      .kind = EntryKind::Virtual,
      .label = fmt::format("TextureReplacement {}", filename),
      .virtualPath = std::string{path},
      .source = source,
      .virtualReadState = std::make_shared<VirtualReadState>(),
  });
  ++s_sourceEntryCount;
  const auto snapshot = snapshot_entry(replacementKey, entries.back());
  queue_thumbnail_load(snapshot);
  erase_cache_locked(replacementKey);
  gx::clear_static_texture_cache();
  return registration;
}

ReplacementGroup load_replacement_directory(const std::filesystem::path& root, ReplacementOptions options) {
  ReplacementGroup group;
  if (root.empty() || !io::create_directories(root)) {
    return group;
  }

  const auto dumpRoot = io::fs_path_from_string(g_config.cachePath) / "texture_dumps";
  if (g_config.allowTextureDumps && !io::create_directories(dumpRoot)) {
    return group;
  }

  std::vector<ReplacementCandidate> candidates;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(
           root,
           std::filesystem::directory_options::skip_permission_denied |
               std::filesystem::directory_options::follow_directory_symlink,
           ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      break;
    }

    if (!it->is_regular_file()) {
      continue;
    }

    const auto& path = it->path();
    if (is_relative_to(path, dumpRoot)) {
      continue;
    }

    const auto extension = io::fs_path_to_string(path.extension());
    if (!iequals_ascii(extension, ".dds") && !iequals_ascii(extension, ".png")) {
      continue;
    }

    if (is_sidecar_mip(io::fs_path_to_string(path.stem()))) {
      continue;
    }

    candidates.push_back({
        .path = path,
        .components = path_components(root, path),
    });
  }

  std::sort(candidates.begin(), candidates.end(), compare_replacement_candidates);

  absl::flat_hash_set<TextureSourceKey, SourceKeyHash> registeredKeys;
  for (const auto& candidate : candidates) {
    const auto parsed = parse_replacement_filename(io::fs_path_to_string(candidate.path.filename()));
    if (!parsed.has_value() || registeredKeys.contains(*parsed)) {
      continue;
    }
    registeredKeys.insert(*parsed);
    group.registrations.push_back(register_file_replacement(*parsed, candidate.path, options));
  }

  Log.info("Loaded {} texture replacement registrations from {}", group.registrations.size(),
           io::fs_path_to_string(root));
  return group;
}

void reload_replacement_directory(const std::filesystem::path& root, ReplacementGroup& group,
                                  ReplacementOptions options) {
  unregister_replacements(group);
  group = load_replacement_directory(root, options);
}

bool has_replacement(const GXTexObj* obj, const GXTlutObj* tlut) {
  const auto* obj_ = reinterpret_cast<const GXTexObj_*>(obj);
  if (tlut != nullptr) {
    const auto* tlut_ = reinterpret_cast<const GXTlutObj_*>(tlut);
    return gfx::texture_replacement::has_replacement(*obj_, *tlut_);
  }
  return gfx::texture_replacement::has_replacement(*obj_);
}
} // namespace aurora::texture

namespace aurora::gfx::texture_replacement {
using namespace aurora::texture;

void shutdown() noexcept {
  clear_replacements();
  stop_worker_pool();
}

StreamingStats process_streaming() noexcept {
  if constexpr (!gx::texture::AsyncTextureReplacements) {
    return {};
  }

  std::vector<LoadCompletion> thumbnails;
  {
    std::lock_guard lock{s_jobMutex};
    while (!s_workerCompletions.empty()) {
      auto completion = std::move(s_workerCompletions.front());
      s_workerCompletions.pop_front();
      if (completion.tier == Tier::Thumbnail) {
        thumbnails.push_back(std::move(completion));
      } else {
        s_readyPublishes.push_back(std::move(completion));
      }
    }
  }

  for (auto& completion : thumbnails) {
    if (!completion.thumbnailStored) {
      continue;
    }

    bool invalidate = false;
    {
      std::lock_guard lock{s_registryMutex};
      auto* entry = find_entry_locked(completion.entry.key, completion.entry.id);
      if (entry == nullptr) {
        continue;
      }
      const auto* selected = find_selected_entry_locked(completion.entry.key);
      const auto cache = s_cacheByKey.find(completion.entry.key);
      invalidate = selected != nullptr && selected->id == completion.entry.id &&
                   (cache == s_cacheByKey.end() || cache->second.id != completion.entry.id);
    }
    if (invalidate) {
      gx::texture::invalidate_replacement(completion.entry.id);
      gx::texture::invalidate_bindings();
    }
  }

  {
    std::lock_guard lock{s_jobMutex};
    const auto priority = [](uint64_t id) {
      const auto it = s_pendingFullLoads.find(id);
      return it == s_pendingFullLoads.end() ? 0 : it->second;
    };
    std::stable_sort(s_readyPublishes.begin(), s_readyPublishes.end(),
                     [&](const auto& lhs, const auto& rhs) { return priority(lhs.entry.id) > priority(rhs.entry.id); });
  }

  StreamingStats stats;
  std::vector<LoadCompletion> deferred;
  for (auto& completion : s_readyPublishes) {
    bool selected = false;
    {
      std::lock_guard lock{s_registryMutex};
      const auto* entry = find_selected_entry_locked(completion.entry.key);
      selected = entry != nullptr && entry->id == completion.entry.id;
      if (selected && !completion.texture.has_value()) {
        s_failedIds.insert(completion.entry.id);
      }
    }
    if (!selected || !completion.texture.has_value()) {
      finish_full_load(completion.entry.id);
      continue;
    }

    const uint64_t bytes = converted_upload_bytes(*completion.texture);
    if (!publish_fits_budget(stats.publishes, stats.publishBytes, bytes)) {
      deferred.push_back(std::move(completion));
      continue;
    }

    auto handle = create_converted_texture_handle(completion.entry, *completion.texture);
    bool published = false;
    {
      std::lock_guard lock{s_registryMutex};
      const auto* entry = find_selected_entry_locked(completion.entry.key);
      if (entry != nullptr && entry->id == completion.entry.id) {
        cache_replacement_locked(completion.entry.key, completion.entry.id, handle, Tier::Full);
        s_failedIds.erase(completion.entry.id);
        published = true;
      }
    }
    finish_full_load(completion.entry.id);
    if (!published) {
      continue;
    }

    gx::texture::invalidate_replacement(completion.entry.id);
    gx::texture::invalidate_bindings();
    ++stats.publishes;
    stats.publishBytes += bytes;
  }
  s_readyPublishes = std::move(deferred);
  stats.pendingLoads = pending_full_load_count();
  return stats;
}

std::optional<ReplacementResult> find_source_replacement_locked(const GXTexObj_& obj,
                                                                const TextureSourceKey& sourceKey) noexcept {
  const auto replacementKey = find_source_replacement_key_locked(sourceKey);
  if (!replacementKey.has_value()) {
    const bool alwaysReportMissingKey = false; // Enable for debugging
    if (g_config.allowTextureDumps || alwaysReportMissingKey) {
      report_missing_key(sourceKey, obj);
    }
    return std::nullopt;
  }

  return find_replacement_for_key_locked(*replacementKey);
}

std::optional<ReplacementResult> find_pointer_replacement(const GXTexObj_& obj) noexcept {
  ZoneScoped;
  if (obj.data == nullptr) {
    return std::nullopt;
  }

  std::lock_guard lk(s_registryMutex);
  ReplacementKey pointerKey{TexturePointerKey{.data = obj.data}};
  if (!s_entriesByKey.contains(pointerKey)) {
    return std::nullopt;
  }
  return find_replacement_for_key_locked(pointerKey);
}

std::optional<ReplacementResult> find_source_replacement(const GXTexObj_& obj,
                                                         const TextureSourceKey& sourceKey) noexcept {
  ZoneScoped;
  std::lock_guard lk(s_registryMutex);
  if (s_sourceEntryCount == 0 && !g_config.allowTextureDumps) {
    return std::nullopt;
  }
  return find_source_replacement_locked(obj, sourceKey);
}

bool should_build_source_key() noexcept {
  std::lock_guard lk(s_registryMutex);
  return s_sourceEntryCount != 0 || g_config.allowTextureDumps;
}

bool has_replacement(const GXTexObj_& obj) noexcept {
  std::lock_guard lk(s_registryMutex);
  if (s_entriesByKey.empty()) {
    return false;
  }

  if (obj.data != nullptr) {
    ReplacementKey pointerKey{TexturePointerKey{.data = obj.data}};
    if (s_entriesByKey.contains(pointerKey)) {
      return true;
    }
  }

  if (s_sourceEntryCount == 0) {
    return false;
  }

  return find_source_replacement_key_locked(build_source_key(obj)).has_value();
}

bool has_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept {
  std::lock_guard lk(s_registryMutex);
  if (s_entriesByKey.empty()) {
    return false;
  }

  if (obj.data != nullptr) {
    ReplacementKey pointerKey{TexturePointerKey{.data = obj.data}};
    if (s_entriesByKey.contains(pointerKey)) {
      return true;
    }
  }

  if (s_sourceEntryCount == 0) {
    return false;
  }

  return find_source_replacement_key_locked(build_source_key(obj, tlut)).has_value();
}

std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept {
  return format_replacement_filename(build_source_key(obj));
}

std::string build_texture_replacement_name(const TextureSourceKey& sourceKey) noexcept {
  return format_replacement_filename(sourceKey);
}

namespace testing {
void set_workers_paused(bool paused) noexcept {
  {
    std::lock_guard lock{s_jobMutex};
    s_workersPaused = paused;
  }
  s_jobCv.notify_all();
}

void set_worker_count(uint32_t count) noexcept {
  std::lock_guard lock{s_jobMutex};
  if (s_workers.empty()) {
    s_workerCountOverride = count;
  }
}

bool wait_for_completions(uint64_t id, uint32_t count, uint32_t timeoutMs) noexcept {
  std::unique_lock lock{s_jobMutex};
  return s_jobCv.wait_for(lock, std::chrono::milliseconds{timeoutMs}, [id, count] {
    return std::count_if(s_workerCompletions.begin(), s_workerCompletions.end(),
                         [id](const LoadCompletion& completion) { return completion.entry.id == id; }) >= count;
  });
}
} // namespace testing
} // namespace aurora::gfx::texture_replacement

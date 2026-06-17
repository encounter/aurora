#include "texture_replacement.hpp"

#include "../fs_helper.hpp"
#include "../gx/gx.hpp"
#include "../internal.hpp"
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
#include <cstring>
#include <filesystem>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace aurora::gx;
using aurora::webgpu::g_device;

namespace {
aurora::Module Log("aurora::texture");

constexpr uint64_t kReplacementCacheBudgetBytes = 4294967296; // 4GB
constexpr uint64_t kReplacementWildcardTextureHash = 0xFFFFFFFFFFFFFFFFull;
constexpr uint64_t kReplacementWildcardTlutHash = 0xFFFFFFFFFFFFFFFEull;

enum class EntryKind {
  Raw,
  File,
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
};

struct SelectedCache {
  aurora::gfx::TextureHandle handle;
  uint64_t id = 0;
  uint64_t bytes = 0;
  std::list<aurora::texture::ReplacementKey>::iterator lruIt;
};

struct TlutMetadata {
  uint32_t size = 0;
  uint32_t format = 0;
  uint16_t entries = 0;
  bool valid = false;
  aurora::ByteBuffer data;
};

struct SourceKeyHash {
  size_t operator()(const aurora::texture::TextureSourceKey& key) const noexcept {
    return absl::HashOf(key.textureHash, key.tlutHash, key.width, key.height, key.format, key.hasTlut);
  }
};

struct ReplacementKeyHash {
  size_t operator()(const aurora::texture::ReplacementKey& key) const noexcept {
    if (const auto* ptrKey = std::get_if<aurora::texture::TexturePointerKey>(&key)) {
      return absl::HashOf(0u, ptrKey->data);
    }
    const auto& sourceKey = std::get<aurora::texture::TextureSourceKey>(key);
    return absl::HashOf(1u, sourceKey.textureHash, sourceKey.tlutHash, sourceKey.width, sourceKey.height,
                        sourceKey.format, sourceKey.hasTlut);
  }
};

std::mutex s_registryMutex;
absl::flat_hash_map<aurora::texture::ReplacementKey, std::vector<ReplacementEntry>, ReplacementKeyHash> s_entriesByKey;
absl::flat_hash_map<aurora::texture::ReplacementKey, SelectedCache, ReplacementKeyHash> s_cacheByKey;
absl::flat_hash_set<uint64_t> s_failedIds;
absl::flat_hash_set<aurora::texture::TextureSourceKey, SourceKeyHash> s_reportedMisses;
std::list<aurora::texture::ReplacementKey> s_replacementLru;
uint64_t s_replacementCacheBytes = 0;
uint64_t s_nextRegistrationId = 1;
uint64_t s_nextSequence = 1;
uint32_t s_sourceEntryCount = 0;

absl::flat_hash_map<const GXTlutObj*, TlutMetadata> s_pendingTluts;
std::array<TlutMetadata, MaxTluts> s_loadedTluts{};

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
    components.push_back(fs_path_to_string(component));
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
    if (pathIt == path.end() || !iequals_ascii(fs_path_to_string(*pathIt), fs_path_to_string(*rootIt))) {
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

std::optional<uint64_t> compute_referenced_tlut_hash(const GXTexObj_& obj, std::span<const uint8_t> tlutData) noexcept {
  const uint32_t textureSize = texture_base_level_size(obj);
  const auto* textureData = static_cast<const uint8_t*>(obj.data);
  if (!is_palette_format(obj.format()) || !obj.has_data() || textureSize == 0 || tlutData.empty()) {
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
  if (!is_palette_format(obj.format()) || obj.tlut >= s_loadedTluts.size()) {
    return std::nullopt;
  }

  const auto& tlut = s_loadedTluts[obj.tlut];
  if (!tlut.valid) {
    return std::nullopt;
  }

  return compute_referenced_tlut_hash(obj, {tlut.data.data(), tlut.data.size()});
}

const TlutMetadata* get_loaded_tlut(const GXTexObj_& obj) noexcept {
  if (!is_palette_format(obj.format()) || obj.tlut >= s_loadedTluts.size()) {
    return nullptr;
  }

  const auto& tlut = s_loadedTluts[obj.tlut];
  return tlut.valid ? &tlut : nullptr;
}

bool ensure_directory(const std::filesystem::path& dir) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

aurora::texture::TextureSourceKey build_source_key_base(const GXTexObj_& obj) noexcept {
  aurora::texture::TextureSourceKey key{
      .width = obj.width(),
      .height = obj.height(),
      .format = obj.format(),
      .hasTlut = is_palette_format(obj.format()),
  };

  const uint32_t textureSize = texture_base_level_size(obj);
  if (obj.has_data() && textureSize != 0) {
    key.textureHash = XXH64(obj.data, textureSize, 0);
  }
  return key;
}

aurora::texture::TextureSourceKey build_source_key(const GXTexObj_& obj) noexcept {
  auto key = build_source_key_base(obj);
  if (key.hasTlut) {
    key.tlutHash = compute_referenced_tlut_hash(obj).value_or(0);
  }
  return key;
}

aurora::texture::TextureSourceKey build_source_key(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept {
  auto key = build_source_key_base(obj);
  if (key.hasTlut && tlut.data != nullptr) {
    key.tlutHash =
        compute_referenced_tlut_hash(
            obj, {static_cast<const uint8_t*>(tlut.data), static_cast<size_t>(tlut.numEntries) * sizeof(uint16_t)})
            .value_or(0);
  }
  return key;
}

std::string format_replacement_filename(const aurora::texture::TextureSourceKey& key) {
  if (key.hasTlut) {
    return fmt::format("tex1_{}x{}_{:016x}_{:016x}_{}.dds", key.width, key.height, key.textureHash, key.tlutHash,
                       key.format);
  }
  return fmt::format("tex1_{}x{}_{:016x}_{}.dds", key.width, key.height, key.textureHash, key.format);
}

std::string format_source_key_for_log(const aurora::texture::TextureSourceKey& key) {
  const auto textureHash =
      key.textureHash == kReplacementWildcardTextureHash ? std::string{"$"} : fmt::format("{:016x}", key.textureHash);
  if (!key.hasTlut) {
    return fmt::format("{}x{} tex={} fmt={}", key.width, key.height, textureHash, key.format);
  }

  const auto tlutHash =
      key.tlutHash == kReplacementWildcardTlutHash ? std::string{"$"} : fmt::format("{:016x}", key.tlutHash);
  return fmt::format("{}x{} tex={} tlut={} fmt={}", key.width, key.height, textureHash, tlutHash, key.format);
}

std::optional<aurora::texture::TextureSourceKey> parse_replacement_filename(std::string_view filename) noexcept {
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

  return aurora::texture::TextureSourceKey{
      .textureHash = textureHash,
      .tlutHash = tlutHash,
      .width = dimensions->first,
      .height = dimensions->second,
      .format = *format,
      .hasTlut = hasTlut,
  };
}

std::optional<aurora::gfx::ConvertedTexture> load_texture_file(const std::filesystem::path& path) {
  if (iequals_ascii(fs_path_to_string(path.extension()), ".png")) {
    return aurora::gfx::png::load_png_file(path);
  }
  return aurora::gfx::dds::load_dds_file(path);
}

bool remove_mipmaps(aurora::gfx::ConvertedTexture& texture) noexcept {
  if (texture.mips <= 1) {
    return true;
  }

  const uint64_t size = aurora::gfx::calc_texture_size(texture.format, texture.width, texture.height, 1);
  if (size == 0 || size > texture.data.size()) {
    return false;
  }

  aurora::ByteBuffer data{static_cast<size_t>(size)};
  std::memcpy(data.data(), texture.data.data(), static_cast<size_t>(size));
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
    return !aurora::webgpu::g_bcTexturesSupported;
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
    return !aurora::webgpu::g_astcTexturesSupported;
  default:
    return false;
  }
}

bool validate_texture_size(wgpu::TextureFormat format, uint32_t width, uint32_t height,
                           std::string_view label) noexcept {
  if (aurora::gfx::is_block_aligned(format, width, height)) {
    return true;
  }

  const auto info = aurora::gfx::format_info(format);
  Log.warn(
      "texture_replacement: failed to load texture {} because {}x{} is not aligned to {}x{} texel blocks for "
      "format {}",
      label, width, height, info.blockWidth, info.blockHeight, static_cast<uint32_t>(format));
  return false;
}

std::optional<aurora::gfx::ConvertedTexture> load_file_replacement(const ReplacementEntry& entry) noexcept {
  auto base = load_texture_file(entry.path);
  if (!base.has_value()) {
    Log.warn("texture_replacement: failed to load texture {}", fs_path_to_string(entry.path));
    return std::nullopt;
  }
  if (is_unsupported_texture_format(base->format)) {
    Log.warn("texture_replacement: failed to load texture {} due to unsupported format: {}",
             fs_path_to_string(entry.path), static_cast<uint32_t>(base->format));
    return std::nullopt;
  }
  if (!validate_texture_size(base->format, base->width, base->height, fs_path_to_string(entry.path))) {
    return std::nullopt;
  }

  if (base->mips > 1) {
    return base;
  }

  std::vector<aurora::gfx::ConvertedTexture> more;
  std::error_code ec;
  for (uint32_t mipLevel = 1;; ++mipLevel) {
    const auto mipPath = entry.path.parent_path() / fmt::format("{}_mip{}{}", fs_path_to_string(entry.path.stem()),
                                                                mipLevel, fs_path_to_string(entry.path.extension()));
    if (!std::filesystem::is_regular_file(mipPath, ec)) {
      break;
    }

    auto lvl = load_texture_file(mipPath);
    const uint32_t ew = std::max(base->width >> mipLevel, 1u);
    const uint32_t eh = std::max(base->height >> mipLevel, 1u);
    const bool ok = lvl.has_value() && lvl->format == base->format && lvl->width == ew && lvl->height == eh;
    if (!ok) {
      if (!lvl.has_value()) {
        Log.warn("texture_replacement: could not load mip {}", fs_path_to_string(mipPath));
      } else {
        Log.warn("texture_replacement: expected {}x{} for mip {}, got {}x{}", ew, eh, fs_path_to_string(mipPath),
                 lvl->width, lvl->height);
      }

      break;
    }
    // If a sidecar mip file contains mipmaps, keep only the top level mip.
    if (!remove_mipmaps(*lvl)) {
      Log.warn("texture_replacement: could not slice first mip {}", fs_path_to_string(mipPath));
      break;
    }
    more.push_back(std::move(*lvl));
  }

  if (more.empty()) {
    return base;
  }

  const uint32_t mips = 1u + static_cast<uint32_t>(more.size());
  const uint64_t n = aurora::gfx::calc_texture_size(base->format, base->width, base->height, mips);
  if (n == 0) {
    return std::nullopt;
  }

  aurora::ByteBuffer blob{n};
  uint8_t* const dst = blob.data();
  uint64_t o = 0;
  const auto append = [&](const aurora::ByteBuffer& d) noexcept -> bool {
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

  return aurora::gfx::ConvertedTexture{
      .format = base->format,
      .width = base->width,
      .height = base->height,
      .mips = mips,
      .data = std::move(blob),
  };
}

aurora::gfx::TextureHandle create_converted_texture_handle(const aurora::texture::ReplacementKey& key,
                                                           const ReplacementEntry& entry,
                                                           const aurora::gfx::ConvertedTexture& replacement) noexcept {
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
  auto texture = g_device.CreateTexture(&textureDescriptor);
  const auto viewLabel = fmt::format("{} view", label);
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = viewLabel.c_str(),
      .format = replacement.format,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = replacement.mips,
  };
  auto textureView = texture.CreateView(&textureViewDescriptor);
  auto handle = std::make_shared<aurora::gfx::TextureRef>(std::move(texture), std::move(textureView),
                                                          wgpu::TextureView{}, size, replacement.format,
                                                          replacement.mips, aurora::gfx::InvalidTextureFormat);
  handle->isReplacement = true;
  aurora::gfx::write_texture(*handle, replacement.data);
  return handle;
}

aurora::gfx::TextureHandle create_raw_texture_handle(const ReplacementEntry& entry) noexcept {
  if (entry.bytes.empty() || entry.width == 0 || entry.height == 0 || entry.mipCount == 0) {
    return {};
  }

  const auto label = entry.label.empty() ? fmt::format("{}", entry.id) : entry.label;
  const auto format = aurora::gfx::to_wgpu(entry.gxFormat);
  if (is_unsupported_texture_format(format)) {
    Log.warn("texture_replacement: failed to load raw replacement {} due to unsupported format: {}", label,
             static_cast<uint32_t>(format));
    return {};
  }
  if (!validate_texture_size(format, entry.width, entry.height, label)) {
    return {};
  }

  const auto textureLabel = entry.label.empty() ? fmt::format("TextureReplacement {}", entry.id) : entry.label;
  auto handle =
      aurora::gfx::new_static_texture_2d(entry.width, entry.height, entry.mipCount, entry.gxFormat,
                                         {entry.bytes.data(), entry.bytes.size()}, false, textureLabel.c_str());
  if (handle) {
    handle->isReplacement = true;
  }
  return handle;
}

void erase_cache_locked(const aurora::texture::ReplacementKey& key) noexcept {
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
    erase_cache_locked(key);
  }
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

const ReplacementEntry* find_selected_entry_locked(const aurora::texture::ReplacementKey& key) noexcept {
  const auto it = s_entriesByKey.find(key);
  if (it == s_entriesByKey.end()) {
    return nullptr;
  }
  return select_entry(it->second);
}

std::optional<aurora::texture::ReplacementKey>
find_source_replacement_key_locked(const aurora::texture::TextureSourceKey& key) noexcept {
  aurora::texture::ReplacementKey exactKey{key};
  if (s_entriesByKey.contains(exactKey)) {
    return exactKey;
  }

  if (key.hasTlut) {
    auto tlutWildcard = key;
    tlutWildcard.tlutHash = kReplacementWildcardTlutHash;
    aurora::texture::ReplacementKey tlutWildcardKey{tlutWildcard};
    if (s_entriesByKey.contains(tlutWildcardKey)) {
      return tlutWildcardKey;
    }
  }

  auto textureWildcard = key;
  textureWildcard.textureHash = kReplacementWildcardTextureHash;
  aurora::texture::ReplacementKey textureWildcardKey{textureWildcard};
  if (s_entriesByKey.contains(textureWildcardKey)) {
    return textureWildcardKey;
  }

  return std::nullopt;
}

aurora::gfx::TextureHandle load_entry_handle(const aurora::texture::ReplacementKey& key,
                                             const ReplacementEntry& entry) noexcept {
  if (s_failedIds.contains(entry.id)) {
    return {};
  }

  aurora::gfx::TextureHandle handle;
  if (entry.kind == EntryKind::File) {
    const auto replacement = load_file_replacement(entry);
    if (!replacement.has_value()) {
      s_failedIds.insert(entry.id);
      return {};
    }
    handle = create_converted_texture_handle(key, entry, *replacement);
  } else {
    handle = create_raw_texture_handle(entry);
    if (!handle) {
      s_failedIds.insert(entry.id);
      return {};
    }
  }
  return handle;
}

std::optional<aurora::gfx::TextureHandle>
find_replacement_for_key_locked(const aurora::texture::ReplacementKey& key) noexcept {
  const auto* entry = find_selected_entry_locked(key);
  if (entry == nullptr) {
    return std::nullopt;
  }

  if (const auto cache = s_cacheByKey.find(key); cache != s_cacheByKey.end() && cache->second.id == entry->id) {
    touch_cached_replacement(cache);
    return cache->second.handle;
  }

  erase_cache_locked(key);
  auto handle = load_entry_handle(key, *entry);
  if (!handle) {
    return std::nullopt;
  }

  const uint64_t replacementBytes =
      aurora::gfx::calc_texture_size(handle->format, handle->size.width, handle->size.height, handle->mipCount);
  s_replacementLru.push_front(key);
  s_cacheByKey.emplace(
      key,
      SelectedCache{.handle = handle, .id = entry->id, .bytes = replacementBytes, .lruIt = s_replacementLru.begin()});
  s_replacementCacheBytes += replacementBytes;
  evict_replacement_cache_if_needed();
  return handle;
}

bool dump_editable_texture_dds(const aurora::texture::TextureSourceKey& key, const GXTexObj_& obj) noexcept {
  const aurora::ArrayRef texData{static_cast<const uint8_t*>(obj.data), UINT32_MAX};
  const uint32_t texWidth = obj.width();
  const uint32_t texHeight = obj.height();

  aurora::gfx::ConvertedTexture pixels;
  if (is_palette_format(obj.format())) {
    const TlutMetadata* tlut = get_loaded_tlut(obj);
    if (tlut == nullptr) {
      return false;
    }
    pixels = aurora::gfx::convert_texture_palette(obj.format(), texWidth, texHeight, 1, texData,
                                                  static_cast<GXTlutFmt>(tlut->format), tlut->entries,
                                                  {tlut->data.data(), tlut->data.size()});
  } else {
    pixels = aurora::gfx::convert_texture(obj.format(), texWidth, texHeight, 1, texData);
  }

  const uint64_t rgbaBytes = aurora::gfx::calc_texture_size(wgpu::TextureFormat::RGBA8Unorm, texWidth, texHeight, 1);
  if (pixels.data.empty() || pixels.format != wgpu::TextureFormat::RGBA8Unorm || pixels.data.size() != rgbaBytes) {
    return false;
  }

  const auto dumpRoot =
      std::filesystem::path{reinterpret_cast<const char8_t*>(aurora::g_config.cachePath)} / "texture_dumps";
  const auto path = dumpRoot / format_replacement_filename(key);
  return aurora::gfx::dds::write_rgba8_dds(path, texWidth, texHeight, pixels.data);
}

bool report_missing_key(const aurora::texture::TextureSourceKey& key, const GXTexObj_& obj) noexcept {
  if (!s_reportedMisses.insert(key).second) {
    return false;
  }

  Log.warn("texture_replacement: missing runtime key {}", format_source_key_for_log(key));

  size_t loggedCandidates = 0;
  size_t omittedCandidates = 0;
  for (const auto& [replacementKey, entries] : s_entriesByKey) {
    const auto* candidate = std::get_if<aurora::texture::TextureSourceKey>(&replacementKey);
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
               selected != nullptr ? fs_path_to_string(selected->path) : std::string{});
      ++loggedCandidates;
    } else {
      ++omittedCandidates;
    }
  }
  if (omittedCandidates != 0) {
    Log.warn("texture_replacement: omitted {} additional candidate(s) for missing key {}", omittedCandidates,
             format_source_key_for_log(key));
  }

  if (aurora::g_config.allowTextureDumps) {
    dump_editable_texture_dds(key, obj);
  }
  return true;
}

void clear_replacement_runtime_state_locked() noexcept {
  s_entriesByKey.clear();
  s_cacheByKey.clear();
  s_failedIds.clear();
  s_reportedMisses.clear();
  s_replacementLru.clear();
  s_replacementCacheBytes = 0;
  s_sourceEntryCount = 0;
}

bool is_source_key(const aurora::texture::ReplacementKey& key) noexcept {
  return std::holds_alternative<aurora::texture::TextureSourceKey>(key);
}

aurora::texture::ReplacementRegistration register_file_replacement(aurora::texture::TextureSourceKey key,
                                                                   std::filesystem::path path,
                                                                   aurora::texture::ReplacementOptions options) {
  std::lock_guard lk(s_registryMutex);
  aurora::texture::ReplacementKey replacementKey{key};
  aurora::texture::ReplacementRegistration registration{
      .id = s_nextRegistrationId++,
      .key = replacementKey,
  };

  auto& entries = s_entriesByKey[replacementKey];
  entries.push_back({
      .id = registration.id,
      .priority = options.priority,
      .sequence = s_nextSequence++,
      .kind = EntryKind::File,
      .label = fmt::format("TextureReplacement {}", fs_path_to_string(path.filename())),
      .path = std::move(path),
  });
  ++s_sourceEntryCount;
  erase_cache_locked(replacementKey);
  clear_static_texture_cache();
  return registration;
}
} // namespace

namespace aurora::texture {
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
  clear_static_texture_cache();
  return registration;
}

void unregister_replacement(const ReplacementRegistration& registration) {
  if (registration.id == 0) {
    return;
  }

  std::lock_guard lk(s_registryMutex);
  const auto it = s_entriesByKey.find(registration.key);
  if (it == s_entriesByKey.end()) {
    return;
  }

  auto& entries = it->second;
  const auto oldSize = entries.size();
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&](const ReplacementEntry& entry) { return entry.id == registration.id; }),
                entries.end());
  if (entries.size() != oldSize && is_source_key(registration.key)) {
    --s_sourceEntryCount;
  }
  s_failedIds.erase(registration.id);
  erase_cache_locked(registration.key);
  if (entries.empty()) {
    s_entriesByKey.erase(it);
  }
  clear_static_texture_cache();
}

void unregister_replacements(std::span<const ReplacementRegistration> registrations) {
  for (const auto& registration : registrations) {
    unregister_replacement(registration);
  }
}

void unregister_replacements(const ReplacementGroup& group) { unregister_replacements(group.registrations); }

void unregister_replacements(const ReplacementKey& key) {
  std::lock_guard lk(s_registryMutex);
  const auto it = s_entriesByKey.find(key);
  if (it == s_entriesByKey.end()) {
    return;
  }

  if (is_source_key(key)) {
    s_sourceEntryCount -= std::min<uint32_t>(s_sourceEntryCount, static_cast<uint32_t>(it->second.size()));
  }
  for (const auto& entry : it->second) {
    s_failedIds.erase(entry.id);
  }
  erase_cache_locked(key);
  s_entriesByKey.erase(it);
  clear_static_texture_cache();
}

void clear_replacements() {
  std::lock_guard lk(s_registryMutex);
  clear_replacement_runtime_state_locked();
  clear_static_texture_cache();
}

ReplacementGroup load_replacement_directory(const std::filesystem::path& root, ReplacementOptions options) {
  ReplacementGroup group;
  if (root.empty() || !ensure_directory(root)) {
    return group;
  }

  const auto dumpRoot = std::filesystem::path{reinterpret_cast<const char8_t*>(g_config.cachePath)} / "texture_dumps";
  if (g_config.allowTextureDumps && !ensure_directory(dumpRoot)) {
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

    const auto extension = fs_path_to_string(path.extension());
    if (!iequals_ascii(extension, ".dds") && !iequals_ascii(extension, ".png")) {
      continue;
    }

    if (is_sidecar_mip(fs_path_to_string(path.stem()))) {
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
    const auto parsed = parse_replacement_filename(fs_path_to_string(candidate.path.filename()));
    if (!parsed.has_value() || registeredKeys.contains(*parsed)) {
      continue;
    }
    registeredKeys.insert(*parsed);
    group.registrations.push_back(register_file_replacement(*parsed, candidate.path, options));
  }

  Log.info("Loaded {} texture replacement registrations from {}", group.registrations.size(), fs_path_to_string(root));
  return group;
}

void reload_replacement_directory(const std::filesystem::path& root, ReplacementGroup& group,
                                  ReplacementOptions options) {
  unregister_replacements(group);
  group = load_replacement_directory(root, options);
}
} // namespace aurora::texture

namespace aurora::gfx::texture_replacement {
void initialize() noexcept {}

void shutdown() noexcept {
  texture::clear_replacements();
  s_pendingTluts.clear();
  for (auto& tlut : s_loadedTluts) {
    tlut = {};
  }
}

void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, uint16_t entries) noexcept {
  if (obj == nullptr || data == nullptr) {
    return;
  }

  const size_t sz = static_cast<size_t>(entries) * 2;
  ByteBuffer buffer{sz};
  std::memcpy(buffer.data(), static_cast<const uint8_t*>(data), sz);
  s_pendingTluts[obj] = {
      .size = static_cast<uint32_t>(entries) * 2,
      .format = static_cast<uint32_t>(format),
      .entries = entries,
      .valid = true,
      .data = std::move(buffer),
  };
}

void load_tlut(const GXTlutObj* obj, uint32_t idx) noexcept {
  if (idx >= s_loadedTluts.size()) {
    return;
  }

  const auto it = s_pendingTluts.find(obj);
  if (it == s_pendingTluts.end()) {
    s_loadedTluts[idx] = {};
    return;
  }

  const auto& pending = it->second;
  s_loadedTluts[idx] = {
      .size = pending.size,
      .format = pending.format,
      .entries = pending.entries,
      .valid = pending.valid,
      .data = pending.data.clone(),
  };
}

std::optional<TextureHandle> find_source_replacement_locked(const GXTexObj_& obj,
                                                            const texture::TextureSourceKey& sourceKey) noexcept {
  const auto replacementKey = find_source_replacement_key_locked(sourceKey);
  if (!replacementKey.has_value()) {
    // Enable for debugging
    // report_missing_key(sourceKey, obj);
    return std::nullopt;
  }

  return find_replacement_for_key_locked(*replacementKey);
}

std::optional<TextureHandle> find_replacement(const GXTexObj_& obj) noexcept {
  ZoneScoped;

  std::lock_guard lk(s_registryMutex);
  if (s_entriesByKey.empty()) {
    return std::nullopt;
  }

  if (obj.data != nullptr) {
    texture::ReplacementKey pointerKey{texture::TexturePointerKey{.data = obj.data}};
    if (s_entriesByKey.contains(pointerKey)) {
      return find_replacement_for_key_locked(pointerKey);
    }
  }

  if (s_sourceEntryCount == 0 && !g_config.allowTextureDumps) {
    return std::nullopt;
  }

  const auto sourceKey = build_source_key(obj);
  return find_source_replacement_locked(obj, sourceKey);
}

std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept {
  ZoneScoped;

  std::lock_guard lk(s_registryMutex);
  if (s_entriesByKey.empty()) {
    return std::nullopt;
  }

  if (obj.data != nullptr) {
    texture::ReplacementKey pointerKey{texture::TexturePointerKey{.data = obj.data}};
    if (s_entriesByKey.contains(pointerKey)) {
      return find_replacement_for_key_locked(pointerKey);
    }
  }

  if (s_sourceEntryCount == 0 && !g_config.allowTextureDumps) {
    return std::nullopt;
  }

  const auto sourceKey = build_source_key(obj, tlut);
  return find_source_replacement_locked(obj, sourceKey);
}

std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept {
  const auto key = build_source_key(obj);
  return format_replacement_filename(key);
}
} // namespace aurora::gfx::texture_replacement

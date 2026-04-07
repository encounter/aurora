#include "texture_replacement.hpp"

#include "../internal.hpp"
#include "../gx/gx.hpp"
#include "../webgpu/gpu.hpp"
#include "dds_io.hpp"
#include "texture_convert.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using namespace aurora::gx;
using aurora::webgpu::g_device;

namespace aurora::gfx::texture_replacement {
Module Log("aurora::gfx::texture_replacement");

struct RuntimeTextureKey {
  uint64_t textureHash = 0;
  uint64_t tlutHash = 0;
  u16 width = 0;
  u16 height = 0;
  bool hasMips = false;
  bool hasTlut = false;
  u32 format = 0;

  bool operator==(const RuntimeTextureKey& rhs) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const RuntimeTextureKey& key) {
    return H::combine(std::move(h), key.textureHash, key.tlutHash, key.width, key.height, key.hasMips, key.hasTlut, key.format);
  }
};

struct TlutMetadata {
  u32 size = 0;
  u32 format = 0;
  u16 entries = 0;
  bool valid = false;
  std::vector<u8> data;
};

struct CachedReplacement {
  gfx::TextureHandle handle;
  uint64_t bytes = 0;
  std::list<RuntimeTextureKey>::iterator lruIt;
};

absl::flat_hash_map<RuntimeTextureKey, std::filesystem::path> s_replacementIndex;
absl::flat_hash_map<RuntimeTextureKey, CachedReplacement> s_replacementCache;
absl::flat_hash_set<RuntimeTextureKey> s_failedKeys;
absl::flat_hash_set<RuntimeTextureKey> s_reportedMisses;
absl::flat_hash_map<const GXTlutObj*, TlutMetadata> s_pendingTluts;
std::array<TlutMetadata, MaxTluts> s_loadedTluts{};
std::list<RuntimeTextureKey> s_replacementLru;
std::filesystem::path s_replacementRoot;
std::filesystem::path s_dumpRoot;
uint64_t s_replacementCacheBytes = 0;
constexpr uint64_t kReplacementCacheBudgetBytes = 4294967296; // 4GB, reasonable for modern hardware?

constexpr u32 mip_count(const GXTexObj_& obj) noexcept {
  return obj.hasMips ? std::max<u32>(static_cast<u32>(obj.maxLod) + 1, 1) : 1;
}

u32 summed_mip_size(u32 width, u32 height, u32 bytesPerPixel, u32 mips) noexcept {
  u32 total = 0;
  for (u32 mip = 0; mip < mips; ++mip) {
    total += width * height * bytesPerPixel;
    width = std::max(width >> 1, 1u);
    height = std::max(height >> 1, 1u);
  }
  return total;
}

u32 compute_texture_upload_size(const GXTexObj_& obj) noexcept {
  if (obj.dataSize != 0) {
    return obj.dataSize;
  }

  const u32 mips = mip_count(obj);
  switch (obj.fmt) {
  case GX_TF_R8_PC:
    return summed_mip_size(obj.width, obj.height, 1, mips);
  case GX_TF_RGBA8_PC:
    return summed_mip_size(obj.width, obj.height, 4, mips);
  default:
    return GXGetTexBufferSize(obj.width, obj.height, obj.fmt, obj.hasMips, static_cast<u8>(mips - 1));
  }
}

bool iequals_ascii(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }
  return true;
}

bool is_relative_to(const std::filesystem::path& path, const std::filesystem::path& root) noexcept {
  if (root.empty()) {
    return false;
  }
  auto pathIt = path.begin();
  auto rootIt = root.begin();
  for (; rootIt != root.end(); ++rootIt, ++pathIt) {
    if (pathIt == path.end() || !iequals_ascii(pathIt->string(), rootIt->string())) {
      return false;
    }
  }
  return true;
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

std::optional<u32> parse_u32(std::string_view text, int base = 10) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }

  u32 value = 0;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value, base);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::pair<u16, u16>> parse_dimensions(std::string_view text) noexcept {
  const size_t sep = text.find('x');
  if (sep == std::string_view::npos) {
    return std::nullopt;
  }

  const auto width = parse_u32(text.substr(0, sep));
  const auto height = parse_u32(text.substr(sep + 1));
  if (!width.has_value() || !height.has_value() || *width > UINT16_MAX || *height > UINT16_MAX) {
    return std::nullopt;
  }
  return std::pair{static_cast<u16>(*width), static_cast<u16>(*height)};
}

u32 texture_base_level_size(const GXTexObj_& obj) noexcept {
  switch (obj.fmt) {
  case GX_TF_R8_PC:
    return static_cast<u32>(obj.width) * obj.height;
  case GX_TF_RGBA8_PC:
    return static_cast<u32>(obj.width) * obj.height * 4;
  default:
    return GXGetTexBufferSize(obj.width, obj.height, obj.fmt, false, 0);
  }
}

constexpr std::optional<size_t> palette_storage_byte_size(u32 format) noexcept {
  switch (format) {
  case GX_TF_C4:
    return 16 * 2;
  case GX_TF_C8:
    return 256 * 2;
  case GX_TF_C14X2:
    return 16384 * 2;
  default:
    return std::nullopt;
  }
}

std::optional<uint64_t> compute_referenced_tlut_hash(const GXTexObj_& obj) noexcept {
  if (!is_palette_format(obj.fmt) || obj.tlut >= s_loadedTluts.size()) {
    return std::nullopt;
  }

  const auto& tlut = s_loadedTluts[obj.tlut];
  const auto paletteSize = palette_storage_byte_size(obj.fmt);
  const u32 textureSize = texture_base_level_size(obj);
  const auto* textureData = static_cast<const u8*>(obj.data);
  if (!paletteSize.has_value() || !tlut.valid || tlut.data.size() < *paletteSize || textureData == nullptr || textureSize == 0) {
    return std::nullopt;
  }

  u32 minIndex = 0xffff;
  u32 maxIndex = 0;
  switch (obj.fmt) {
  case GX_TF_C4:
    for (u32 i = 0; i < textureSize; ++i) {
      const u32 lowNibble = textureData[i] & 0xf;
      const u32 highNibble = textureData[i] >> 4;
      minIndex = std::min({minIndex, lowNibble, highNibble});
      maxIndex = std::max({maxIndex, lowNibble, highNibble});
    }
    break;
  case GX_TF_C8:
    for (u32 i = 0; i < textureSize; ++i) {
      const u32 index = textureData[i];
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  case GX_TF_C14X2:
    for (u32 i = 0; i + sizeof(u16) <= textureSize; i += sizeof(u16)) {
      u16 value = 0;
      std::memcpy(&value, textureData + i, sizeof(value));
      const u32 index = bswap(value) & 0x3fff;
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  default:
    return std::nullopt;
  }

  size_t tlutSize = 2 * (static_cast<size_t>(maxIndex) + 1 - minIndex);
  const size_t tlutOffset = 2 * static_cast<size_t>(minIndex);
  if (tlutOffset + tlutSize > tlut.data.size()) {
    return std::nullopt;
  }
  return XXH64(tlut.data.data() + tlutOffset, tlutSize, 0);
}

const TlutMetadata* get_loaded_tlut(const GXTexObj_& obj) noexcept {
  if (!is_palette_format(obj.fmt) || obj.tlut >= s_loadedTluts.size()) {
    return nullptr;
  }

  const auto& tlut = s_loadedTluts[obj.tlut];
  return tlut.valid ? &tlut : nullptr;
}

std::optional<u32> tlut_to_texture_format(u32 tlutFormat) noexcept {
  switch (tlutFormat) {
  case GX_TL_IA8:
    return GX_TF_IA8;
  case GX_TL_RGB565:
    return GX_TF_RGB565;
  case GX_TL_RGB5A3:
    return GX_TF_RGB5A3;
  default:
    return std::nullopt;
  }
}

bool replacement_uses_literal_sampling(wgpu::TextureFormat format) noexcept {
  switch (format) {
  case wgpu::TextureFormat::BC1RGBAUnorm:
  case wgpu::TextureFormat::BC3RGBAUnorm:
  case wgpu::TextureFormat::BC5RGUnorm:
  case wgpu::TextureFormat::BC7RGBAUnorm:
    return true;
  default:
    return false;
  }
}

bool setup_replacement_swizzle(wgpu::TextureComponentSwizzleDescriptor& swizzle, u32 format) noexcept {
  switch (format) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_R8_PC:
    swizzle.swizzle.r = wgpu::ComponentSwizzle::R;
    swizzle.swizzle.g = wgpu::ComponentSwizzle::R;
    swizzle.swizzle.b = wgpu::ComponentSwizzle::R;
    swizzle.swizzle.a = wgpu::ComponentSwizzle::R;
    return true;
  case GX_TF_IA4:
  case GX_TF_IA8:
    swizzle.swizzle.r = wgpu::ComponentSwizzle::R;
    swizzle.swizzle.g = wgpu::ComponentSwizzle::R;
    swizzle.swizzle.b = wgpu::ComponentSwizzle::R;
    swizzle.swizzle.a = wgpu::ComponentSwizzle::G;
    return true;
  case GX_TF_RGB565:
    swizzle.swizzle.r = wgpu::ComponentSwizzle::R;
    swizzle.swizzle.g = wgpu::ComponentSwizzle::G;
    swizzle.swizzle.b = wgpu::ComponentSwizzle::B;
    swizzle.swizzle.a = wgpu::ComponentSwizzle::One;
    return true;
  default:
    return false;
  }
}

bool ensure_directory(const std::filesystem::path& dir) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

std::vector<u8> expand_intensity_to_rgba(ArrayRef<u8> converted) {
  std::vector<u8> pixels(converted.size() * 4);
  for (size_t i = 0; i < converted.size(); ++i) {
    pixels[i * 4 + 0] = converted[i];
    pixels[i * 4 + 1] = converted[i];
    pixels[i * 4 + 2] = converted[i];
    pixels[i * 4 + 3] = 0xFF;
  }
  return pixels;
}

std::vector<u8> expand_intensity_alpha_to_rgba(ArrayRef<u8> converted) {
  std::vector<u8> pixels((converted.size() / 2) * 4);
  for (size_t i = 0; i + 1 < converted.size(); i += 2) {
    const u8 intensity = converted[i];
    const u8 alpha = converted[i + 1];
    const size_t dst = (i / 2) * 4;
    pixels[dst + 0] = intensity;
    pixels[dst + 1] = intensity;
    pixels[dst + 2] = intensity;
    pixels[dst + 3] = alpha;
  }
  return pixels;
}

std::vector<u8> copy_rgba8_pixels(ArrayRef<u8> pixels) {
  return std::vector<u8>(pixels.data(), pixels.data() + pixels.size());
}

std::optional<std::vector<u8>> build_palette_rgba8_pixels(const GXTexObj_& obj, ArrayRef<u8> rawData) noexcept {
  if (obj.fmt == GX_TF_C14X2) {
    return std::nullopt;
  }

  const TlutMetadata* tlut = get_loaded_tlut(obj);
  if (tlut == nullptr || tlut->data.empty()) {
    return std::nullopt;
  }

  const auto indices = gfx::convert_texture(obj.fmt, obj.width, obj.height, 1, rawData);
  if (indices.empty()) {
    return std::nullopt;
  }

  const auto tlutFormat = tlut_to_texture_format(tlut->format);
  if (!tlutFormat.has_value()) {
    return std::nullopt;
  }

  const auto palette = gfx::convert_tlut(*tlutFormat, tlut->entries, {tlut->data.data(), tlut->data.size()});
  if (palette.empty()) {
    return std::nullopt;
  }

  const size_t pixelCount = static_cast<size_t>(obj.width) * obj.height;
  if (indices.size() < pixelCount * sizeof(u16)) {
    return std::nullopt;
  }

  std::vector<u8> pixels(pixelCount * 4, 0);
  const auto* indexData = reinterpret_cast<const u16*>(indices.data());
  for (size_t i = 0; i < pixelCount; ++i) {
    const u32 index = indexData[i];
    if (index >= tlut->entries) {
      continue;
    }
    const size_t dst = i * 4;
    if (tlut->format == GX_TL_IA8) {
      const size_t src = static_cast<size_t>(index) * 2;
      pixels[dst + 0] = palette.data()[src];
      pixels[dst + 1] = palette.data()[src];
      pixels[dst + 2] = palette.data()[src];
      pixels[dst + 3] = palette.data()[src + 1];
    } else {
      const size_t src = static_cast<size_t>(index) * 4;
      std::memcpy(pixels.data() + dst, palette.data() + src, 4);
    }
  }
  return pixels;
}

std::optional<std::vector<u8>> build_non_palette_rgba8_pixels(const GXTexObj_& obj, ArrayRef<u8> rawData) noexcept {
  switch (obj.fmt) {
  case GX_TF_R8_PC:
    if (rawData.size() < static_cast<size_t>(obj.width) * obj.height) {
      return std::nullopt;
    }
    return expand_intensity_to_rgba({rawData.data(), static_cast<size_t>(obj.width) * obj.height});
  case GX_TF_RGBA8_PC:
    if (rawData.size() < static_cast<size_t>(obj.width) * obj.height * 4) {
      return std::nullopt;
    }
    return copy_rgba8_pixels({rawData.data(), static_cast<size_t>(obj.width) * obj.height * 4});
  default:
    break;
  }

  const auto converted = gfx::convert_texture(obj.fmt, obj.width, obj.height, 1, rawData);
  if (converted.empty()) {
    return std::nullopt;
  }

  switch (obj.fmt) {
  case GX_TF_I4:
  case GX_TF_I8:
    return expand_intensity_to_rgba({converted.data(), converted.size()});
  case GX_TF_IA4:
  case GX_TF_IA8:
    return expand_intensity_alpha_to_rgba({converted.data(), converted.size()});
  case GX_TF_RGB565:
  case GX_TF_RGB5A3:
  case GX_TF_RGBA8:
  case GX_TF_CMPR:
    return copy_rgba8_pixels({converted.data(), converted.size()});
  default:
    return std::nullopt;
  }
}

std::optional<std::vector<u8>> build_editable_rgba8_pixels(const GXTexObj_& obj) noexcept {
  if (obj.data == nullptr || obj.width == 0 || obj.height == 0) {
    return std::nullopt;
  }

  const u32 dataSize = compute_texture_upload_size(obj);
  const ArrayRef<u8> rawData{static_cast<const u8*>(obj.data), dataSize};

  if (is_palette_format(obj.fmt)) {
    return build_palette_rgba8_pixels(obj, rawData);
  }

  return build_non_palette_rgba8_pixels(obj, rawData);
}

RuntimeTextureKey build_runtime_key(const GXTexObj_& obj) noexcept {
  RuntimeTextureKey key{
      .width = obj.width,
      .height = obj.height,
      .hasMips = obj.hasMips,
      .hasTlut = is_palette_format(obj.fmt),
      .format = obj.fmt,
  };

  const u32 textureSize = texture_base_level_size(obj);
  if (obj.data != nullptr && textureSize != 0) {
    key.textureHash = XXH64(obj.data, textureSize, 0);
  }
  if (key.hasTlut) {
    key.tlutHash = compute_referenced_tlut_hash(obj).value_or(0);
  }
  return key;
}

std::string format_replacement_filename(const RuntimeTextureKey& key) {
  if (key.hasTlut) {
    return fmt::format("tex1_{}x{}{}_{:016x}_{:016x}_{}.dds", key.width, key.height, key.hasMips ? "_m" : "", key.textureHash, key.tlutHash, key.format);
  }
  return fmt::format("tex1_{}x{}{}_{:016x}_{}.dds", key.width, key.height, key.hasMips ? "_m" : "", key.textureHash, key.format);
}

std::optional<RuntimeTextureKey> parse_replacement_filename(std::string_view filename) noexcept {
  const size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos || !iequals_ascii(filename.substr(dot), ".dds")) {
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
  bool hasMips = false;
  if (parts[index] == "m") {
    hasMips = true;
    ++index;
  }

  const size_t remaining = partCount - index;
  if (remaining != 2 && remaining != 3) {
    return std::nullopt;
  }

  const auto texHash = parse_hex(parts[index]);
  const auto format = parse_u32(parts[partCount - 1]);
  if (!texHash.has_value() || !format.has_value()) {
    return std::nullopt;
  }

  uint64_t tlutHash = 0;
  const bool hasTlut = remaining == 3;
  if (hasTlut) {
    const auto parsedTlutHash = parse_hex(parts[index + 1]);
    if (!parsedTlutHash.has_value()) {
      return std::nullopt;
    }
    tlutHash = *parsedTlutHash;
  }

  return RuntimeTextureKey{
      .textureHash = *texHash,
      .tlutHash = tlutHash,
      .width = dimensions->first,
      .height = dimensions->second,
      .hasMips = hasMips,
      .hasTlut = hasTlut,
      .format = *format,
  };
}

void touch_cached_replacement(decltype(s_replacementCache)::iterator it) noexcept {
  if (it->second.lruIt != s_replacementLru.begin()) {
    s_replacementLru.splice(s_replacementLru.begin(), s_replacementLru, it->second.lruIt);
    it->second.lruIt = s_replacementLru.begin();
  }
}

void evict_replacement_cache_if_needed() noexcept {
  while (s_replacementCacheBytes > kReplacementCacheBudgetBytes && !s_replacementLru.empty()) {
    const RuntimeTextureKey key = s_replacementLru.back();
    s_replacementLru.pop_back();

    const auto it = s_replacementCache.find(key);
    if (it == s_replacementCache.end()) {
      continue;
    }

    const uint64_t entryBytes = it->second.bytes;
    s_replacementCache.erase(it);
    s_replacementCacheBytes -= std::min(s_replacementCacheBytes, entryBytes);
  }
}

void build_index() noexcept {
  if (!g_config.allowTextureReplacements) {
    return;
  }

  s_replacementRoot = std::filesystem::path{g_config.configPath} / "texture_replacements";
  s_dumpRoot = std::filesystem::path{g_config.configPath} / "texture_dumps";

  if (!ensure_directory(s_replacementRoot) || !ensure_directory(s_dumpRoot)) {
    return;
  }

  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(s_replacementRoot, std::filesystem::directory_options::skip_permission_denied, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      break;
    }

    if (!it->is_regular_file()) {
      continue;
    }

    const auto& path = it->path();

    if (is_relative_to(path, s_dumpRoot)) {
      continue;
    }

    if (!iequals_ascii(path.extension().string(), ".dds")) {
      continue;
    }

    const auto parsed = parse_replacement_filename(path.filename().string());
    if (!parsed.has_value()) {
      continue;
    }

    s_replacementIndex.try_emplace(*parsed, path);
  }
}

const std::filesystem::path* find_replacement_path(const RuntimeTextureKey& key) noexcept {
  const auto indexed = s_replacementIndex.find(key);
  return indexed != s_replacementIndex.end() ? &indexed->second : nullptr;
}

const gfx::TextureHandle* find_cached_replacement(const RuntimeTextureKey& key) noexcept {
  const auto cached = s_replacementCache.find(key);
  if (cached == s_replacementCache.end()) {
    return nullptr;
  }

  touch_cached_replacement(cached);
  return &cached->second.handle;
}

gfx::TextureHandle load_replacement_texture(const RuntimeTextureKey& key, const GXTexObj_& source, const std::filesystem::path& path) noexcept {
  const auto replacement = dds::load_dds_file(path);
  if (!replacement.has_value()) {
    s_failedKeys.insert(key);
    return {};
  }

  const auto uploadBytes = dds::upload_byte_size(replacement->format, replacement->width, replacement->height, replacement->mipCount);
  if (!uploadBytes.has_value() || *uploadBytes > gfx::TextureUploadSize) {
    s_failedKeys.insert(key);
    return {};
  }

  const auto label = fmt::format("TextureReplacement {}", format_replacement_filename(key));
  const wgpu::Extent3D size{
      .width = replacement->width,
      .height = replacement->height,
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDescriptor{
      .label = label.c_str(),
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = replacement->format,
      .mipLevelCount = replacement->mipCount,
      .sampleCount = 1,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);
  const auto viewLabel = fmt::format("{} view", label);
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = viewLabel.c_str(),
      .format = replacement->format,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = replacement->mipCount,
  };
  wgpu::TextureViewDescriptor sampleTextureViewDescriptor = textureViewDescriptor;
  wgpu::TextureComponentSwizzleDescriptor swizzle;
  if (!replacement_uses_literal_sampling(replacement->format) && setup_replacement_swizzle(swizzle, source.fmt)) {
    sampleTextureViewDescriptor.nextInChain = &swizzle;
  }
  auto textureView = texture.CreateView(&sampleTextureViewDescriptor);
  auto handle = std::make_shared<gfx::TextureRef>(std::move(texture), std::move(textureView), wgpu::TextureView{}, size,
                                                  replacement->format, replacement->mipCount,
                                                  gfx::InvalidTextureFormat, false);
  gfx::write_texture(*handle, {replacement->data.data(), replacement->data.size()});
  return handle;
}

void cache_replacement(const RuntimeTextureKey& key, const gfx::TextureHandle& handle) noexcept {
  const uint64_t replacementBytes = dds::storage_byte_size(handle->format, handle->size.width, handle->size.height, handle->mipCount).value_or(0);
  s_replacementLru.push_front(key);
  s_replacementCache.emplace(key, CachedReplacement{.handle = handle, .bytes = replacementBytes, .lruIt = s_replacementLru.begin()});
  s_replacementCacheBytes += replacementBytes;
  evict_replacement_cache_if_needed();
}

void bind_replacement(GXTexObj_& obj, GXTexMapID id, const gfx::TextureHandle& handle) noexcept {
  obj.ref = handle;
  obj.dataInvalidated = false;

  GXTexObj_ out = obj;
  out.fmt = GX_TF_RGBA8;
  g_gxState.textures[id] = {out};
  g_gxState.stateDirty = true;
}

bool dump_editable_texture_dds(const RuntimeTextureKey& key, const GXTexObj_& obj) noexcept {
  const auto pixels = build_editable_rgba8_pixels(obj);
  if (!pixels.has_value()) {
    return false;
  }

  const auto path = s_dumpRoot / format_replacement_filename(key);
  return dds::write_rgba8_dds(path, obj.width, obj.height, {pixels->data(), pixels->size()});
}

bool report_missing_key(const RuntimeTextureKey& key, const GXTexObj_& obj) noexcept {
  if (!s_reportedMisses.insert(key).second) {
    return false;
  }

  if (g_config.allowTextureDumps) {
    dump_editable_texture_dds(key, obj);
  }
  return true;
}

void initialize() noexcept {
  build_index();
}

void shutdown() noexcept {
  s_replacementIndex.clear();
  s_replacementCache.clear();
  s_failedKeys.clear();
  s_reportedMisses.clear();
  s_pendingTluts.clear();
  s_loadedTluts.fill({});
  s_replacementLru.clear();
  s_replacementCacheBytes = 0;
  s_replacementRoot.clear();
  s_dumpRoot.clear();
}

void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, u16 entries) noexcept {
  if (obj == nullptr || data == nullptr) {
    return;
  }

  s_pendingTluts[obj] = {
      .size = static_cast<u32>(entries) * 2,
      .format = static_cast<u32>(format),
      .entries = entries,
      .valid = true,
      .data = std::vector<u8>(static_cast<const u8*>(data), static_cast<const u8*>(data) + static_cast<size_t>(entries) * 2),
  };
}

void load_tlut(const GXTlutObj* obj, u32 idx) noexcept {
  if (idx >= s_loadedTluts.size()) {
    return;
  }

  const auto it = s_pendingTluts.find(obj);
  if (it == s_pendingTluts.end()) {
    s_loadedTluts[idx] = {};
    return;
  }

  s_loadedTluts[idx] = it->second;
}

bool try_bind_replacement(GXTexObj_& obj, GXTexMapID id) noexcept {
  if (!g_config.allowTextureReplacements) {
    return false;
  }

  const RuntimeTextureKey key = build_runtime_key(obj);
  const auto* path = find_replacement_path(key);
  if (path == nullptr) {
    report_missing_key(key, obj);
    return false;
  }

  if (const auto* cached = find_cached_replacement(key); cached != nullptr) {
    bind_replacement(obj, id, *cached);
    return true;
  }

  if (s_failedKeys.contains(key)) {
    return false;
  }

  auto handle = load_replacement_texture(key, obj, *path);
  if (!handle) {
    return false;
  }

  cache_replacement(key, handle);
  bind_replacement(obj, id, handle);
  return true;
}
} // namespace aurora::gfx::texture_replacement

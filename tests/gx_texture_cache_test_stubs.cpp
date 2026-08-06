#include "gx/texture.hpp"

#include "gfx/tex_palette_conv.hpp"
#include "gfx/texture_convert.hpp"
#include "gfx/texture_replacement.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cstdio>
#include <fmt/format.h>
#include <memory>
#include <optional>

namespace {
uint64_t s_textureAllocations = 0;
uint64_t s_paletteConversions = 0;
aurora::gfx::TextureHandle s_replacement;
std::optional<aurora::texture::TextureSourceKey> s_sourceKey;
aurora::gfx::TextureHandle s_sourceReplacement;
uint64_t s_replacementId = 0;
uint64_t s_sourceReplacementId = 0;
} // namespace

namespace aurora {
AuroraConfig g_config{};

void log_internal(AuroraLogLevel level, const char* module, const char* message, unsigned int len) noexcept {
  fprintf(stderr, "[%d] %s: %.*s\n", static_cast<int>(level), module, len, message);
}
} // namespace aurora

auto fmt::formatter<AuroraLogLevel>::format(AuroraLogLevel level, format_context& ctx) const
    -> format_context::iterator {
  return fmt::format_to(ctx.out(), "{}", static_cast<int>(level));
}

namespace aurora::gx {
GXState g_gxState{};

namespace testing {
gfx::TextureHandle make_texture_handle(uint32_t width, uint32_t height, u32 format) {
  return std::make_shared<gfx::TextureRef>(wgpu::Texture{}, wgpu::TextureView{}, wgpu::TextureView{},
                                           wgpu::Extent3D{.width = width, .height = height, .depthOrArrayLayers = 1},
                                           wgpu::TextureFormat::RGBA8Unorm, 1, format);
}

void reset_texture_stubs() {
  s_textureAllocations = 0;
  s_paletteConversions = 0;
  s_replacement.reset();
  s_sourceKey.reset();
  s_sourceReplacement.reset();
  s_replacementId = 0;
  s_sourceReplacementId = 0;
}

uint64_t texture_allocations() { return s_textureAllocations; }
uint64_t palette_conversions() { return s_paletteConversions; }
void set_replacement(gfx::TextureHandle handle, uint64_t id) {
  s_replacement = std::move(handle);
  s_replacementId = id;
}
void set_source_replacement(aurora::texture::TextureSourceKey key, gfx::TextureHandle handle) {
  s_sourceKey = key;
  s_sourceReplacement = std::move(handle);
  s_sourceReplacementId = 2;
}
} // namespace testing
} // namespace aurora::gx

namespace aurora::gfx {
uint64_t calc_texture_size(wgpu::TextureFormat format, uint32_t width, uint32_t height, uint32_t mips) noexcept {
  uint64_t total = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    total += static_cast<uint64_t>(std::max(width >> mip, 1u)) * std::max(height >> mip, 1u) * 4;
  }
  return total;
}

TextureHandle new_static_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                    ArrayRef<uint8_t> data, bool tlut, const char* label) noexcept {
  ++s_textureAllocations;
  auto handle = gx::testing::make_texture_handle(width, height, gxFormat);
  handle->mipCount = mips;
  return handle;
}

TextureHandle new_conv_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept {
  return gx::testing::make_texture_handle(width, height, gxFormat);
}

ConvertedTexture convert_texture_palette(u32 textureFormat, uint32_t width, uint32_t height, uint32_t mips,
                                         ArrayRef<uint8_t> textureData, GXTlutFmt tlutFormat, uint16_t tlutEntries,
                                         ArrayRef<uint8_t> tlutData) {
  size_t pixels = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    pixels += static_cast<size_t>(std::max(width >> mip, 1u)) * std::max(height >> mip, 1u);
  }
  ConvertedTexture result{
      .format = wgpu::TextureFormat::RGBA8Unorm,
      .width = width,
      .height = height,
      .mips = mips,
      .data = ByteBuffer(pixels * 4),
  };
  return result;
}

GXTexFmt tlut_texture_format(GXTlutFmt format) noexcept { return GX_TF_RGBA8_PC; }

void queue_palette_conv(tex_palette_conv::ConvRequest req) { ++s_paletteConversions; }
} // namespace aurora::gfx

namespace aurora::gfx::texture_replacement {
StreamingStats process_streaming() noexcept { return {}; }

std::optional<ReplacementResult> find_pointer_replacement(const GXTexObj_& obj) noexcept {
  return s_replacementId != 0 ? std::optional{ReplacementResult{.handle = s_replacement, .id = s_replacementId}}
                              : std::nullopt;
}

bool should_build_source_key() noexcept { return s_sourceKey.has_value(); }

std::optional<ReplacementResult> find_source_replacement(const GXTexObj_& obj,
                                                         const aurora::texture::TextureSourceKey& sourceKey) noexcept {
  return s_sourceKey.has_value() && sourceKey == *s_sourceKey
             ? std::optional{ReplacementResult{.handle = s_sourceReplacement, .id = s_sourceReplacementId}}
             : std::nullopt;
}

std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept { return "test texture"; }
std::string build_texture_replacement_name(const aurora::texture::TextureSourceKey& sourceKey) noexcept {
  return "test texture";
}
} // namespace aurora::gfx::texture_replacement

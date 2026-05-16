#pragma once
#include <dolphin/gx.h>

#include <utility>

#include "common.hpp"

namespace aurora::gfx {
struct TextureUpload {
  wgpu::TexelCopyBufferLayout layout;
  wgpu::TexelCopyTextureInfo tex;
  wgpu::Extent3D size;

  TextureUpload(wgpu::TexelCopyBufferLayout layout, wgpu::TexelCopyTextureInfo tex, wgpu::Extent3D size) noexcept
  : layout(layout), tex(std::move(tex)), size(size) {}
};
extern std::vector<TextureUpload> g_textureUploads;

struct TextureFormatInfo {
  uint8_t blockWidth;
  uint8_t blockHeight;
  uint8_t blockSize;
  bool compressed;
};
TextureFormatInfo format_info(wgpu::TextureFormat format) noexcept;
uint64_t calc_texture_size(wgpu::TextureFormat format, uint32_t width, uint32_t height, uint32_t mips) noexcept;

constexpr u32 InvalidTextureFormat = -1;
struct TextureRef {
  wgpu::Texture texture;
  wgpu::TextureView sampleTextureView;
  wgpu::TextureView attachmentTextureView;
  wgpu::Extent3D size;
  wgpu::TextureFormat format;
  uint32_t mipCount;
  u32 gxFormat;
  bool hasArbitraryMips = false;
  bool isReplacement = false;

  TextureRef(wgpu::Texture texture, wgpu::TextureView sampleTextureView, wgpu::TextureView attachmentTextureView,
             wgpu::Extent3D size, wgpu::TextureFormat format, uint32_t mipCount, u32 gxFormat)
  : texture(std::move(texture))
  , sampleTextureView(std::move(sampleTextureView))
  , attachmentTextureView(std::move(attachmentTextureView))
  , size(size)
  , format(format)
  , mipCount(mipCount)
  , gxFormat(gxFormat) {}
};

TextureHandle new_static_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                    ArrayRef<uint8_t> data, bool tlut, const char* label) noexcept;
TextureHandle new_dynamic_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                     const char* label) noexcept;
TextureHandle new_render_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept;
TextureHandle new_conv_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept;
void write_texture(TextureRef& ref, ArrayRef<uint8_t> data) noexcept;
}; // namespace aurora::gfx

struct GXTexObj_ {
  u32 mode0 = 0;
  u32 mode1 = 0;
  u32 image0 = UINT32_MAX;
  u32 image3 = 0;
  const void* userData = nullptr;
  const void* data = nullptr;
  u32 mWidth = 0;
  u32 mHeight = 0;
  u32 mFormat = aurora::gfx::InvalidTextureFormat;
  GXTlut tlut = GX_TLUT0;
  u32 texObjId = 0;
  u32 texDataVersion = 0;
  u8 flags = 0;

  static constexpr u32 get_bits(u32 reg, u32 size, u32 shift) noexcept { return (reg >> shift) & ((1u << size) - 1); }

  u32 width() const noexcept { return mWidth != 0 ? mWidth : get_bits(image0, 10, 0) + 1 & 0x3FF; }
  u32 height() const noexcept { return mHeight != 0 ? mHeight : get_bits(image0, 10, 10) + 1 & 0x3FF; }
  u32 raw_format() const noexcept { return get_bits(image0, 4, 20); }
  u32 format() const noexcept { return mFormat != aurora::gfx::InvalidTextureFormat ? mFormat : raw_format(); }
  GXTexWrapMode wrap_s() const noexcept { return static_cast<GXTexWrapMode>(get_bits(mode0, 2, 0)); }
  GXTexWrapMode wrap_t() const noexcept { return static_cast<GXTexWrapMode>(get_bits(mode0, 2, 2)); }
  GXTexFilter min_filter() const noexcept {
    constexpr GXTexFilter kHwToGxFilter[8] = {
        GX_NEAR, GX_NEAR_MIP_NEAR, GX_LIN_MIP_NEAR, GX_NEAR, GX_LINEAR, GX_NEAR_MIP_LIN, GX_LIN_MIP_LIN, GX_NEAR,
    };
    return kHwToGxFilter[get_bits(mode0, 3, 5)];
  }
  GXTexFilter mag_filter() const noexcept { return get_bits(mode0, 1, 4) != 0 ? GX_LINEAR : GX_NEAR; }
  GXBool has_mips() const noexcept { return (flags & 1u) != 0 ? GX_TRUE : GX_FALSE; }
  u32 mip_count() const noexcept { return has_mips() ? std::max<u32>(static_cast<u32>(max_lod()) + 1, 1u) : 1; }
  GXBool do_edge_lod() const noexcept { return get_bits(mode0, 1, 8) == 0 ? GX_TRUE : GX_FALSE; }
  float lod_bias() const noexcept { return static_cast<float>(static_cast<int8_t>(get_bits(mode0, 8, 9))) / 32.0f; }
  GXAnisotropy max_aniso() const noexcept { return static_cast<GXAnisotropy>(get_bits(mode0, 2, 19)); }
  GXBool bias_clamp() const noexcept { return get_bits(mode0, 1, 21) != 0 ? GX_TRUE : GX_FALSE; }
  float min_lod() const noexcept { return static_cast<float>(get_bits(mode1, 8, 0)) / 16.0f; }
  float max_lod() const noexcept { return static_cast<float>(get_bits(mode1, 8, 8)) / 16.0f; }

  // Custom flag for texture caching
  bool no_cache() const noexcept { return (flags & 0x80) != 0; }
  void set_no_cache(bool value) noexcept { flags = value ? flags | 0x80 : flags & ~0x80; }
};
static_assert(sizeof(GXTexObj_) <= sizeof(GXTexObj), "GXTexObj too small!");
struct GXTlutObj_ {
  u32 tlut = 0;
  u32 loadTlut0 = 0;
  u16 numEntries = 0;
  const void* data = nullptr;
  GXTlutFmt format = GX_TL_IA8;
  u32 tlutObjId = 0;
  u32 tlutDataVersion = 0;
  u8 flags = 0;

  // Custom flag for texture caching
  bool no_cache() const noexcept { return (flags & 0x80) != 0; }
  void set_no_cache(bool value) noexcept { flags = value ? flags | 0x80 : flags & ~0x80; }
};
static_assert(sizeof(GXTlutObj_) <= sizeof(GXTlutObj), "GXTlutObj too small!");

namespace aurora::gfx {
struct TextureBind {
  TextureHandle ref;
  GXTexObj_ texObj;

  TextureBind() noexcept = default;
  TextureBind(const GXTexObj_& obj, TextureHandle handle) noexcept : ref(std::move(handle)), texObj(obj) {}
  void reset() noexcept { ref.reset(); }
  [[nodiscard]] wgpu::SamplerDescriptor get_descriptor() const noexcept;
  operator bool() const noexcept { return ref.operator bool(); }
};
} // namespace aurora::gfx

#pragma once

#include "common.hpp"
#include "texture.hpp"
#include "../webgpu/gpu.hpp"

namespace aurora::gfx {
static constexpr wgpu::TextureFormat to_wgpu(u32 gxFormat) {
  switch (gxFormat) {
  case GX_TF_C4:
  case GX_TF_C8:
  case GX_TF_C14X2:
    return wgpu::TextureFormat::R16Sint;
  default:
    return wgpu::TextureFormat::RGBA8Unorm;
  }
}

struct ConvertedTexture {
  wgpu::TextureFormat format;
  uint32_t width;
  uint32_t height;
  uint32_t mips = 1;
  ByteBuffer data;
  bool hasArbitraryMips = false;
};

ConvertedTexture convert_texture(u32 format, uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data);
ConvertedTexture convert_texture_palette(u32 textureFormat, uint32_t width, uint32_t height, uint32_t mips,
                                         ArrayRef<uint8_t> textureData, GXTlutFmt tlutFormat, uint16_t tlutEntries,
                                         ArrayRef<uint8_t> tlutData);
ConvertedTexture convert_tlut(u32 format, uint32_t width, ArrayRef<uint8_t> data);
GXTexFmt tlut_texture_format(GXTlutFmt format) noexcept;
} // namespace aurora::gfx

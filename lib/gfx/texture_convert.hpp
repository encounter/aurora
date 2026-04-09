#pragma once

#include "common.hpp"
#include "texture.hpp"
#include "../webgpu/gpu.hpp"

namespace aurora::gfx {
static constexpr wgpu::TextureFormat to_wgpu(u32 gxFormat) {
  switch (gxFormat) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_R8_PC:
  case GX_CTF_R4:
  case GX_CTF_A8:
  case GX_CTF_R8:
  case GX_CTF_B8:
  case GX_CTF_G8:
    return wgpu::TextureFormat::R8Unorm;
  case GX_TF_IA4:
  case GX_TF_IA8:
  case GX_CTF_RA4:
  case GX_CTF_RA8:
  case GX_CTF_RG8:
  case GX_CTF_GB8:
    return wgpu::TextureFormat::RG8Unorm;
  case GX_TF_C4:
  case GX_TF_C8:
  case GX_TF_C14X2:
    return wgpu::TextureFormat::R16Sint;
  default:
    return wgpu::TextureFormat::RGBA8Unorm;
  }
}

ByteBuffer convert_texture(u32 format, uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data);
ByteBuffer convert_texture_palette(u32 textureFormat, uint32_t width, uint32_t height, uint32_t mips,
                                   ArrayRef<uint8_t> textureData, GXTlutFmt tlutFormat, uint16_t tlutEntries,
                                   ArrayRef<uint8_t> tlutData);
ByteBuffer convert_tlut(u32 format, uint32_t width, ArrayRef<uint8_t> data);
GXTexFmt tlut_texture_format(GXTlutFmt format) noexcept;
} // namespace aurora::gfx

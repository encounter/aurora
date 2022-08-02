#pragma once

#include "common.hpp"
#include "texture.hpp"
#include "../webgpu/gpu.hpp"

namespace aurora::gfx {
static wgpu::TextureFormat to_wgpu(u32 format) {
  switch (format) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_R8_PC:
    return wgpu::TextureFormat::R8Unorm;
  case GX_TF_C4:
  case GX_TF_C8:
  case GX_TF_C14X2:
    return wgpu::TextureFormat::R16Sint;
  case GX_TF_CMPR:
    if (webgpu::g_device.HasFeature(wgpu::FeatureName::TextureCompressionBC)) {
      return wgpu::TextureFormat::BC1RGBAUnorm;
    }
    [[fallthrough]];
  default:
    return wgpu::TextureFormat::RGBA8Unorm;
  }
}

ByteBuffer convert_texture(u32 format, uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data);
} // namespace aurora::gfx

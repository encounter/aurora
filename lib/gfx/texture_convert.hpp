#pragma once

#include "common.hpp"
#include "texture.hpp"
#include "../webgpu/gpu.hpp"

namespace aurora::gfx {
static WGPUTextureFormat to_wgpu(u32 format) {
  switch (format) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_R8_PC:
    return WGPUTextureFormat_R8Unorm;
  case GX_TF_C4:
  case GX_TF_C8:
  case GX_TF_C14X2:
    return WGPUTextureFormat_R16Sint;
  case GX_TF_CMPR:
    if (wgpuDeviceHasFeature(webgpu::g_device, WGPUFeatureName_TextureCompressionBC)) {
      return WGPUTextureFormat_BC1RGBAUnorm;
    }
    [[fallthrough]];
  default:
    return WGPUTextureFormat_RGBA8Unorm;
  }
}

ByteBuffer convert_texture(u32 format, uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data);
} // namespace aurora::gfx

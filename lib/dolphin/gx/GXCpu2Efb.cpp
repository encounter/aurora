#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/common.hpp"
#include "../../webgpu/gpu.hpp"

#include <magic_enum.hpp>

#include <cstring>
#include <vector>

namespace {
wgpu::Buffer s_readbackBuffer;
std::vector<uint8_t> s_cpuDepthCache;
uint32_t s_cachedWidth = 0;
uint32_t s_cachedHeight = 0;
uint32_t s_cachedRowPitch = 0;
uint32_t s_cachedFrame = UINT32_MAX;
bool s_cacheValid = false;

bool s_mapDone = false;
bool s_mapOk = false;

void peekz_map_callback(wgpu::MapAsyncStatus status, wgpu::StringView message) {
  s_mapDone = true;
  s_mapOk = (status == wgpu::MapAsyncStatus::Success);
  if (!s_mapOk && status != wgpu::MapAsyncStatus::CallbackCancelled &&
      status != wgpu::MapAsyncStatus::Aborted) {
    Log.warn("GXPeekZ buffer map failed: {} {}", magic_enum::enum_name(status), message);
  }
}

bool refresh_depth_cache() {
  using namespace aurora::webgpu;
  if (!g_device || !g_depthBuffer.texture) {
    return false;
  }
  if (g_depthBuffer.format != wgpu::TextureFormat::Depth32Float) {
    // Only Depth32Float is supported; other formats would need conversion.
    return false;
  }
  const uint32_t width = g_depthBuffer.size.width;
  const uint32_t height = g_depthBuffer.size.height;
  if (width == 0 || height == 0) {
    return false;
  }
  const uint32_t rowPitch = AURORA_ALIGN(width * 4u, 256u);
  const uint64_t bufferSize = static_cast<uint64_t>(rowPitch) * height;

  if (!s_readbackBuffer || s_cachedWidth != width || s_cachedHeight != height) {
    const wgpu::BufferDescriptor desc{
        .label = "PeekZ readback buffer",
        .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
        .size = bufferSize,
    };
    s_readbackBuffer = g_device.CreateBuffer(&desc);
    s_cpuDepthCache.assign(bufferSize, 0);
    s_cachedWidth = width;
    s_cachedHeight = height;
    s_cachedRowPitch = rowPitch;
  }

  const wgpu::CommandEncoderDescriptor encDesc{.label = "PeekZ readback encoder"};
  auto encoder = g_device.CreateCommandEncoder(&encDesc);

  const wgpu::TexelCopyTextureInfo src{
      .texture = g_depthBuffer.texture,
      .mipLevel = 0,
      .origin = {0, 0, 0},
      .aspect = wgpu::TextureAspect::DepthOnly,
  };
  const wgpu::TexelCopyBufferInfo dst{
      .layout =
          {
              .offset = 0,
              .bytesPerRow = rowPitch,
              .rowsPerImage = height,
          },
      .buffer = s_readbackBuffer,
  };
  const wgpu::Extent3D extent{width, height, 1};
  encoder.CopyTextureToBuffer(&src, &dst, &extent);

  const wgpu::CommandBufferDescriptor cbDesc{.label = "PeekZ command buffer"};
  auto cmdBuf = encoder.Finish(&cbDesc);
  g_queue.Submit(1, &cmdBuf);

  s_mapDone = false;
  s_mapOk = false;
  const auto future = s_readbackBuffer.MapAsync(wgpu::MapMode::Read, 0, bufferSize,
                                                wgpu::CallbackMode::WaitAnyOnly, peekz_map_callback);
  const auto status = g_instance.WaitAny(future, 5000000000);
  if (status != wgpu::WaitStatus::Success || !s_mapDone || !s_mapOk) {
    Log.warn("GXPeekZ WaitAny status: {}", magic_enum::enum_name(status));
    if (s_mapDone && s_mapOk) {
      s_readbackBuffer.Unmap();
    }
    return false;
  }

  const void* mapped = s_readbackBuffer.GetConstMappedRange(0, bufferSize);
  if (mapped != nullptr) {
    std::memcpy(s_cpuDepthCache.data(), mapped, bufferSize);
  }
  s_readbackBuffer.Unmap();
  return mapped != nullptr;
}
} // namespace

extern "C" {
void GXPeekZ(u16 x, u16 y, u32* z) {
  // Default: return the "farthest" Z so callers that check `expected > peeked`
  // (to detect occlusion) will treat the pixel as unoccluded if readback fails.
  *z = 0xFFFFFFu;

  const uint32_t currentFrame = aurora::gfx::current_frame();
  if (!s_cacheValid || s_cachedFrame != currentFrame) {
    s_cacheValid = refresh_depth_cache();
    s_cachedFrame = currentFrame;
    if (!s_cacheValid) {
      return;
    }
  }

  const uint32_t width = s_cachedWidth;
  const uint32_t height = s_cachedHeight;
  if (width == 0 || height == 0) {
    return;
  }

  // Game coordinates are in GX EFB space (640x480). Scale to Aurora's FB.
  uint32_t sx = (static_cast<uint32_t>(x) * width) / 640u;
  uint32_t sy = (static_cast<uint32_t>(y) * height) / 480u;
  if (sx >= width) {
    sx = width - 1;
  }
  if (sy >= height) {
    sy = height - 1;
  }

  float depth;
  std::memcpy(&depth, s_cpuDepthCache.data() + sy * s_cachedRowPitch + sx * sizeof(float),
              sizeof(depth));
  if (depth < 0.0f) {
    depth = 0.0f;
  } else if (depth > 1.0f) {
    depth = 1.0f;
  }
  // Aurora uses reverse-Z (0 = far, 1 = near); GX expects 0 = near, 0xFFFFFF = far.
  const float gxDepth = aurora::gx::UseReversedZ ? (1.0f - depth) : depth;
  *z = static_cast<u32>(gxDepth * 16777215.0f);
}
}

#pragma once

#include <aurora/aurora.h>

#include "wgpu.hpp"

#include <array>
#include <cstdint>

struct SDL_Window;

namespace aurora::webgpu {
struct GraphicsConfig {
  uint32_t width;
  uint32_t height;
  WGPUTextureFormat colorFormat;
  WGPUTextureFormat depthFormat;
  uint32_t msaaSamples;
  uint16_t textureAnisotropy;
};
struct TextureWithSampler {
  wgpu::Texture texture;
  wgpu::TextureView view;
  WGPUExtent3D size;
  WGPUTextureFormat format;
  wgpu::Sampler sampler;

  //  TextureWithSampler() = default;
  //  TextureWithSampler(WGPUTexture texture, WGPUTextureView view, WGPUExtent3D size, WGPUTextureFormat format,
  //                     WGPUSampler sampler) noexcept
  //  : texture(texture), view(view), size(size), format(format), sampler(sampler) {}
  //  TextureWithSampler(const TextureWithSampler& rhs) noexcept
  //  : texture(rhs.texture), view(rhs.view), size(rhs.size), format(rhs.format), sampler(rhs.sampler) {
  //    wgpuTextureReference(texture);
  //    wgpuTextureViewReference(view);
  //    wgpuSamplerReference(sampler);
  //  }
  //  TextureWithSampler(TextureWithSampler&& rhs) noexcept
  //  : texture(rhs.texture), view(rhs.view), size(rhs.size), format(rhs.format), sampler(rhs.sampler) {
  //    rhs.texture = nullptr;
  //    rhs.view = nullptr;
  //    rhs.sampler = nullptr;
  //  }
  //  ~TextureWithSampler() { reset(); }
  //  TextureWithSampler& operator=(const TextureWithSampler& rhs) noexcept {
  //    reset();
  //    texture = rhs.texture;
  //    view = rhs.view;
  //    size = rhs.size;
  //    format = rhs.format;
  //    sampler = rhs.sampler;
  //    wgpuTextureReference(texture);
  //    wgpuTextureViewReference(view);
  //    wgpuSamplerReference(sampler);
  //    return *this;
  //  }
  //  void reset() {
  //    if (texture != nullptr) {
  //      wgpuTextureRelease(texture);
  //      texture = nullptr;
  //    }
  //    if (view != nullptr) {
  //      wgpuTextureViewRelease(view);
  //      view = nullptr;
  //    }
  //    if (sampler != nullptr) {
  //      wgpuSamplerRelease(sampler);
  //      sampler = nullptr;
  //    }
  //  }
};

extern WGPUDevice g_device;
extern WGPUQueue g_queue;
extern WGPUSwapChain g_swapChain;
extern WGPUBackendType g_backendType;
extern GraphicsConfig g_graphicsConfig;
extern TextureWithSampler g_frameBuffer;
extern TextureWithSampler g_frameBufferResolved;
extern TextureWithSampler g_depthBuffer;
extern WGPURenderPipeline g_CopyPipeline;
extern WGPUBindGroup g_CopyBindGroup;

bool initialize(AuroraBackend backend);
void shutdown();
void resize_swapchain(uint32_t width, uint32_t height, bool force = false);
TextureWithSampler create_render_texture(bool multisampled);
} // namespace aurora::webgpu

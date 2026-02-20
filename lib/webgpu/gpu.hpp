#pragma once

#include <aurora/aurora.h>

#include "wgpu.hpp"

#include <array>
#include <cstdint>

struct SDL_Window;

namespace aurora::webgpu {
struct GraphicsConfig {
  wgpu::SurfaceConfiguration surfaceConfiguration;
  wgpu::TextureFormat depthFormat;
  uint32_t msaaSamples;
  uint16_t textureAnisotropy;
};
struct TextureWithSampler {
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::Extent3D size;
  wgpu::TextureFormat format;
  wgpu::Sampler sampler;
};

extern wgpu::Device g_device;
extern wgpu::Queue g_queue;
extern wgpu::Surface g_surface;
extern wgpu::BackendType g_backendType;
extern GraphicsConfig g_graphicsConfig;
extern TextureWithSampler g_frameBuffer;
extern TextureWithSampler g_frameBufferResolved;
extern TextureWithSampler g_depthBuffer;
extern wgpu::RenderPipeline g_CopyPipeline;
extern wgpu::BindGroup g_CopyBindGroup;
extern wgpu::Instance g_instance;

bool initialize(AuroraBackend backend);
void shutdown();
bool refresh_surface(bool recreate = true);
void resize_swapchain(uint32_t width, uint32_t height, bool force = false);
TextureWithSampler create_render_texture(bool multisampled);
} // namespace aurora::webgpu

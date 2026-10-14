#pragma once

#include <aurora/aurora.h>
#include <aurora/math.hpp>

#include "wgpu.hpp"

#include <array>
#include <cstdint>
#include <span>

struct SDL_Window;

namespace aurora::webgpu {
inline constexpr wgpu::TextureFormat NormalBufferFormat = wgpu::TextureFormat::RGB10A2Unorm;

struct GraphicsConfig {
  wgpu::TextureFormat depthFormat;
  uint32_t msaaSamples;
  uint16_t textureAnisotropy;
  bool normalBuffer = false;
};
struct TextureWithSampler {
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::Extent3D size;
  wgpu::TextureFormat format;
  wgpu::Sampler sampler;
};
struct Viewport {
  float left;
  float top;
  float width;
  float height;
  float znear;
  float zfar;

  bool operator==(const Viewport& rhs) const {
    return left == rhs.left && top == rhs.top && width == rhs.width && height == rhs.height && znear == rhs.znear &&
           zfar == rhs.zfar;
  }
  bool operator!=(const Viewport& rhs) const { return !(*this == rhs); }
};

extern wgpu::Device g_device;
extern wgpu::Queue g_queue;
extern wgpu::Adapter g_adapter;
extern wgpu::TextureFormat g_efbFormat;
extern wgpu::BackendType g_backendType;
extern GraphicsConfig g_graphicsConfig;
extern TextureWithSampler g_frameBuffer;
extern TextureWithSampler g_frameBufferResolved;
extern TextureWithSampler g_depthBuffer;
extern TextureWithSampler g_normalBuffer;
extern wgpu::Instance g_instance;
extern wgpu::AdapterInfo g_adapterInfo;
extern bool g_hasCoreFeatures;
extern bool g_bcTexturesSupported;
extern bool g_astcTexturesSupported;
extern bool g_textureComponentSwizzleSupported;
extern bool g_dualSourceBlendingSupported;

bool initialize(AuroraBackend backend, bool allowCpu);
void shutdown();
void resize_efb(uint32_t width, uint32_t height);
TextureWithSampler create_render_texture(uint32_t width, uint32_t height, bool multisampled);
bool enable_normal_buffer();
wgpu::BindGroup create_copy_bind_group(const TextureWithSampler& source);
void set_resampler(AuroraSampler sampler) noexcept;
AuroraSampler get_resampler() noexcept;
Viewport calculate_present_viewport(uint32_t surface_width, uint32_t surface_height, uint32_t content_width,
                                    uint32_t content_height) noexcept;
struct ResampleUniformBlock {
  uint32_t samplerMode = 0;
  float frameWidth = 0.f;
  float frameHeight = 0.f;
  uint32_t padding = 0;
};
ResampleUniformBlock resample_uniform(uint32_t width, uint32_t height) noexcept;
void resample(const wgpu::CommandEncoder& encoder, const TextureWithSampler& source,
              const TextureWithSampler& destination, const wgpu::Buffer& uniforms, uint32_t offset);
wgpu::RenderPipeline copy_pipeline(wgpu::TextureFormat format, bool premultipliedAlpha = false);
wgpu::PresentMode select_present_mode(const wgpu::SurfaceCapabilities& capabilities, bool vsync) noexcept;
wgpu::PresentMode select_present_mode(std::span<const wgpu::PresentMode> modes, bool vsync) noexcept;
void draw_clear(const wgpu::RenderPassEncoder& pass, bool clearColor, bool clearAlpha, bool clearDepth,
                const Vec4<float>& clearColorValue, float clearDepthValue);

size_t load_from_cache(void const* key, size_t keySize, void* value, size_t valueSize, void* userdata);
void store_to_cache(void const* key, size_t keySize, void const* value, size_t valueSize, void* userdata);
void cache_prune();
void cache_shutdown();

} // namespace aurora::webgpu

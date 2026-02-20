#include <aurora/aurora.h>

#include "gfx/common.hpp"
#include "imgui.hpp"
#include "input.hpp"
#include "internal.hpp"
#include "webgpu/gpu.hpp"
#include "window.hpp"

#include <SDL3/SDL_filesystem.h>
#include <magic_enum.hpp>
#include <webgpu/webgpu_cpp.h>

namespace aurora {
AuroraConfig g_config;

namespace {
Module Log("aurora");

// GPU
using webgpu::g_device;
using webgpu::g_queue;
using webgpu::g_surface;

constexpr std::array PreferredBackendOrder{
#ifdef ENABLE_BACKEND_WEBGPU
    BACKEND_WEBGPU,
#endif
#ifdef DAWN_ENABLE_BACKEND_D3D12
    BACKEND_D3D12,
#endif
#ifdef DAWN_ENABLE_BACKEND_METAL
    BACKEND_METAL,
#endif
#ifdef DAWN_ENABLE_BACKEND_VULKAN
    BACKEND_VULKAN,
#endif
#ifdef DAWN_ENABLE_BACKEND_D3D11
    BACKEND_D3D11,
#endif
// #ifdef DAWN_ENABLE_BACKEND_DESKTOP_GL
//     BACKEND_OPENGL,
// #endif
// #ifdef DAWN_ENABLE_BACKEND_OPENGLES
//     BACKEND_OPENGLES,
// #endif
#ifdef DAWN_ENABLE_BACKEND_NULL
    BACKEND_NULL,
#endif
};

bool g_initialFrame = false;

AuroraInfo initialize(int argc, char* argv[], const AuroraConfig& config) noexcept {
  g_config = config;
  Log.info("Aurora initializing");
  if (g_config.appName == nullptr) {
    g_config.appName = "Aurora";
  }
  if (g_config.configPath == nullptr) {
    g_config.configPath = SDL_GetPrefPath(nullptr, g_config.appName);
  }
  if (g_config.msaa == 0) {
    g_config.msaa = 1;
  }
  if (g_config.maxTextureAnisotropy == 0) {
    g_config.maxTextureAnisotropy = 16;
  }
  ASSERT(window::initialize(), "Error initializing window");

  /* Attempt to create a window using the calling application's desired backend */
  AuroraBackend selectedBackend = config.desiredBackend;
  bool windowCreated = false;
  if (selectedBackend != BACKEND_AUTO && window::create_window(selectedBackend)) {
    if (webgpu::initialize(selectedBackend)) {
      windowCreated = true;
    } else {
      window::destroy_window();
    }
  }

  if (!windowCreated) {
    for (const auto backendType : PreferredBackendOrder) {
      selectedBackend = backendType;
      if (!window::create_window(selectedBackend)) {
        continue;
      }
      if (webgpu::initialize(selectedBackend)) {
        windowCreated = true;
        break;
      } else {
        window::destroy_window();
      }
    }
  }

  ASSERT(windowCreated, "Error creating window: {}", SDL_GetError());

  // Initialize SDL_Renderer for ImGui when we can't use a Dawn backend
  if (webgpu::g_backendType == wgpu::BackendType::Null) {
    ASSERT(window::create_renderer(), "Failed to initialize SDL renderer: {}", SDL_GetError());
  }

  window::show_window();

  gfx::initialize();

  imgui::create_context();
  const auto size = window::get_window_size();
  Log.info("Using framebuffer size {}x{} scale {}", size.fb_width, size.fb_height, size.scale);
  if (g_config.imGuiInitCallback != nullptr) {
    g_config.imGuiInitCallback(&size);
  }
  imgui::initialize();

  g_initialFrame = true;
  g_config.desiredBackend = selectedBackend;
  return {
      .backend = selectedBackend,
      .configPath = g_config.configPath,
      .window = window::get_sdl_window(),
      .windowSize = size,
  };
}

wgpu::TextureView g_currentView;

void shutdown() noexcept {
  g_currentView = {};
  imgui::shutdown();
  gfx::shutdown();
  webgpu::shutdown();
  window::shutdown();
}

const AuroraEvent* update() noexcept {
  if (g_initialFrame) {
    g_initialFrame = false;
    input::initialize();
  }
  return window::poll_events();
}

bool begin_frame() noexcept {
  if (!g_surface) {
    webgpu::refresh_surface(true);
    if (!g_surface) {
      return false;
    }
  }
  wgpu::SurfaceTexture surfaceTexture;
  g_surface.GetCurrentTexture(&surfaceTexture);
  switch (surfaceTexture.status) {
  case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
    g_currentView = surfaceTexture.texture.CreateView();
    break;
  case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
    Log.warn("Surface texture acquisition timed out");
    return false;
  case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
  case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
    Log.info("Surface texture is {}, reconfiguring swapchain", magic_enum::enum_name(surfaceTexture.status));
    webgpu::refresh_surface(false);
    return false;
  case wgpu::SurfaceGetCurrentTextureStatus::Lost:
  case wgpu::SurfaceGetCurrentTextureStatus::Error:
    Log.warn("Surface texture is {}, recreating surface", magic_enum::enum_name(surfaceTexture.status));
    webgpu::refresh_surface(true);
    return false;
  default:
    Log.error("Failed to get surface texture: {}", magic_enum::enum_name(surfaceTexture.status));
    return false;
  }
  imgui::new_frame(window::get_window_size());
  gfx::begin_frame();
  return true;
}

void end_frame() noexcept {
  const auto encoderDescriptor = wgpu::CommandEncoderDescriptor{
      .label = "Redraw encoder",
  };
  auto encoder = g_device.CreateCommandEncoder(&encoderDescriptor);
  gfx::end_frame(encoder);
  gfx::render(encoder);
  {
    const std::array attachments{
        wgpu::RenderPassColorAttachment{
            .view = g_currentView,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
        },
    };
    const wgpu::RenderPassDescriptor renderPassDescriptor{
        .label = "Post render pass",
        .colorAttachmentCount = attachments.size(),
        .colorAttachments = attachments.data(),
    };
    auto pass = encoder.BeginRenderPass(&renderPassDescriptor);
    // Copy EFB -> XFB (swapchain)
    pass.SetPipeline(webgpu::g_CopyPipeline);
    pass.SetBindGroup(0, webgpu::g_CopyBindGroup, 0, nullptr);
    pass.Draw(3);
    imgui::render(pass);
    pass.End();
  }
  const wgpu::CommandBufferDescriptor cmdBufDescriptor{.label = "Redraw command buffer"};
  const auto buffer = encoder.Finish(&cmdBufDescriptor);
  g_queue.Submit(1, &buffer);
  g_surface.Present();
  g_currentView = {};
}
} // namespace
} // namespace aurora

// C API bindings
AuroraInfo aurora_initialize(int argc, char* argv[], const AuroraConfig* config) {
  return aurora::initialize(argc, argv, *config);
}
void aurora_shutdown() { aurora::shutdown(); }
const AuroraEvent* aurora_update() { return aurora::update(); }
bool aurora_begin_frame() { return aurora::begin_frame(); }
void aurora_end_frame() { aurora::end_frame(); }
AuroraBackend aurora_get_backend() { return aurora::g_config.desiredBackend; }
const AuroraBackend* aurora_get_available_backends(size_t* count) {
  *count = aurora::PreferredBackendOrder.size();
  return aurora::PreferredBackendOrder.data();
}

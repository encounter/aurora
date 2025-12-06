#include "imgui.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <webgpu/webgpu_cpp.h>
#include <SDL3/SDL_render.h>

#include "internal.hpp"
#include "webgpu/gpu.hpp"
#include "window.hpp"

#define IMGUI_IMPL_WEBGPU_BACKEND_DAWN
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "backends/imgui_impl_wgpu.h"

namespace aurora::imgui {
static float g_scale;
static std::string g_imguiSettings{};
static std::string g_imguiLog{};
static bool g_useSdlRenderer = false;

static std::vector<SDL_Texture*> g_sdlTextures;
static std::vector<wgpu::Texture> g_wgpuTextures;

void create_context() noexcept {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  g_imguiSettings = std::string{g_config.configPath} + "/imgui.ini";
  g_imguiLog = std::string{g_config.configPath} + "/imgui.log";
  io.IniFilename = g_imguiSettings.c_str();
  io.LogFilename = g_imguiLog.c_str();
}

void initialize() noexcept {
  SDL_Renderer* renderer = window::get_sdl_renderer();
  ImGui_ImplSDL3_InitForSDLRenderer(window::get_sdl_window(), renderer);
  g_useSdlRenderer = renderer != nullptr;
  if (g_useSdlRenderer) {
    ImGui_ImplSDLRenderer3_Init(renderer);
  } else {
    ImGui_ImplWGPU_InitInfo info;
    info.Device = webgpu::g_device.Get();
    info.RenderTargetFormat = static_cast<WGPUTextureFormat>(webgpu::g_graphicsConfig.surfaceConfiguration.format);
    ImGui_ImplWGPU_Init(&info);
  }
}

void shutdown() noexcept {
  if (g_useSdlRenderer) {
    ImGui_ImplSDLRenderer3_Shutdown();
  } else {
    ImGui_ImplWGPU_Shutdown();
  }
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  for (const auto& texture : g_sdlTextures) {
    SDL_DestroyTexture(texture);
  }
  g_sdlTextures.clear();
  g_wgpuTextures.clear();
}

void process_event(const SDL_Event& event) noexcept {
  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    SDL_Event scaledEvent = event;
    const auto density = SDL_GetWindowPixelDensity(window::get_sdl_window());
    scaledEvent.motion.x *= density;
    scaledEvent.motion.y *= density;
    scaledEvent.motion.xrel *= density;
    scaledEvent.motion.yrel *= density;
    ImGui_ImplSDL3_ProcessEvent(&scaledEvent);
    return;
  }
  ImGui_ImplSDL3_ProcessEvent(&event);
}

void new_frame(const AuroraWindowSize& size) noexcept {
  if (g_useSdlRenderer) {
    ImGui_ImplSDLRenderer3_NewFrame();
    g_scale = size.scale;
  } else {
    if (g_scale != size.scale) {
      if (g_scale > 0.f) {
        ImGui_ImplWGPU_CreateDeviceObjects();
      }
      g_scale = size.scale;
    }
    ImGui_ImplWGPU_NewFrame();
  }
  ImGui_ImplSDL3_NewFrame();

  // Render at full DPI
  ImGui::GetIO().DisplaySize = {
      static_cast<float>(size.fb_width),
      static_cast<float>(size.fb_height),
  };
  ImGui::NewFrame();
}

void render(const wgpu::RenderPassEncoder& pass) noexcept {
  ImGui::Render();

  auto* data = ImGui::GetDrawData();
  // io.DisplayFramebufferScale is informational; we're rendering at full DPI
  data->FramebufferScale = {1.f, 1.f};
  if (g_useSdlRenderer) {
    SDL_Renderer* renderer = window::get_sdl_renderer();
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(data, renderer);
    SDL_RenderPresent(renderer);
  } else {
    ImGui_ImplWGPU_RenderDrawData(data, pass.Get());
  }
}

ImTextureID add_texture(uint32_t width, uint32_t height, const uint8_t* data) noexcept {
  if (g_useSdlRenderer) {
    SDL_Renderer* renderer = window::get_sdl_renderer();
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    SDL_UpdateTexture(texture, nullptr, data, width * 4);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    g_sdlTextures.push_back(texture);
    return reinterpret_cast<ImTextureID>(texture);
  }
  const wgpu::Extent3D size{
      .width = width,
      .height = height,
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDescriptor{
      .label = "imgui texture",
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = wgpu::TextureFormat::RGBA8Unorm,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = "imgui texture view",
      .format = wgpu::TextureFormat::RGBA8Unorm,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED,
      .arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
  };
  auto texture = webgpu::g_device.CreateTexture(&textureDescriptor);
  auto textureView = texture.CreateView(&textureViewDescriptor);
  {
    const wgpu::TexelCopyTextureInfo dstView{
        .texture = texture,
    };
    const wgpu::TexelCopyBufferLayout dataLayout{
        .bytesPerRow = 4 * width,
        .rowsPerImage = height,
    };
    webgpu::g_queue.WriteTexture(&dstView, data, width * height * 4, &dataLayout, &size);
  }
  g_wgpuTextures.push_back(texture);
  return reinterpret_cast<ImTextureID>(textureView.MoveToCHandle());
}
} // namespace aurora::imgui

// C bindings
extern "C" {
ImTextureID aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8) {
  return aurora::imgui::add_texture(width, height, static_cast<const uint8_t*>(rgba8));
}
}

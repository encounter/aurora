#include "imgui.hpp"

#include "webgpu/gpu.hpp"
#include "internal.hpp"
#include "window.hpp"

#include <SDL.h>
#include <webgpu/webgpu.h>

#include "../imgui/backends/imgui_impl_sdl.cpp"         // NOLINT(bugprone-suspicious-include)
#include "../imgui/backends/imgui_impl_sdlrenderer.cpp" // NOLINT(bugprone-suspicious-include)
// #include "../imgui/backends/imgui_impl_wgpu.cpp"     // NOLINT(bugprone-suspicious-include)
// TODO: Transition back to imgui-provided backend when it uses WGSL
#include "imgui_impl_wgpu.cpp"                          // NOLINT(bugprone-suspicious-include)

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
  ImGui_ImplSDL2_Init(window::get_sdl_window(), renderer);
#ifdef __APPLE__
  // Disable MouseCanUseGlobalState for scaling purposes
  ImGui_ImplSDL2_GetBackendData()->MouseCanUseGlobalState = false;
#endif
  g_useSdlRenderer = renderer != nullptr;
  if (g_useSdlRenderer) {
    ImGui_ImplSDLRenderer_Init(renderer);
  } else {
    const auto format = webgpu::g_graphicsConfig.swapChainDescriptor.format;
    ImGui_ImplWGPU_Init(webgpu::g_device.Get(), 1, static_cast<WGPUTextureFormat>(format));
  }
}

void shutdown() noexcept {
  if (g_useSdlRenderer) {
    ImGui_ImplSDLRenderer_Shutdown();
  } else {
    ImGui_ImplWGPU_Shutdown();
  }
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  for (const auto& texture : g_sdlTextures) {
    SDL_DestroyTexture(texture);
  }
  g_sdlTextures.clear();
  g_wgpuTextures.clear();
}

void process_event(const SDL_Event& event) noexcept {
#ifdef __APPLE__
  if (event.type == SDL_MOUSEMOTION) {
    auto& io = ImGui::GetIO();
    // Scale up mouse coordinates
    io.AddMousePosEvent(static_cast<float>(event.motion.x) * g_scale, static_cast<float>(event.motion.y) * g_scale);
    return;
  }
#endif
  ImGui_ImplSDL2_ProcessEvent(&event);
}

void new_frame(const AuroraWindowSize& size) noexcept {
  if (g_useSdlRenderer) {
    ImGui_ImplSDLRenderer_NewFrame();
  } else {
    if (g_scale != size.scale) {
      if (g_scale > 0.f) {
        // TODO wgpu backend bug: doesn't clear bind groups on invalidate
        g_resources.ImageBindGroups.Clear();
        ImGui_ImplWGPU_CreateDeviceObjects();
      }
      g_scale = size.scale;
    }
    ImGui_ImplWGPU_NewFrame();
  }
  ImGui_ImplSDL2_NewFrame();

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
    SDL_Renderer* renderer = ImGui_ImplSDLRenderer_GetBackendData()->SDLRenderer;
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer_RenderDrawData(data);
    SDL_RenderPresent(renderer);
  } else {
    ImGui_ImplWGPU_RenderDrawData(data, pass.Get());
  }
}

ImTextureID add_texture(uint32_t width, uint32_t height, const uint8_t* data) noexcept {
  if (g_useSdlRenderer) {
    SDL_Renderer* renderer = ImGui_ImplSDLRenderer_GetBackendData()->SDLRenderer;
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    SDL_UpdateTexture(texture, nullptr, data, width * 4);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    g_sdlTextures.push_back(texture);
    return texture;
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
    const wgpu::ImageCopyTexture dstView{
        .texture = texture,
    };
    const wgpu::TextureDataLayout dataLayout{
        .bytesPerRow = 4 * width,
        .rowsPerImage = height,
    };
    webgpu::g_queue.WriteTexture(&dstView, data, width * height * 4, &dataLayout, &size);
  }
  g_wgpuTextures.push_back(texture);
  return textureView.Release();
}
} // namespace aurora::imgui

// C bindings
extern "C" {
ImTextureID aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8) {
  return aurora::imgui::add_texture(width, height, static_cast<const uint8_t*>(rgba8));
}
}

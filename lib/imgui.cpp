#include "imgui.hpp"

#include <cstddef>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <webgpu/webgpu_cpp.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>

#include "internal.hpp"
#include "gfx/render_worker.hpp"
#include "webgpu/gpu.hpp"
#include "window.hpp"

#define IMGUI_IMPL_WEBGPU_BACKEND_DAWN
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "backends/imgui_impl_wgpu.h"
#include "tracy/Tracy.hpp"

namespace aurora::imgui {
static float g_scale;
static std::string g_imguiSettings{};
static std::string g_imguiLog{};
static bool g_useSdlRenderer = false;

static std::vector<SDL_Texture*> g_sdlTextures;
static std::vector<wgpu::Texture> g_wgpuTextures;

struct DrawData::Impl {
  ImDrawData drawData;
  std::vector<std::unique_ptr<ImDrawList>> drawLists;
};

void create_context() noexcept {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  g_imguiSettings = std::string{g_config.userPath} + "/imgui.ini";
  g_imguiLog = std::string{g_config.cachePath} + "/imgui.log";
  io.IniFilename = g_imguiSettings.c_str();
  io.LogFilename = g_imguiLog.c_str();
}

void initialize() noexcept {
  ZoneScoped;
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
  ZoneScoped;
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
  auto renderEvent = event;
  if (g_useSdlRenderer) {
    if (SDL_Renderer* renderer = window::get_sdl_renderer()) {
      SDL_ConvertEventToRenderCoordinates(renderer, &renderEvent);
    }
  }
  ImGui_ImplSDL3_ProcessEvent(&renderEvent);
}

bool wants_capture_event(const SDL_Event& event) noexcept {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }

  const ImGuiIO& io = ImGui::GetIO();
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_WHEEL:
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    return io.WantCaptureMouse;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
  case SDL_EVENT_TEXT_INPUT:
    return io.WantCaptureKeyboard || io.WantTextInput;
  default:
    return false;
  }
}

void new_frame(const AuroraWindowSize& size) noexcept {
  ZoneScoped;
  ImVec2 framebufferScale{
      size.width > 0 ? static_cast<float>(size.native_fb_width) / static_cast<float>(size.width) : 1.0f,
      size.height > 0 ? static_cast<float>(size.native_fb_height) / static_cast<float>(size.height) : 1.0f,
  };
  ImVec2 displaySize{static_cast<float>(size.width), static_cast<float>(size.height)};

  if (g_useSdlRenderer) {
    if (SDL_Renderer* renderer = window::get_sdl_renderer()) {
      float renderScaleX = 1.0f;
      float renderScaleY = 1.0f;
      SDL_GetRenderScale(renderer, &renderScaleX, &renderScaleY);
      if (renderScaleX > 0.0f && renderScaleY > 0.0f &&
          (std::fabs(renderScaleX - 1.0f) > 0.0001f || std::fabs(renderScaleY - 1.0f) > 0.0001f)) {
        int outputWidth = static_cast<int>(size.native_fb_width);
        int outputHeight = static_cast<int>(size.native_fb_height);
        SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
        displaySize = {
            static_cast<float>(outputWidth) / renderScaleX,
            static_cast<float>(outputHeight) / renderScaleY,
        };
        framebufferScale = {renderScaleX, renderScaleY};
      }
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    g_scale = size.scale;
  } else {
    if (g_scale != size.scale) {
      if (g_scale > 0.f) {
        ImGui_ImplWGPU_CreateDeviceObjects();
      }
      g_scale = size.scale;
    }
    if (!ImGui::GetIO().Fonts->IsBuilt()) {
      ImGui_ImplWGPU_CreateDeviceObjects();
    }
    ImGui_ImplWGPU_NewFrame();
  }
  ImGui_ImplSDL3_NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  io.DisplayFramebufferScale = framebufferScale;
  ImGui::GetIO().DisplaySize = displaySize;
  ImGui::NewFrame();
}

DrawData freeze() noexcept {
  ZoneScoped;
  ImGui::Render();

  auto* data = ImGui::GetDrawData();
  data->FramebufferScale = ImGui::GetIO().DisplayFramebufferScale;
  auto frozen = std::make_shared<DrawData::Impl>();
  frozen->drawData = *data;
  frozen->drawLists.reserve(data->CmdListsCount);
  frozen->drawData.CmdLists.resize(data->CmdListsCount);
  for (int i = 0; i < data->CmdListsCount; ++i) {
    frozen->drawLists.emplace_back(data->CmdLists[i]->CloneOutput());
    frozen->drawData.CmdLists[i] = frozen->drawLists.back().get();
  }
  return DrawData{std::move(frozen)};
}

void render(const wgpu::RenderPassEncoder& pass, const DrawData& drawData) noexcept {
  ZoneScoped;

  if (!drawData.m_impl) {
    return;
  }
  auto* data = &drawData.m_impl->drawData;
  if (data->CmdListsCount == 0) {
    return;
  }
  if (g_useSdlRenderer) {
    SDL_Renderer* renderer = window::get_sdl_renderer();
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(data, renderer);
    SDL_RenderPresent(renderer);
  } else {
    pass.PushDebugGroup("Aurora: Dear Imgui");
    ImGui_ImplWGPU_RenderDrawData(data, pass.Get());
    pass.PopDebugGroup();
  }
}

static wgpu::Buffer create_texture_upload_buffer(uint32_t width, uint32_t height, const uint8_t* data,
                                                 uint32_t copyBytesPerRow) {
  const uint32_t rowBytes = width * 4;
  const uint64_t uploadSize = static_cast<uint64_t>(copyBytesPerRow) * height;
  const wgpu::BufferDescriptor desc{
      .label = "imgui texture upload buffer",
      .usage = wgpu::BufferUsage::CopySrc,
      .size = uploadSize,
      .mappedAtCreation = true,
  };
  auto buffer = webgpu::g_device.CreateBuffer(&desc);
  auto* dst = static_cast<uint8_t*>(buffer.GetMappedRange(0, uploadSize));
  for (uint32_t row = 0; row < height; ++row) {
    std::memcpy(dst, data, rowBytes);
    dst += copyBytesPerRow;
    data += rowBytes;
  }
  buffer.Unmap();
  return buffer;
}

static void enqueue_texture_upload(wgpu::Buffer buffer, wgpu::TexelCopyTextureInfo dst,
                                   wgpu::TexelCopyBufferLayout layout, wgpu::Extent3D size) {
  gfx::render_worker::enqueue_work([buffer = std::move(buffer), dst = std::move(dst), layout, size] {
    const wgpu::CommandEncoderDescriptor encoderDesc{
        .label = "imgui texture upload encoder",
    };
    auto encoder = webgpu::g_device.CreateCommandEncoder(&encoderDesc);
    const wgpu::TexelCopyBufferInfo src{
        .layout = layout,
        .buffer = buffer,
    };
    encoder.CopyBufferToTexture(&src, &dst, &size);
    const wgpu::CommandBufferDescriptor commandBufferDesc{
        .label = "imgui texture upload command buffer",
    };
    auto commandBuffer = encoder.Finish(&commandBufferDesc);
    webgpu::g_queue.Submit(1, &commandBuffer);
  });
}

ImTextureID add_texture(uint32_t width, uint32_t height, const uint8_t* data) noexcept {
  if (SDL_Renderer* renderer = window::get_sdl_renderer()) {
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
    const uint32_t copyBytesPerRow = AURORA_ALIGN(width * 4, 256);
    const wgpu::TexelCopyBufferLayout dataLayout{
        .bytesPerRow = copyBytesPerRow,
        .rowsPerImage = height,
    };
    enqueue_texture_upload(create_texture_upload_buffer(width, height, data, copyBytesPerRow), dstView, dataLayout, size);
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

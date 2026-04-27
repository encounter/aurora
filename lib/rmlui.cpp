#include "rmlui.hpp"

#include <RmlUi/Core.h>
#include <RmlUi_Backend.h>
#include <RmlUi_Platform_SDL.h>

#include "window.hpp"
#include "internal.hpp"
#include "rmlui/WebGPURenderInterface.hpp"
#include "webgpu/gpu.hpp"

namespace aurora::rmlui {
Module Log("aurora::rmlui");

Rml::Context* g_context = nullptr;

void initialize(const AuroraWindowSize& window_size) noexcept {
  if (!Backend::Initialize("Aurora RmlUi Backend", window_size.native_fb_width, window_size.native_fb_height, false)) {
    Log.error("Failed to initialize RmlUI Backend!");
    return;
  }

  WebGPURenderInterface* render_interface = static_cast<WebGPURenderInterface*>(Backend::GetRenderInterface());

  Rml::SetSystemInterface(Backend::GetSystemInterface());
  Rml::SetRenderInterface(render_interface);

  render_interface->SetWindowSize({(int)window_size.native_fb_width, (int)window_size.native_fb_height});
  render_interface->SetRenderTargetFormat(webgpu::g_graphicsConfig.surfaceConfiguration.format);

  render_interface->CreateDeviceObjects();

  Rml::Initialise();

  g_context = Rml::CreateContext("main", Rml::Vector2i(window_size.native_fb_width, window_size.native_fb_height));

  if (!g_context) {
    Log.error("Failed to initialize RmlUI Context!");

    Rml::Shutdown();
    Backend::Shutdown();
  }
}

Rml::Context* get_context() noexcept { return g_context; }

bool is_initialized() noexcept { return g_context != nullptr; }

void handle_event(SDL_Event& event) noexcept {
  if (g_context == nullptr) {
    return;
  }

  RmlSDL::InputEventHandler(g_context, window::get_sdl_window(), event);
}

void render(const wgpu::RenderPassEncoder& pass) noexcept {
  if (g_context == nullptr) {
    return;
  }

  g_context->Update();

  WebGPURenderInterface* render_interface = static_cast<WebGPURenderInterface*>(Backend::GetRenderInterface());
  render_interface->SetRenderPass(&pass);
  render_interface->SetWindowSize(g_context->GetDimensions());
  render_interface->NewFrame();

  pass.PushDebugGroup("Aurora: RmlUi");

  Backend::BeginFrame();
  g_context->Render();
  Backend::PresentFrame();

  pass.PopDebugGroup();

  render_interface->SetRenderPass(nullptr);
}

void shutdown() noexcept {
  if (g_context == nullptr) {
    return;
  }

  Rml::Shutdown();
  Backend::Shutdown();
  g_context = nullptr;
}
} // namespace aurora::rmlui

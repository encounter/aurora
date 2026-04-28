#include "rmlui.hpp"

#include <RmlUi/Core.h>
#include <RmlUi_Backend.h>
#include <RmlUi_Platform_SDL.h>

#include "window.hpp"
#include "internal.hpp"
#include "rmlui/WebGPURenderInterface.hpp"
#include "webgpu/gpu.hpp"

namespace aurora::rmlui {
namespace {
Module Log("aurora::rmlui");

WebGPURenderInterface* get_render_interface() noexcept {
  return static_cast<WebGPURenderInterface*>(Backend::GetRenderInterface()); // NOLINT(*-pro-type-static-cast-downcast)
}
} // namespace

Rml::Context* g_context = nullptr;

void initialize(const AuroraWindowSize& size) noexcept {
  int width = static_cast<int>(size.native_fb_width);
  int height = static_cast<int>(size.native_fb_height);
  if (!Backend::Initialize("Aurora RmlUi Backend", width, height, false)) {
    Log.error("Failed to initialize RmlUI Backend!");
    return;
  }

  auto* renderInterface = get_render_interface();
  Rml::SetSystemInterface(Backend::GetSystemInterface());
  Rml::SetRenderInterface(renderInterface);

  renderInterface->SetWindowSize({width, height});
  renderInterface->SetRenderTargetFormat(webgpu::g_graphicsConfig.surfaceConfiguration.format);
  renderInterface->CreateDeviceObjects();

  Rml::Initialise();
  g_context = Rml::CreateContext("main", Rml::Vector2i(width, height));

  if (!g_context) {
    Log.error("Failed to initialize RmlUI Context!");

    Rml::Shutdown();
    Backend::Shutdown();
  }
}

Rml::Context* get_context() noexcept { return g_context; }

bool is_initialized() noexcept { return g_context != nullptr; }

wgpu::TextureView prepare_stencil_view(const wgpu::Extent3D& size) noexcept {
  return get_render_interface()->GetClipMaskStencilView(size);
}

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

  auto* renderInterface = get_render_interface();
  renderInterface->SetRenderPass(&pass);
  renderInterface->SetWindowSize(g_context->GetDimensions());
  renderInterface->NewFrame();

  pass.PushDebugGroup("Aurora: RmlUi");

  Backend::BeginFrame();
  g_context->Render();
  Backend::PresentFrame();

  pass.PopDebugGroup();

  renderInterface->SetRenderPass(nullptr);
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

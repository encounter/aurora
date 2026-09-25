#include "rmlui.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi_Backend.h>
#include <RmlUi_Platform_SDL.h>
#include <tracy/Tracy.hpp>

#include "window.hpp"
#include "internal.hpp"
#include "rmlui/FileInterface_SDL.h"
#include "rmlui/GlassFilter.hpp"
#include "rmlui/ImageEffects.hpp"
#include "rmlui/SystemInterface_Aurora.h"
#include "rmlui/WebGPURenderInterface.hpp"
#include "webgpu/gpu.hpp"

namespace aurora::rmlui {
Rml::Context* g_context = nullptr;
FileInterface_SDL* g_fileInterface = nullptr;

namespace {
Module Log("aurora::rmlui");
constexpr size_t MaxTrackedTouches = 16;

struct TrackedTouch {
  SDL_FingerID id = 0;
  Rml::Vector2f position;
  Rml::Vector2f rmlPosition;
  Rml::Vector2f startPosition;
  Rml::ObserverPtr<Rml::Element> target;
  bool active = false;
};

uint32_t s_pressedMouseButtons = 0;
std::array<TrackedTouch, MaxTrackedTouches> s_trackedTouches{};
float s_uiScale = 0.0f;
webgpu::TextureWithSampler s_renderTarget;
wgpu::BindGroup s_renderTargetCopyBindGroup;

WebGPURenderInterface* get_render_interface() noexcept {
  return static_cast<WebGPURenderInterface*>(Backend::GetRenderInterface()); // NOLINT(*-pro-type-static-cast-downcast)
}

Rml::Vector2i dimensions_from_viewport(const gfx::Viewport& viewport) noexcept {
  return {
      std::max(1, static_cast<int>(std::lround(viewport.width))),
      std::max(1, static_cast<int>(std::lround(viewport.height))),
  };
}

Rml::Vector2i presentation_dimensions_from_window_size(const AuroraWindowSize& size) noexcept {
  const auto viewport =
      webgpu::calculate_present_viewport(size.native_fb_width, size.native_fb_height, size.fb_width, size.fb_height);
  return dimensions_from_viewport(viewport);
}

void sync_context_metrics(Rml::Vector2i dimensions) noexcept {
  if (g_context == nullptr || dimensions.x <= 0 || dimensions.y <= 0) {
    return;
  }
  if (g_context->GetDimensions() != dimensions) {
    g_context->SetDimensions(dimensions);
  }
  const float ratio = s_uiScale > 0.0f ? s_uiScale : window::get_window_size().scale;
  if (g_context->GetDensityIndependentPixelRatio() != ratio) {
    g_context->SetDensityIndependentPixelRatio(ratio);
  }
}

void ensure_render_target(Rml::Vector2i dimensions) noexcept {
  if (dimensions.x <= 0 || dimensions.y <= 0) {
    return;
  }
  const auto width = static_cast<uint32_t>(dimensions.x);
  const auto height = static_cast<uint32_t>(dimensions.y);
  if (s_renderTarget.view && s_renderTarget.size.width == width && s_renderTarget.size.height == height) {
    return;
  }
  s_renderTarget = webgpu::create_render_texture(width, height, false);
  s_renderTargetCopyBindGroup = webgpu::create_copy_bind_group(s_renderTarget);
}

bool element_has_visible_backdrop_filter(const Rml::Element* element) noexcept {
  if (element == nullptr || !element->IsVisible(true)) {
    return false;
  }

  const auto& computed = element->GetComputedValues();
  if (computed.opacity() > 0.f && computed.has_backdrop_filter()) {
    return true;
  }

  const int childCount = element->GetNumChildren();
  for (int childIndex = 0; childIndex < childCount; ++childIndex) {
    if (element_has_visible_backdrop_filter(element->GetChild(childIndex))) {
      return true;
    }
  }
  return false;
}

bool context_has_visible_backdrop_filter(Rml::Context* context) noexcept {
  if (context == nullptr) {
    return false;
  }
  return element_has_visible_backdrop_filter(context->GetRootElement());
}

struct MappedPoint {
  Rml::Vector2f position;
  bool valid = false;
  bool inside = false;
};

MappedPoint map_native_point_to_content(float nativeX, float nativeY) noexcept {
  if (g_context == nullptr) {
    return {};
  }

  const auto size = window::get_window_size();
  const Rml::Vector2i contentSize = g_context->GetDimensions();
  if (size.native_fb_width == 0 || size.native_fb_height == 0 || contentSize.x <= 0 || contentSize.y <= 0) {
    return {};
  }

  const auto viewport =
      webgpu::calculate_present_viewport(size.native_fb_width, size.native_fb_height,
                                         static_cast<uint32_t>(contentSize.x), static_cast<uint32_t>(contentSize.y));
  const float right = viewport.left + viewport.width;
  const float bottom = viewport.top + viewport.height;
  if (viewport.width <= 0.f || viewport.height <= 0.f) {
    return {};
  }

  return {
      .position =
          {
              (nativeX - viewport.left) * static_cast<float>(contentSize.x) / viewport.width,
              (nativeY - viewport.top) * static_cast<float>(contentSize.y) / viewport.height,
          },
      .valid = true,
      .inside = nativeX >= viewport.left && nativeY >= viewport.top && nativeX < right && nativeY < bottom,
  };
}

MappedPoint map_window_point_to_content(float windowX, float windowY) noexcept {
  const auto size = window::get_window_size();
  if (size.width == 0 || size.height == 0) {
    return {};
  }
  return map_native_point_to_content(
      windowX * static_cast<float>(size.native_fb_width) / static_cast<float>(size.width),
      windowY * static_cast<float>(size.native_fb_height) / static_cast<float>(size.height));
}

int rounded_content_coord(float value) noexcept { return static_cast<int>(std::floor(value)); }

bool mouse_button_tracked(uint8_t button) noexcept {
  return button < 32 && (s_pressedMouseButtons & (1u << button)) != 0;
}

void set_mouse_button_tracked(uint8_t button, bool tracked) noexcept {
  if (button >= 32) {
    return;
  }
  const uint32_t mask = 1u << button;
  if (tracked) {
    s_pressedMouseButtons |= mask;
  } else {
    s_pressedMouseButtons &= ~mask;
  }
}

TrackedTouch* find_tracked_touch(SDL_FingerID id) noexcept {
  for (auto& touch : s_trackedTouches) {
    if (touch.active && touch.id == id) {
      return &touch;
    }
  }
  return nullptr;
}

TrackedTouch* find_free_touch() noexcept {
  for (auto& touch : s_trackedTouches) {
    if (!touch.active) {
      return &touch;
    }
  }
  return nullptr;
}

Rml::TouchList touch_list(SDL_FingerID id, Rml::Vector2f position) {
  return {Rml::Touch{static_cast<Rml::TouchId>(id), position}};
}

void dispatch_touch_event(TrackedTouch& touch, const char* type, Rml::Vector2f position, bool inside,
                          Rml::Vector2f delta = {}) noexcept {
  if (touch.target == nullptr) {
    return;
  }

  Rml::Dictionary parameters;
  parameters["finger_id"] = touch.id;
  parameters["x"] = position.x;
  parameters["y"] = position.y;
  parameters["dx"] = delta.x;
  parameters["dy"] = delta.y;
  parameters["start_x"] = touch.startPosition.x;
  parameters["start_y"] = touch.startPosition.y;
  parameters["inside"] = inside;
  touch.target->DispatchEvent(type, parameters, true, true);
}

int key_modifiers(SDL_Keymod mod) noexcept {
  int modifiers = 0;
  if ((mod & SDL_KMOD_CTRL) != 0) {
    modifiers |= Rml::Input::KM_CTRL;
  }
  if ((mod & SDL_KMOD_SHIFT) != 0) {
    modifiers |= Rml::Input::KM_SHIFT;
  }
  if ((mod & SDL_KMOD_ALT) != 0) {
    modifiers |= Rml::Input::KM_ALT;
  }
  if ((mod & SDL_KMOD_GUI) != 0) {
    modifiers |= Rml::Input::KM_META;
  }
  if ((mod & SDL_KMOD_CAPS) != 0) {
    modifiers |= Rml::Input::KM_CAPSLOCK;
  }
  if ((mod & SDL_KMOD_NUM) != 0) {
    modifiers |= Rml::Input::KM_NUMLOCK;
  }
  if ((mod & SDL_KMOD_SCROLL) != 0) {
    modifiers |= Rml::Input::KM_SCROLLLOCK;
  }
  return modifiers;
}

using Pointer = input::InputEvent::PointerChanged;

// RmlUi's Process* functions return true while the event is still propagating.
InputResult input_result(bool stillPropagating, Rml::Element* target = nullptr) noexcept {
  return {.handled = !stillPropagating, .target = target};
}

InputResult process_mouse(const Pointer& pointer) noexcept {
  const MappedPoint mapped = map_window_point_to_content(pointer.position.x, pointer.position.y);
  const int modifiers = key_modifiers(pointer.modifiers);
  switch (pointer.phase) {
  case Pointer::Phase::Move:
    if (!mapped.inside) {
      g_context->ProcessMouseLeave();
      return {};
    }
    return input_result(g_context->ProcessMouseMove(rounded_content_coord(mapped.position.x),
                                                    rounded_content_coord(mapped.position.y), modifiers));
  case Pointer::Phase::Down: {
    if (!mapped.inside) {
      g_context->ProcessMouseLeave();
      return {};
    }
    g_context->ProcessMouseMove(rounded_content_coord(mapped.position.x), rounded_content_coord(mapped.position.y),
                                modifiers);
    Rml::Element* target = g_context->GetElementAtPoint(mapped.position);
    const bool stillPropagating =
        g_context->ProcessMouseButtonDown(RmlSDL::ConvertMouseButton(pointer.button), modifiers);
    set_mouse_button_tracked(pointer.button, true);
    SDL_CaptureMouse(true);
    return input_result(stillPropagating, target);
  }
  case Pointer::Phase::Up: {
    if (!mouse_button_tracked(pointer.button)) {
      return {};
    }
    if (mapped.inside) {
      g_context->ProcessMouseMove(rounded_content_coord(mapped.position.x), rounded_content_coord(mapped.position.y),
                                  modifiers);
    } else {
      g_context->ProcessMouseLeave();
    }
    const bool stillPropagating =
        g_context->ProcessMouseButtonUp(RmlSDL::ConvertMouseButton(pointer.button), modifiers);
    set_mouse_button_tracked(pointer.button, false);
    if (s_pressedMouseButtons == 0) {
      SDL_CaptureMouse(false);
    }
    return input_result(stillPropagating);
  }
  case Pointer::Phase::Cancel:
    return {};
  }
  return {};
}

void cancel_mouse() noexcept {
  g_context->ProcessMouseLeave();
  for (uint8_t button = 0; button < 32; ++button) {
    if (mouse_button_tracked(button)) {
      g_context->ProcessMouseButtonUp(RmlSDL::ConvertMouseButton(button), 0);
    }
  }
  if (s_pressedMouseButtons != 0) {
    s_pressedMouseButtons = 0;
    SDL_CaptureMouse(false);
  }
}

InputResult process_scroll(const input::InputEvent::Scroll& scroll) noexcept {
  if (!map_window_point_to_content(scroll.position.x, scroll.position.y).inside) {
    g_context->ProcessMouseLeave();
    return {};
  }
  return input_result(
      g_context->ProcessMouseWheel(Rml::Vector2f{scroll.delta.x, scroll.delta.y}, key_modifiers(scroll.modifiers)));
}

InputResult touch_down(SDL_FingerID id, const MappedPoint& mapped) noexcept {
  if (find_tracked_touch(id) != nullptr || !mapped.inside) {
    return {};
  }
  auto* tracked = find_free_touch();
  if (tracked == nullptr) {
    return {};
  }
  auto* target = g_context->GetElementAtPoint(mapped.position);
  Rml::Element* hit = target;
  if (target == nullptr) {
    target = g_context->GetRootElement();
  }
  *tracked = {
      .id = id,
      .position = mapped.position,
      .rmlPosition = mapped.position,
      .startPosition = mapped.position,
      .target = target->GetObserverPtr(),
      .active = true,
  };
  dispatch_touch_event(*tracked, TouchStartEvent, mapped.position, true);
  return input_result(g_context->ProcessTouchStart(touch_list(id, mapped.position), 0), hit);
}

InputResult touch_motion(SDL_FingerID id, const MappedPoint& mapped) noexcept {
  auto* tracked = find_tracked_touch(id);
  if (tracked == nullptr || !mapped.valid) {
    return {};
  }
  const Rml::Vector2f delta = mapped.position - tracked->position;
  tracked->position = mapped.position;
  dispatch_touch_event(*tracked, TouchMoveEvent, mapped.position, mapped.inside, delta);
  if (!mapped.inside) {
    return {};
  }
  tracked->rmlPosition = mapped.position;
  return input_result(g_context->ProcessTouchMove(touch_list(id, mapped.position), 0));
}

InputResult touch_up(SDL_FingerID id, const MappedPoint& mapped) noexcept {
  auto* tracked = find_tracked_touch(id);
  if (tracked == nullptr) {
    return {};
  }
  const Rml::Vector2f position = mapped.valid ? mapped.position : tracked->position;
  const Rml::Vector2f delta = position - tracked->position;
  dispatch_touch_event(*tracked, TouchEndEvent, position, mapped.valid && mapped.inside, delta);
  const auto rmlPosition = tracked->rmlPosition;
  *tracked = {};
  return input_result(g_context->ProcessTouchEnd(touch_list(id, rmlPosition), 0));
}

void touch_cancel(TrackedTouch& tracked) noexcept {
  dispatch_touch_event(tracked, TouchCancelEvent, tracked.position, false);
  const auto id = tracked.id;
  const auto rmlPosition = tracked.rmlPosition;
  tracked = {};
  // RmlUi's touch cancel is an emulated mouse-up; leave first so it cannot click.
  g_context->ProcessMouseLeave();
  g_context->ProcessTouchCancel(touch_list(id, rmlPosition));
}

InputResult process_touch(const Pointer& pointer) noexcept {
  const auto id = static_cast<SDL_FingerID>(pointer.pointer);
  const MappedPoint mapped = map_window_point_to_content(pointer.position.x, pointer.position.y);
  switch (pointer.phase) {
  case Pointer::Phase::Down:
    return touch_down(id, mapped);
  case Pointer::Phase::Move:
    return touch_motion(id, mapped);
  case Pointer::Phase::Up:
    return touch_up(id, mapped);
  case Pointer::Phase::Cancel:
    if (auto* tracked = find_tracked_touch(id)) {
      touch_cancel(*tracked);
    }
    return {};
  }
  return {};
}

void cancel_input(const input::InputSource& source, const input::InputEvent::Cancelled& cancelled) noexcept {
  using Cancelled = input::InputEvent::Cancelled;
  switch (source.kind) {
  case input::InputSource::Kind::Mouse:
    cancel_mouse();
    break;
  case input::InputSource::Kind::Touch:
    for (auto& touch : s_trackedTouches) {
      if (!touch.active) {
        continue;
      }
      const auto* target = cancelled.target.get_if<Cancelled::Pointer>();
      if (target == nullptr || static_cast<SDL_FingerID>(target->pointer) == touch.id) {
        touch_cancel(touch);
      }
    }
    break;
  default:
    break;
  }
}
} // namespace

void initialize(const AuroraWindowSize& size) noexcept {
  const Rml::Vector2i dim = presentation_dimensions_from_window_size(size);
  if (!Backend::Initialize("Aurora RmlUi Backend", dim.x, dim.y, false)) {
    Log.error("Failed to initialize RmlUI Backend!");
    return;
  }

  g_fileInterface = new FileInterface_SDL();

  auto* renderInterface = get_render_interface();
  Rml::SetSystemInterface(Backend::GetSystemInterface());
  Rml::SetRenderInterface(renderInterface);
  Rml::SetFileInterface(g_fileInterface);

  renderInterface->SetWindowSize(dim);
  renderInterface->SetRenderTargetFormat(webgpu::g_graphicsConfig.surfaceConfiguration.format);
  renderInterface->CreateDeviceObjects();

  Rml::Initialise();
  register_image_effects();

  static GlassFilterInstancer s_glassInstancer;
  Rml::Factory::RegisterFilterInstancer("glass", &s_glassInstancer);

  g_context = Rml::CreateContext("main", dim);

  if (g_context) {
    sync_context_metrics(dim);
  } else {
    Log.error("Failed to initialize RmlUI Context!");
    Rml::Shutdown();
    Backend::Shutdown();
  }
}

Rml::Context* get_context() noexcept { return g_context; }

bool is_initialized() noexcept { return g_context != nullptr; }

void set_ui_scale(float scale) noexcept { s_uiScale = scale > 0.0f ? std::clamp(scale, 0.25f, 4.0f) : 0.0f; }

float get_ui_scale() noexcept { return s_uiScale; }

void set_glass_light_dir(float x, float y) noexcept {
  if (const float len = std::hypot(x, y); len > 1e-4f) {
    g_glassLightDir = {x / len, y / len};
  }
}

void set_input_type(InputType type) noexcept {
  auto* systemInterface = static_cast<SystemInterface_Aurora*>(Backend::GetSystemInterface());
  if (systemInterface != nullptr) {
    systemInterface->SetInputType(type);
  }
}

void handle_window_event(const SDL_Event& event) noexcept {
  if (g_context == nullptr) {
    return;
  }

  switch (event.type) {
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
  case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    sync_context_metrics(presentation_dimensions_from_window_size(window::get_window_size()));
    break;
  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    g_context->ProcessMouseLeave();
    break;
  default:
    break;
  }
}

InputResult process_input(const input::InputSource& source, const input::InputEvent& event) noexcept {
  if (g_context == nullptr) {
    return {};
  }
  using Event = input::InputEvent;
  return event.payload.match(
      [&](const Event::KeyChanged& key) -> InputResult {
        const auto identifier = RmlSDL::ConvertKey(static_cast<int>(key.keycode));
        const int modifiers = key_modifiers(key.modifiers);
        if (!key.pressed) {
          return input_result(g_context->ProcessKeyUp(identifier, modifiers));
        }
        bool stillPropagating = g_context->ProcessKeyDown(identifier, modifiers);
        if (key.keycode == SDLK_RETURN || key.keycode == SDLK_KP_ENTER) {
          stillPropagating &= g_context->ProcessTextInput('\n');
        }
        return input_result(stillPropagating);
      },
      [&](const Event::TextInput& text) -> InputResult {
        return input_result(g_context->ProcessTextInput(Rml::String(text.text)));
      },
      [&](const Event::PointerChanged& pointer) -> InputResult {
        if (source.kind == input::InputSource::Kind::Mouse) {
          return process_mouse(pointer);
        }
        if (source.kind == input::InputSource::Kind::Touch) {
          return process_touch(pointer);
        }
        return {};
      },
      [&](const Event::Scroll& scroll) -> InputResult { return process_scroll(scroll); },
      [&](const Event::Cancelled& cancelled) -> InputResult {
        cancel_input(source, cancelled);
        return {};
      },
      [](const auto&) -> InputResult { return {}; });
}

RecordedFrame record_frame(const webgpu::Viewport& presentViewport) noexcept {
  if (g_context == nullptr) {
    return {};
  }

  ZoneScoped;
  const Rml::Vector2i dim = dimensions_from_viewport(presentViewport);
  ensure_render_target(dim);
  if (!s_renderTarget.view) {
    return {};
  }

  sync_context_metrics(dim);
  g_context->Update();
  const bool needsBackdrop = context_has_visible_backdrop_filter(g_context);

  auto* renderInterface = get_render_interface();
  renderInterface->SetWindowSize(g_context->GetDimensions());
  renderInterface->BeginFrame(s_renderTarget, webgpu::present_source(),
                              needsBackdrop ? BaseLayerContent::Scene : BaseLayerContent::Transparent);

  Backend::BeginFrame();
  g_context->Render();
  Backend::PresentFrame();

  if (!renderInterface->EndFrame()) {
    // We didn't render anything
    return {};
  }
  return {
      .bindGroup = s_renderTargetCopyBindGroup,
      .overlay = !needsBackdrop,
  };
}

void shutdown() noexcept {
  if (g_context == nullptr) {
    return;
  }

  s_trackedTouches = {};
  Rml::Shutdown();
  Backend::Shutdown();
  g_context = nullptr;
  s_renderTarget = {};
  s_renderTargetCopyBindGroup = {};
}
} // namespace aurora::rmlui

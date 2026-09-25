#include "sdl_input.hpp"

#include "router.hpp"
#include "../window.hpp"
#ifdef AURORA_ENABLE_GX
#include "../imgui.hpp"
#endif

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_touch.h>

#include <algorithm>

namespace aurora::input::sdl {
namespace {

constexpr uint64_t kCursorIdleTimeoutNs = 2'000'000'000;

float normalize_axis(SDL_GamepadAxis axis, Sint16 value) noexcept {
  if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
    return std::clamp(static_cast<float>(value) / 32767.f, 0.f, 1.f);
  }
  return std::clamp(static_cast<float>(value) / 32767.f, -1.f, 1.f);
}

SDL_FPoint touch_to_window(float x, float y) noexcept {
  const auto size = window::get_window_size();
  return {x * static_cast<float>(size.width), y * static_cast<float>(size.height)};
}

InputEvent::PointerChanged::Phase finger_phase(Uint32 type) noexcept {
  switch (type) {
  case SDL_EVENT_FINGER_DOWN:
    return InputEvent::PointerChanged::Phase::Down;
  case SDL_EVENT_FINGER_UP:
    return InputEvent::PointerChanged::Phase::Up;
  case SDL_EVENT_FINGER_CANCELED:
    return InputEvent::PointerChanged::Phase::Cancel;
  default:
    return InputEvent::PointerChanged::Phase::Move;
  }
}

SourceState gamepad_state(SDL_JoystickID joystick) noexcept {
  SourceState state;
  SDL_Gamepad* gamepad = SDL_GetGamepadFromID(joystick);
  if (gamepad == nullptr) {
    return state;
  }
  for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
    const auto button = static_cast<SDL_GamepadButton>(i);
    state.set_button(button, SDL_GetGamepadButton(gamepad, button));
  }
  for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
    const auto axis = static_cast<SDL_GamepadAxis>(i);
    state.set_axis(axis, normalize_axis(axis, SDL_GetGamepadAxis(gamepad, axis)));
  }
  return state;
}

class Cursor {
public:
  void update(PointerMode mode, uint64_t nowNs) {
    m_mode = mode;
    apply(nowNs);
  }

  void activity(uint64_t timestampNs) {
    m_lastActivityNs = timestampNs;
    if (m_mode == PointerMode::AutoHide) {
      apply(timestampNs);
    }
  }

private:
  void apply(uint64_t nowNs) {
    SDL_Window* window = window::get_sdl_window();
    if (window == nullptr) {
      return;
    }
    const bool relative = m_mode == PointerMode::Relative;
    const bool shown = m_mode == PointerMode::Visible ||
                       (m_mode == PointerMode::AutoHide && nowNs < m_lastActivityNs + kCursorIdleTimeoutNs);
    if (relative != m_relative) {
      SDL_SetWindowRelativeMouseMode(window, relative);
      m_relative = relative;
    }
    if (shown != m_shown) {
      if (shown) {
        SDL_ShowCursor();
      } else {
        SDL_HideCursor();
      }
      m_shown = shown;
#ifdef AURORA_ENABLE_GX
      imgui::set_cursor_managed(!shown);
#endif
    }
  }

  PointerMode m_mode = PointerMode::Visible;
  uint64_t m_lastActivityNs = 0;
  bool m_relative = false;
  bool m_shown = true;
};

Cursor g_cursor;

} // namespace

bool is_routed_event(const SDL_Event& event) noexcept {
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
  case SDL_EVENT_TEXT_INPUT:
  case SDL_EVENT_TEXT_EDITING:
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_WHEEL:
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP:
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    return true;
  default:
    return false;
  }
}

void dispatch(const SDL_Event& event) noexcept {
  InputEvent input{.timestampNs = event.common.timestamp};

  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    input.source = keyboard_source();
    input.payload = InputEvent::KeyChanged{
        .scancode = event.key.scancode,
        .keycode = event.key.key,
        .modifiers = event.key.mod,
        .pressed = event.key.down,
        .repeat = event.key.repeat,
    };
    break;
  case SDL_EVENT_TEXT_INPUT:
    input.source = keyboard_source();
    input.payload = InputEvent::TextInput{.text = event.text.text != nullptr ? event.text.text : ""};
    break;
  case SDL_EVENT_TEXT_EDITING:
    input.source = keyboard_source();
    input.payload = InputEvent::TextEditing{
        .text = event.edit.text != nullptr ? event.edit.text : "",
        .start = event.edit.start,
        .length = event.edit.length,
    };
    break;
  case SDL_EVENT_MOUSE_MOTION:
    if (event.motion.which == SDL_TOUCH_MOUSEID) {
      return;
    }
    g_cursor.activity(event.common.timestamp);
    input.source = mouse_source();
    input.payload = InputEvent::PointerChanged{
        .phase = InputEvent::PointerChanged::Phase::Move,
        .position = {event.motion.x, event.motion.y},
        .delta = {event.motion.xrel, event.motion.yrel},
        .modifiers = SDL_GetModState(),
    };
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.which == SDL_TOUCH_MOUSEID) {
      return;
    }
    g_cursor.activity(event.common.timestamp);
    input.source = mouse_source();
    input.payload = InputEvent::PointerChanged{
        .phase = event.button.down ? InputEvent::PointerChanged::Phase::Down : InputEvent::PointerChanged::Phase::Up,
        .position = {event.button.x, event.button.y},
        .button = event.button.button,
        .modifiers = SDL_GetModState(),
    };
    break;
  case SDL_EVENT_MOUSE_WHEEL: {
    if (event.wheel.which == SDL_TOUCH_MOUSEID) {
      return;
    }
    g_cursor.activity(event.common.timestamp);
    input.source = mouse_source();
    const float flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
    input.payload = InputEvent::Scroll{
        .position = {event.wheel.mouse_x, event.wheel.mouse_y},
        // SDL's positive y is away from the user.
        .delta = {event.wheel.x * flip, -event.wheel.y * flip},
        .modifiers = SDL_GetModState(),
    };
    break;
  }
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    input.source = touch_source();
    input.payload = InputEvent::PointerChanged{
        .pointer = event.tfinger.fingerID,
        .phase = finger_phase(event.type),
        .position = touch_to_window(event.tfinger.x, event.tfinger.y),
        .delta = touch_to_window(event.tfinger.dx, event.tfinger.dy),
        .pressure = event.tfinger.pressure,
    };
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP:
    input.source.id = source_for_gamepad(event.gbutton.which);
    input.payload = InputEvent::ButtonChanged{
        .button = static_cast<SDL_GamepadButton>(event.gbutton.button),
        .pressed = event.gbutton.down,
    };
    break;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
    input.source.id = source_for_gamepad(event.gaxis.which);
    const auto axis = static_cast<SDL_GamepadAxis>(event.gaxis.axis);
    input.payload = InputEvent::AxisChanged{
        .axis = axis,
        .value = normalize_axis(axis, event.gaxis.value),
    };
    break;
  }
  default:
    return;
  }

  detail::dispatch(std::move(input), &event);
}

void gamepad_added(SDL_JoystickID joystick) noexcept {
  const char* name = SDL_GetGamepadNameForID(joystick);
  detail::add_gamepad(joystick, name != nullptr ? name : "Controller", gamepad_state(joystick));
}

void gamepad_removed(SDL_JoystickID joystick) noexcept { detail::remove_gamepad(joystick); }

void gamepad_remapped(SDL_JoystickID joystick) noexcept { detail::remap_gamepad(joystick); }

void update_cursor() noexcept { g_cursor.update(pointer_mode(), SDL_GetTicksNS()); }

} // namespace aurora::input::sdl

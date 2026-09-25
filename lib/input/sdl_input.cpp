#include "sdl_input.hpp"

#include "router.hpp"
#include "../window.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_touch.h>

#include <algorithm>

namespace aurora::input::sdl {
namespace {

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

void seed_gamepad(const InputSource& source, SDL_JoystickID joystick) noexcept {
  SDL_Gamepad* gamepad = SDL_GetGamepadFromID(joystick);
  if (gamepad == nullptr) {
    return;
  }
  for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
    detail::seed_button(source.id, static_cast<SDL_GamepadButton>(button),
                        SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button)));
  }
  for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
    const auto gamepadAxis = static_cast<SDL_GamepadAxis>(axis);
    detail::seed_axis(source.id, gamepadAxis, normalize_axis(gamepadAxis, SDL_GetGamepadAxis(gamepad, gamepadAxis)));
  }
}

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
  InputSource source;

  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    source = keyboard_source();
    input.window = event.key.windowID;
    input.payload = InputEvent::KeyChanged{
        .scancode = event.key.scancode,
        .keycode = event.key.key,
        .modifiers = event.key.mod,
        .pressed = event.key.down,
        .repeat = event.key.repeat,
    };
    break;
  case SDL_EVENT_TEXT_INPUT:
    source = keyboard_source();
    input.window = event.text.windowID;
    input.payload = InputEvent::TextInput{.text = event.text.text != nullptr ? event.text.text : ""};
    break;
  case SDL_EVENT_TEXT_EDITING:
    source = keyboard_source();
    input.window = event.edit.windowID;
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
    source = mouse_source();
    input.window = event.motion.windowID;
    input.payload = InputEvent::PointerChanged{
        .pointer = 0,
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
    source = mouse_source();
    input.window = event.button.windowID;
    input.payload = InputEvent::PointerChanged{
        .pointer = 0,
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
    source = mouse_source();
    input.window = event.wheel.windowID;
    const float flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
    input.payload = InputEvent::Scroll{
        .position = {event.wheel.mouse_x, event.wheel.mouse_y},
        // SDL reports positive y away from the user; router scroll is positive down.
        .delta = {event.wheel.x * flip, -event.wheel.y * flip},
        .modifiers = SDL_GetModState(),
    };
    break;
  }
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    source = touch_source();
    input.window = event.tfinger.windowID;
    input.payload = InputEvent::PointerChanged{
        .pointer = event.tfinger.fingerID,
        .phase = finger_phase(event.type),
        .position = touch_to_window(event.tfinger.x, event.tfinger.y),
        .delta = touch_to_window(event.tfinger.dx, event.tfinger.dy),
        .pressure = event.tfinger.pressure,
    };
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP: {
    const auto found = find_source(source_for_gamepad(event.gbutton.which));
    if (!found) {
      return;
    }
    source = *found;
    input.payload = InputEvent::ButtonChanged{
        .button = static_cast<SDL_GamepadButton>(event.gbutton.button),
        .pressed = event.gbutton.down,
    };
    break;
  }
  case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
    const auto found = find_source(source_for_gamepad(event.gaxis.which));
    if (!found) {
      return;
    }
    source = *found;
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

  detail::dispatch(source, std::move(input), &event);
}

void gamepad_added(SDL_JoystickID joystick) noexcept {
  const char* name = SDL_GetGamepadNameForID(joystick);
  const auto source = detail::add_gamepad(joystick, name != nullptr ? name : "Controller");
  seed_gamepad(source, joystick);
}

void gamepad_removed(SDL_JoystickID joystick) noexcept { detail::remove_gamepad(joystick); }

} // namespace aurora::input::sdl

#include "source_state.hpp"

namespace aurora::input {

void SourceState::apply(const InputEvent& event) {
  using Phase = InputEvent::PointerChanged::Phase;
  event.payload.match(
      [&](const InputEvent::KeyChanged& key) {
        if (!key.repeat) {
          set_key(key.scancode, key.pressed);
        }
      },
      [&](const InputEvent::ButtonChanged& button) { set_button(button.button, button.pressed); },
      [&](const InputEvent::AxisChanged& axis) { set_axis(axis.axis, axis.value); },
      [&](const InputEvent::PointerChanged& pointer) {
        if (event.source.kind == InputSource::Kind::Mouse &&
            (pointer.phase == Phase::Down || pointer.phase == Phase::Up)) {
          set_mouse_button(pointer.button, pointer.phase == Phase::Down);
        }
      },
      [&](const InputEvent::Cancelled&) { *this = {}; }, [](const auto&) {});
}

void SourceState::set_key(SDL_Scancode scancode, bool pressed) {
  if (valid(scancode)) {
    m_keys[scancode] = pressed;
  }
}

void SourceState::set_mouse_button(uint8_t button, bool pressed) {
  if (pressed) {
    m_mouseButtons |= mouse_button_mask(button);
  } else {
    m_mouseButtons &= ~mouse_button_mask(button);
  }
}

void SourceState::set_button(SDL_GamepadButton button, bool pressed) {
  if (valid(button)) {
    m_buttons[button] = pressed;
  }
}

void SourceState::set_axis(SDL_GamepadAxis axis, float value) {
  if (valid(axis)) {
    m_axes[axis] = value;
  }
}

} // namespace aurora::input

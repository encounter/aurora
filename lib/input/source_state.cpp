#include "source_state.hpp"

namespace aurora::input {

void SourceState::apply(InputSource::Kind kind, const InputEvent& event) {
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
        if (kind == InputSource::Kind::Mouse && (pointer.phase == Phase::Down || pointer.phase == Phase::Up)) {
          set_mouse_button(pointer.button, pointer.phase == Phase::Down);
        }
      },
      [&](const InputEvent::Cancelled& cancelled) { cancel(cancelled); }, [](const auto&) {});
}

void SourceState::cancel(const InputEvent::Cancelled& cancelled) {
  using Cancelled = InputEvent::Cancelled;
  cancelled.target.match([&](const Cancelled::All&) { *this = {}; },
                         [&](const Cancelled::Key& key) { set_key(key.scancode, false); },
                         [&](const Cancelled::Button& button) { set_button(button.button, false); },
                         [&](const Cancelled::Axis& axis) { set_axis(axis.axis, 0.f); },
                         [&](const Cancelled::Pointer&) { m_mouseButtons = 0; });
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

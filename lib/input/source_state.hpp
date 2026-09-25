#pragma once

#include <aurora/input.hpp>

#include <array>
#include <bitset>

namespace aurora::input {

constexpr bool valid(SDL_Scancode scancode) { return scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_SCANCODE_COUNT; }
constexpr bool valid(SDL_GamepadButton button) {
  return button > SDL_GAMEPAD_BUTTON_INVALID && button < SDL_GAMEPAD_BUTTON_COUNT;
}
constexpr bool valid(SDL_GamepadAxis axis) { return axis > SDL_GAMEPAD_AXIS_INVALID && axis < SDL_GAMEPAD_AXIS_COUNT; }

// SDL_BUTTON_MASK, with zero for invalid buttons.
constexpr uint32_t mouse_button_mask(uint8_t button) {
  return button >= 1 && button <= 32 ? SDL_BUTTON_MASK(button) : 0u;
}

// Held keys, buttons, and axis values of one source. The router keeps the raw
// view of each source; each binding::State keeps the view of input admitted to it.
class SourceState {
public:
  // Applies presses, releases, axis samples, and cancellations. Key repeats and
  // payloads without held state (motion, scroll, text, touch) change nothing.
  void apply(InputSource::Kind kind, const InputEvent& event);
  void cancel(const InputEvent::Cancelled& cancelled);

  [[nodiscard]] bool key(SDL_Scancode scancode) const { return valid(scancode) && m_keys.test(scancode); }
  [[nodiscard]] bool mouse_button(uint8_t button) const { return (m_mouseButtons & mouse_button_mask(button)) != 0; }
  [[nodiscard]] bool button(SDL_GamepadButton button) const { return valid(button) && m_buttons.test(button); }
  [[nodiscard]] float axis(SDL_GamepadAxis axis) const { return valid(axis) ? m_axes[axis] : 0.f; }

  void set_key(SDL_Scancode scancode, bool pressed);
  void set_mouse_button(uint8_t button, bool pressed);
  void set_button(SDL_GamepadButton button, bool pressed);
  void set_axis(SDL_GamepadAxis axis, float value);

private:
  std::bitset<SDL_SCANCODE_COUNT> m_keys;
  uint32_t m_mouseButtons = 0;
  std::bitset<SDL_GAMEPAD_BUTTON_COUNT> m_buttons;
  std::array<float, SDL_GAMEPAD_AXIS_COUNT> m_axes{};
};

} // namespace aurora::input

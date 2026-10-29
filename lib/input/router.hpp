#pragma once

#include "source_state.hpp"

#include <aurora/input.hpp>

union SDL_Event;

namespace aurora::input::detail {

// `initial` is the controller's state when it connected, so held sticks resample.
InputSource add_gamepad(SDL_JoystickID joystick, std::string_view name, const SourceState& initial);
void remove_gamepad(SDL_JoystickID joystick);
void remap_gamepad(SDL_JoystickID joystick);

void dispatch(InputEvent event, const SDL_Event* raw = nullptr);
const SDL_Event* current_sdl_event();

// Text input backend for tests.
struct TextInputBackend {
  virtual ~TextInputBackend() = default;
  virtual bool start(const TextInputOptions& options) = 0;
  virtual void stop() = 0;
  virtual void clear_composition() = 0;
  virtual void set_area(const std::optional<TextInputArea>& area) = 0;
};
void set_text_input_backend(TextInputBackend* backend);
void text_window_destroyed(); // Invalidates requests before the SDL window dies.
void focus_gained();

// Cancels keyboard, mouse and touch in every layer; suspends text requests.
void focus_lost();

// Drops every layer and source. Tests only.
void reset();

} // namespace aurora::input::detail

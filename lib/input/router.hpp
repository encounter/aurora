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

// Cancels keyboard, mouse and touch in every layer.
void focus_lost();

// Drops every layer and source. Tests only.
void reset();

} // namespace aurora::input::detail

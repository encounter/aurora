#pragma once

#include <SDL3/SDL_events.h>

// Normalizes primary-window SDL input into router events.
namespace aurora::input::sdl {

// Key, text, mouse, touch, and gamepad button/axis events. These are routed
// instead of being fed to ImGui/RmlUi directly.
bool is_routed_event(const SDL_Event& event) noexcept;

// Routes one event for which is_routed_event() returned true.
void dispatch(const SDL_Event& event) noexcept;

// Controller lifecycle, called after aurora::gamepad has opened/closed the device.
void gamepad_added(SDL_JoystickID joystick) noexcept;
void gamepad_removed(SDL_JoystickID joystick) noexcept;

} // namespace aurora::input::sdl

#pragma once

#include <SDL3/SDL_events.h>

namespace aurora::input::sdl {

bool is_routed_event(const SDL_Event& event) noexcept;
void dispatch(const SDL_Event& event) noexcept;

void gamepad_added(SDL_JoystickID joystick) noexcept;
void gamepad_removed(SDL_JoystickID joystick) noexcept;
void gamepad_remapped(SDL_JoystickID joystick) noexcept;

void update_cursor() noexcept;

} // namespace aurora::input::sdl

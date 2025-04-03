#pragma once

#include <string>

#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_mouse.h"

namespace aurora::input {
Sint32 get_instance_for_player(uint32_t player) noexcept;
SDL_JoystickID add_controller(SDL_JoystickID which) noexcept;
void remove_controller(Uint32 instance) noexcept;
Sint32 player_index(Uint32 instance) noexcept;
void set_player_index(Uint32 instance, Sint32 index) noexcept;
std::string controller_name(Uint32 instance) noexcept;
bool is_gamecube(Uint32 instance) noexcept;
bool controller_has_rumble(Uint32 instance) noexcept;
void controller_rumble(uint32_t instance, uint16_t low_freq_intensity, uint16_t high_freq_intensity,
                       uint16_t duration_ms) noexcept;
uint32_t controller_count() noexcept;
void initialize() noexcept;
} // namespace aurora::input

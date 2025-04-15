#pragma once

#include <string>
#include "dolphin/pad.h" // For PADDeaZones and PADButtonMapping
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_mouse.h"
#include "logging.hpp"

#include <absl/container/flat_hash_map.h>

namespace aurora::input {
extern Module Log;

struct GameController {
  SDL_Gamepad* m_controller = nullptr;
  bool m_isGameCube = false;
  Sint32 m_index = -1;
  bool m_hasRumble = false;
  PADDeadZones m_deadZones{
      .emulateTriggers = true,
      .useDeadzones = true,
      .stickDeadZone = 8000,
      .substickDeadZone = 8000,
      .leftTriggerActivationZone = 31150,
      .rightTriggerActivationZone = 31150,
  };
  uint16_t m_vid = 0;
  uint16_t m_pid = 0;
  std::array<PADButtonMapping, 12> m_mapping{};
  bool m_mappingLoaded = false;
  constexpr bool operator==(const GameController& other) const {
    return m_controller == other.m_controller && m_index == other.m_index;
  }
};

GameController* get_controller_for_player(uint32_t player) noexcept;
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
extern absl::flat_hash_map<Uint32, GameController> g_GameControllers;
} // namespace aurora::input

#include "input.hpp"
#include "internal.hpp"

#include "magic_enum.hpp"

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL.h>

#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <cmath>

using namespace std::string_view_literals;

namespace aurora::input {
Module Log("aurora::input");
absl::flat_hash_map<Uint32, GameController> g_GameControllers;

GameController* get_controller_for_player(uint32_t player) noexcept {
  for (auto& [which, controller] : g_GameControllers) {
    if (player_index(which) == player) {
      return &controller;
    }
  }

#if 0
  /* If we don't have a controller assigned to this port use the first unassigned controller */
  if (!g_GameControllers.empty()) {
    int32_t availIndex = -1;
    GameController* ct = nullptr;
    for (auto& controller : g_GameControllers) {
      if (player_index(controller.first) == -1) {
        availIndex = controller.first;
        ct = &controller.second;
        break;
      }
    }
    if (availIndex != -1) {
      set_player_index(availIndex, player);
      return ct;
    }
  }
#endif
  return nullptr;
}

Sint32 get_instance_for_player(uint32_t player) noexcept {
  for (const auto& [which, controller] : g_GameControllers) {
    if (player_index(which) == player) {
      return which;
    }
  }

  return {};
}

SDL_JoystickID add_controller(SDL_JoystickID which) noexcept {
  auto* ctrl = SDL_OpenGamepad(which);
  if (ctrl != nullptr) {
    GameController controller;
    controller.m_controller = ctrl;
    controller.m_index = which;
    controller.m_vid = SDL_GetGamepadVendor(ctrl);
    controller.m_pid = SDL_GetGamepadProduct(ctrl);
    if (controller.m_vid == 0x05ac /* USB_VENDOR_APPLE */ && controller.m_pid == 3) {
      // Ignore Apple TV remote
      SDL_CloseGamepad(ctrl);
      return -1;
    }
    controller.m_isGameCube = SDL_GetGamepadType(ctrl) == SDL_GAMEPAD_TYPE_GAMECUBE;
    const auto props = SDL_GetGamepadProperties(ctrl);
    controller.m_hasRumble = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, true);
    controller.m_hasRgbLed = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);
    SDL_JoystickID instance = SDL_GetJoystickID(SDL_GetGamepadJoystick(ctrl));
    g_GameControllers[instance] = controller;
    return instance;
  }

  return -1;
}

void remove_controller(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    SDL_CloseGamepad(it->second.m_controller);
    g_GameControllers.erase(it);
  }
}

bool is_gamecube(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    return it->second.m_isGameCube;
  }
  return false;
}

int32_t player_index(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    return SDL_GetGamepadPlayerIndex(it->second.m_controller);
  }
  return -1;
}

void set_player_index(Uint32 instance, Sint32 index) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    SDL_SetGamepadPlayerIndex(it->second.m_controller, index);
  }
}

std::string controller_name(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    const auto* name = SDL_GetGamepadName(it->second.m_controller);
    if (name != nullptr) {
      return {name};
    }
  }
  return {};
}

bool controller_has_rumble(Uint32 instance) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    return it->second.m_hasRumble;
  }
  return false;
}

void controller_rumble(uint32_t instance, uint16_t low_freq_intensity, uint16_t high_freq_intensity,
                       uint16_t duration_ms) noexcept {
  if (auto it = g_GameControllers.find(instance); it != g_GameControllers.end()) {
    SDL_RumbleGamepad(it->second.m_controller, low_freq_intensity, high_freq_intensity, duration_ms);
  }
}

uint32_t controller_count() noexcept { return g_GameControllers.size(); }

void initialize() noexcept {
  /* Make sure we initialize everything input related now, this will automatically add all of the connected controllers
   * as expected */
  ASSERT(SDL_Init(SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD), "Failed to initialize SDL subsystems: {}",
         SDL_GetError());
}

struct MouseScrollStatus {
  float scrollX;
  float scrollY;
};

static MouseScrollStatus g_MouseStatus;

void set_mouse_scroll(const float scrollX, const float scrollY) noexcept {
  g_MouseStatus.scrollX = scrollX;
  g_MouseStatus.scrollY = scrollY;
}

void get_mouse_scroll(float* scrollX, float* scrollY) noexcept {
  *scrollX = g_MouseStatus.scrollX;
  *scrollY = g_MouseStatus.scrollY;
}

void shutdown() noexcept {
  // Upon shutdown we want to ensure all controllers are in a default state, so force all rumble supporting controllers
  // to shut off their rumble motors. 
  for (const auto& controller : g_GameControllers) {
    if (!controller.second.m_hasRumble) {
      continue;
    }
    controller_rumble(controller.first, 0, 0, 0);
  }
}
} // namespace aurora::input

#include "input.hpp"
#include "internal.hpp"

#include "magic_enum.hpp"

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL.h>

#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <absl/strings/str_split.h>
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

static std::optional<std::string> remap_controller_layout(std::string mapping) {
  std::string newMapping;
  newMapping.reserve(mapping.size());
  absl::btree_map<absl::string_view, absl::string_view> entries;
  for (size_t idx = 0; const auto value : absl::StrSplit(mapping, ',')) {
    if (idx < 2) {
      if (idx > 0) {
        newMapping.push_back(',');
      }
      newMapping.append(value);
    } else {
      const auto split = absl::StrSplit(value, absl::MaxSplits(':', 2));
      auto iter = split.begin();
      auto first = *iter++;
      auto second = *iter;
      entries.emplace(std::move(first), std::move(second));
    }
    idx++;
  }
  if (entries.contains("rightshoulder") && !entries.contains("leftshoulder")) {
    Log.info("Remapping GameCube controller layout");
    entries.insert_or_assign("back", entries["rightshoulder"]);
    // TODO trigger buttons may differ per platform
    entries.insert_or_assign("leftshoulder", "b11");
    entries.insert_or_assign("rightshoulder", "b10");
  } else if (entries.contains("leftshoulder") && entries.contains("rightshoulder") && entries.contains("back")) {
    Log.info("Controller has standard layout");
#if 0
    auto a = entries["a"sv];
    entries.insert_or_assign("a"sv, entries["b"sv]);
    entries.insert_or_assign("b"sv, a);
#endif
    auto x = entries["x"];
    entries.insert_or_assign("x", entries["y"]);
    entries.insert_or_assign("y", x);
  } else {
    Log.error("Controller has unsupported layout: {}", mapping);
    return {};
  }
  for (auto [k, v] : entries) {
    newMapping.push_back(',');
    newMapping.append(k);
    newMapping.push_back(':');
    newMapping.append(v);
  }
  return newMapping;
}

SDL_JoystickID add_controller(SDL_JoystickID which) noexcept {
  auto* ctrl = SDL_OpenGamepad(which);
  if (ctrl != nullptr) {
    {
      char* mapping = SDL_GetGamepadMapping(ctrl);
      if (mapping != nullptr) {
        auto newMapping = remap_controller_layout(mapping);
        SDL_free(mapping);
        if (newMapping) {
          if (SDL_AddGamepadMapping(newMapping->c_str()) == -1) {
            Log.error("Failed to update controller mapping: {}", SDL_GetError());
          }
        }
      } else {
        Log.error("Failed to retrieve mapping for controller");
      }
    }
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
    controller.m_isGameCube = controller.m_vid == 0x057E && controller.m_pid == 0x0337;
    const auto props = SDL_GetGamepadProperties(ctrl);
    controller.m_hasRumble = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, true);
    SDL_JoystickID instance = SDL_GetJoystickID(SDL_GetGamepadJoystick(ctrl));
    g_GameControllers[instance] = controller;
    return instance;
  }

  return -1;
}

void remove_controller(Uint32 instance) noexcept {
  if (g_GameControllers.find(instance) != g_GameControllers.end()) {
    SDL_CloseGamepad(g_GameControllers[instance].m_controller);
    g_GameControllers.erase(instance);
  }
}

bool is_gamecube(Uint32 instance) noexcept {
  if (g_GameControllers.find(instance) != g_GameControllers.end()) {
    return g_GameControllers[instance].m_isGameCube;
  }
  return false;
}

int32_t player_index(Uint32 instance) noexcept {
  if (g_GameControllers.find(instance) != g_GameControllers.end()) {
    return SDL_GetGamepadPlayerIndex(g_GameControllers[instance].m_controller);
  }
  return -1;
}

void set_player_index(Uint32 instance, Sint32 index) noexcept {
  if (g_GameControllers.find(instance) != g_GameControllers.end()) {
    SDL_SetGamepadPlayerIndex(g_GameControllers[instance].m_controller, index);
  }
}

std::string controller_name(Uint32 instance) noexcept {
  if (g_GameControllers.find(instance) != g_GameControllers.end()) {
    const auto* name = SDL_GetGamepadName(g_GameControllers[instance].m_controller);
    if (name != nullptr) {
      return {name};
    }
  }
  return {};
}

bool controller_has_rumble(Uint32 instance) noexcept {
  if (g_GameControllers.find(instance) != g_GameControllers.end()) {
    return g_GameControllers[instance].m_hasRumble;
  }

  return false;
}

void controller_rumble(uint32_t instance, uint16_t low_freq_intensity, uint16_t high_freq_intensity,
                       uint16_t duration_ms) noexcept {

  if (g_GameControllers.find(instance) != g_GameControllers.end()) {
    SDL_RumbleGamepad(g_GameControllers[instance].m_controller, low_freq_intensity, high_freq_intensity, duration_ms);
  }
}

uint32_t controller_count() noexcept { return g_GameControllers.size(); }

void initialize() noexcept {
  /* Make sure we initialize everything input related now, this will automatically add all of the connected controllers
   * as expected */
  ASSERT(SDL_Init(SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD), "Failed to initialize SDL subsystems: {}",
         SDL_GetError());
}
} // namespace aurora::input

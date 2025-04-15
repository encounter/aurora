#include <dolphin/si.h>
#include "../../input.hpp"

#include <SDL3/SDL_power.h>

uint32_t SIProbe(int32_t chan) {
  auto* const controller = aurora::input::get_controller_for_player(chan);
  if (controller == nullptr) {
    return SI_ERROR_NO_RESPONSE;
  }

  if (controller->m_isGameCube) {
    auto level = SDL_GetJoystickPowerInfo(SDL_GetGamepadJoystick(controller->m_controller), nullptr);
    if (level == SDL_POWERSTATE_UNKNOWN) {
      return SI_GC_WAVEBIRD;
    }
  }

  return SI_GC_CONTROLLER;
}

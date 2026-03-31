#include "../../input.hpp"
#include "../../internal.hpp"
#include <dolphin/pad.h>
#include <dolphin/si.h>

#include <array>

static constexpr std::array<PADButtonMapping, 12> g_defaultButtons{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_BACK, PAD_TRIGGER_Z},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_TRIGGER_L},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

static constexpr std::array<PADAxisMapping, 12> g_defaultAxes{{
    {{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_POS},
    {{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_NEG},
    // SDL's gamepad y-axis is inverted from GC's
    {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_POS},
    {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_NEG},
    {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_POS},
    {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_NEG},
    // see above
    {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_NEGATIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_POS},
    {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_NEG},
    {{SDL_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_L},
    {{SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_R},
}};

void PADSetSpec(u32 spec) {}
BOOL PADInit() {
  return true;
}
BOOL PADRecalibrate(u32 mask) { return true; }
BOOL PADReset(u32 mask) { return true; }
void PADSetAnalogMode(u32 mode) {}

aurora::input::GameController* __PADGetControllerForIndex(uint32_t idx) {
  if (idx >= aurora::input::g_GameControllers.size()) {
    return nullptr;
  }

  uint32_t tmp = 0;
  auto iter = aurora::input::g_GameControllers.begin();
  while (tmp < idx) {
    ++iter;
    ++tmp;
  }
  if (iter == aurora::input::g_GameControllers.end()) {
    return nullptr;
  }

  return &iter->second;
}

uint32_t PADCount() { return aurora::input::g_GameControllers.size(); }

const char* PADGetNameForControllerIndex(uint32_t idx) {
  const auto* ctrl = __PADGetControllerForIndex(idx);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return SDL_GetGamepadName(ctrl->m_controller);
}

void PADSetPortForIndex(uint32_t idx, int32_t port) {
  auto* ctrl = __PADGetControllerForIndex(idx);
  auto* dest = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return;
  }
  if (dest != nullptr) {
    SDL_SetGamepadPlayerIndex(dest->m_controller, -1);
  }
  SDL_SetGamepadPlayerIndex(ctrl->m_controller, port);
}

int32_t PADGetIndexForPort(uint32_t port) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return -1;
  }
  int32_t index = 0;
  for (auto iter = aurora::input::g_GameControllers.begin(); iter != aurora::input::g_GameControllers.end();
       ++iter, ++index) {
    if (&iter->second == ctrl) {
      break;
    }
  }

  return index;
}

void PADClearPort(uint32_t port) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return;
  }
  SDL_SetGamepadPlayerIndex(ctrl->m_controller, -1);
}

void __PADLoadMapping(aurora::input::GameController* controller) {
  int32_t playerIndex = SDL_GetGamepadPlayerIndex(controller->m_controller);
  if (playerIndex == -1) {
    return;
  }

  std::string basePath{aurora::g_config.configPath};
  if (!controller->m_mappingLoaded) {
    controller->m_buttonMapping = g_defaultButtons;
    controller->m_axisMapping = g_defaultAxes;
  }

  controller->m_mappingLoaded = true;

  auto path = fmt::format("{}/{}_{:04X}_{:04X}.controller", basePath, PADGetName(playerIndex),
                          controller->m_vid, controller->m_pid);
  FILE* file = fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return;
  }

  uint32_t magic = 0;
  fread(&magic, 1, sizeof(uint32_t), file);
  if (magic != SBIG('CTRL')) {
    aurora::input::Log.warn("Invalid controller mapping magic!");
    return;
  }

  uint32_t version = 0;
  fread(&version, 1, sizeof(uint32_t), file);
  if (version != 1) {
    aurora::input::Log.warn("Invalid controller mapping version!");
    return;
  }

  bool isGameCube = false;
  fread(&isGameCube, 1, 1, file);
  fseek(file, (ftell(file) + 31) & ~31, SEEK_SET);
  uint32_t dataStart = ftell(file);
  if (isGameCube) {
    fseek(file, dataStart + ((sizeof(PADDeadZones) + sizeof(PADButtonMapping)) * playerIndex), SEEK_SET);
  }

  fread(&controller->m_deadZones, 1, sizeof(PADDeadZones), file);
  fread(&controller->m_buttonMapping, 1, sizeof(PADButtonMapping) * controller->m_buttonMapping.size(), file);
  fread(&controller->m_axisMapping, 1, sizeof(PADAxisMapping) * controller->m_axisMapping.size(), file);
  fclose(file);
}

static Sint16 _get_axis_value(aurora::input::GameController* controller, PADAxis axis) {
  auto iter = std::find_if(controller->m_axisMapping.cbegin(), controller->m_axisMapping.cend(),
                           [axis](const auto& pair) { return pair.padAxis == axis; });
  if (iter == controller->m_axisMapping.end()) {
    return 0;
  }

  if (iter->nativeAxis.nativeAxis != -1) {
    auto nativeAxis = iter->nativeAxis;
    // clamp value to avoid overflow when casting to Sint16 if -32768 is negated
    return static_cast<Sint16>(std::min(
        SDL_GetGamepadAxis(controller->m_controller, static_cast<SDL_GamepadAxis>(nativeAxis.nativeAxis)) * nativeAxis.sign,
        SDL_JOYSTICK_AXIS_MAX
    ));
  } else {
    assert(iter->nativeButton != -1);
    if (SDL_GetGamepadButton(controller->m_controller, static_cast<SDL_GamepadButton>(iter->nativeButton))) {
      return SDL_JOYSTICK_AXIS_MAX;
    } else {
      return 0;
    }
  }
}

bool gBlockPAD = false;
uint32_t PADRead(PADStatus* status) {
  if (gBlockPAD) {
    return 0;
  }

  uint32_t rumbleSupport = 0;
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    memset(&status[i], 0, sizeof(PADStatus));
    auto controller = aurora::input::get_controller_for_player(i);
    if (controller == nullptr) {
      status[i].err = PAD_ERR_NO_CONTROLLER;
      continue;
    }

    if (!controller->m_mappingLoaded) {
      __PADLoadMapping(controller);
    }
    status[i].err = PAD_ERR_NONE;
    std::for_each(
        controller->m_buttonMapping.begin(), controller->m_buttonMapping.end(), [&controller, &i, &status](const auto& mapping) {
          if (SDL_GetGamepadButton(controller->m_controller, static_cast<SDL_GamepadButton>(mapping.nativeButton))) {
            status[i].button |= mapping.padButton;
          }
        });

    Sint16 xlPos = _get_axis_value(controller, PAD_AXIS_LEFT_X_POS);
    Sint16 xlNeg = _get_axis_value(controller, PAD_AXIS_LEFT_X_NEG);
    Sint16 ylPos = _get_axis_value(controller, PAD_AXIS_LEFT_Y_POS);
    Sint16 ylNeg = _get_axis_value(controller, PAD_AXIS_LEFT_Y_NEG);

    Sint16 xl = (xlPos + -xlNeg) / 2;
    // SDL's gamepad y-axis is inverted from GC's
    Sint16 yl = (-ylPos + ylNeg) / 2;
    if (controller->m_deadZones.useDeadzones) {
      if (std::abs(xl) > controller->m_deadZones.stickDeadZone) {
        xl /= 256;
      } else {
        xl = 0;
      }
      if (std::abs(yl) > controller->m_deadZones.stickDeadZone) {
        yl = (-(yl + 1u)) / 256u;
      } else {
        yl = 0;
      }
    } else {
      xl /= 256;
      yl = (-(yl + 1u)) / 256u;
    }

    status[i].stickX = static_cast<int8_t>(xl);
    status[i].stickY = static_cast<int8_t>(yl);

    Sint16 xrPos = _get_axis_value(controller, PAD_AXIS_RIGHT_X_POS);
    Sint16 xrNeg = _get_axis_value(controller, PAD_AXIS_RIGHT_X_NEG);
    Sint16 yrPos = _get_axis_value(controller, PAD_AXIS_RIGHT_Y_POS);
    Sint16 yrNeg = _get_axis_value(controller, PAD_AXIS_RIGHT_Y_NEG);

    Sint16 xr = (xrPos + -xrNeg) / 2;
    // SDL's gamepad y-axis is inverted from GC's
    Sint16 yr = (-yrPos + yrNeg) / 2;
    if (controller->m_deadZones.useDeadzones) {
      if (std::abs(xr) > controller->m_deadZones.substickDeadZone) {
        xr /= 256;
      } else {
        xr = 0;
      }

      if (std::abs(yr) > controller->m_deadZones.substickDeadZone) {
        yr = (-(yr + 1u)) / 256u;
      } else {
        yr = 0;
      }
    } else {
      xr /= 256;
      yr = (-(yr + 1u)) / 256u;
    }

    status[i].substickX = static_cast<int8_t>(xr);
    status[i].substickY = static_cast<int8_t>(yr);

    xl = std::max((Sint16)0, _get_axis_value(controller, PAD_AXIS_TRIGGER_L));
    xr = std::max((Sint16)0, _get_axis_value(controller, PAD_AXIS_TRIGGER_R));
    if (/*!controller->m_isGameCube && */ controller->m_deadZones.emulateTriggers) {
      if (xr > controller->m_deadZones.leftTriggerActivationZone) {
        status[i].button |= PAD_TRIGGER_L;
      }
      if (yr > controller->m_deadZones.rightTriggerActivationZone) {
        status[i].button |= PAD_TRIGGER_R;
      }
    }
    xr /= 128;
    yr /= 128;

    status[i].triggerLeft = static_cast<int8_t>(xr);
    status[i].triggerRight = static_cast<int8_t>(yr);

    if (controller->m_hasRumble) {
      rumbleSupport |= PAD_CHAN0_BIT >> i;
    }
  }
  return rumbleSupport;
}

void PADControlMotor(int32_t chan, uint32_t command) {
  auto controller = aurora::input::get_controller_for_player(chan);
  auto instance = aurora::input::get_instance_for_player(chan);
  if (controller == nullptr) {
    return;
  }

  if (controller->m_isGameCube) {
    if (command == PAD_MOTOR_STOP) {
      aurora::input::controller_rumble(instance, 0, 1, 0);
    } else if (command == PAD_MOTOR_RUMBLE) {
      aurora::input::controller_rumble(instance, 1, 1, 0);
    } else if (command == PAD_MOTOR_STOP_HARD) {
      aurora::input::controller_rumble(instance, 0, 0, 0);
    }
  } else {
    if (command == PAD_MOTOR_STOP) {
      aurora::input::controller_rumble(instance, 0, 0, 1);
    } else if (command == PAD_MOTOR_RUMBLE) {
      aurora::input::controller_rumble(instance, 32767, 32767, 0);
    } else if (command == PAD_MOTOR_STOP_HARD) {
      aurora::input::controller_rumble(instance, 0, 0, 0);
    }
  }
}

void PADControlAllMotors(const uint32_t* commands) {
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    PADControlMotor(i, commands[i]);
  }
}

struct PADCLampRegion {
  uint8_t minTrigger;
  uint8_t maxTrigger;
  int8_t minStick;
  int8_t maxStick;
  int8_t xyStick;
  int8_t minSubstick;
  int8_t maxSubstick;
  int8_t xySubstick;
  int8_t radStick;
  int8_t radSubstick;
};

static constexpr PADCLampRegion ClampRegion{
    // Triggers
    30,
    180,

    // Left stick
    15,
    72,
    40,

    // Right stick
    15,
    59,
    31,

    // Stick radii
    56,
    44,
};

void ClampTrigger(uint8_t* trigger, uint8_t min, uint8_t max) {
  if (*trigger <= min) {
    *trigger = 0;
  } else {
    if (*trigger > max) {
      *trigger = max;
    }
    *trigger -= min;
  }
}

void ClampCircle(int8_t* px, int8_t* py, int8_t radius, int8_t min) {
  int x = *px;
  int y = *py;

  if (-min < x && x < min) {
    x = 0;
  } else if (0 < x) {
    x -= min;
  } else {
    x += min;
  }

  if (-min < y && y < min) {
    y = 0;
  } else if (0 < y) {
    y -= min;
  } else {
    y += min;
  }

  int squared = x * x + y * y;
  if (radius * radius < squared) {
    int32_t length = static_cast<int32_t>(std::sqrt(squared));
    x = (x * radius) / length;
    y = (y * radius) / length;
  }

  *px = static_cast<int8_t>(x);
  *py = static_cast<int8_t>(y);
}

void ClampStick(int8_t* px, int8_t* py, int8_t max, int8_t xy, int8_t min) {
  int32_t x = *px;
  int32_t y = *py;

  int32_t signX = 0;
  if (0 <= x) {
    signX = 1;
  } else {
    signX = -1;
    x = -x;
  }

  int8_t signY = 0;
  if (0 <= y) {
    signY = 1;
  } else {
    signY = -1;
    y = -y;
  }

  if (x <= min) {
    x = 0;
  } else {
    x -= min;
  }
  if (y <= min) {
    y = 0;
  } else {
    y -= min;
  }

  if (x == 0 && y == 0) {
    *px = *py = 0;
    return;
  }

  x = (x * max) / (INT8_MAX - min);
  y = (y * max) / (INT8_MAX - min);

  if (xy * y <= xy * x) {
    int32_t d = xy * x + (max - xy) * y;
    if (xy * max < d) {
      x = (xy * max * x / d);
      y = (xy * max * y / d);
    }
  } else {
    int32_t d = xy * y + (max - xy) * x;
    if (xy * max < d) {
      x = (xy * max * x / d);
      y = (xy * max * y / d);
    }
  }

  *px = (signX * x);
  *py = (signY * y);
}

void PADClamp(PADStatus* status) {
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    if (status[i].err != PAD_ERR_NONE) {
      continue;
    }

    ClampStick(&status[i].stickX, &status[i].stickY, ClampRegion.maxStick, ClampRegion.xyStick, ClampRegion.minStick);
    ClampStick(&status[i].substickX, &status[i].substickY, ClampRegion.maxSubstick, ClampRegion.xySubstick,
               ClampRegion.minSubstick);
    ClampTrigger(&status[i].triggerLeft, ClampRegion.minTrigger, ClampRegion.maxTrigger);
    ClampTrigger(&status[i].triggerRight, ClampRegion.minTrigger, ClampRegion.maxTrigger);
  }
}

void PADClampCircle(PADStatus* status) {
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    if (status[i].err != PAD_ERR_NONE) {
      continue;
    }

    ClampCircle(&status[i].stickX, &status[i].stickY, ClampRegion.radStick, ClampRegion.minStick);
    ClampCircle(&status[i].substickX, &status[i].substickY, ClampRegion.radSubstick, ClampRegion.minSubstick);
    ClampTrigger(&status[i].triggerLeft, ClampRegion.minTrigger, ClampRegion.maxTrigger);
    ClampTrigger(&status[i].triggerRight, ClampRegion.minTrigger, ClampRegion.maxTrigger);
  }
}

void PADGetVidPid(uint32_t port, uint32_t* vid, uint32_t* pid) {
  *vid = 0;
  *pid = 0;
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  *vid = controller->m_vid;
  *pid = controller->m_pid;
}

const char* PADGetName(uint32_t port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return nullptr;
  }

  return SDL_GetGamepadName(controller->m_controller);
}

void PADSetButtonMapping(uint32_t port, PADButtonMapping mapping) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  auto iter = std::find_if(controller->m_buttonMapping.begin(), controller->m_buttonMapping.end(),
                           [mapping](const auto& pair) { return mapping.padButton == pair.padButton; });
  if (iter == controller->m_buttonMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllButtonMappings(uint32_t port, PADButtonMapping buttons[12]) {
  for (uint32_t i = 0; i < 12; ++i) {
    PADSetButtonMapping(port, buttons[i]);
  }
}

PADButtonMapping* PADGetButtonMappings(uint32_t port, uint32_t* buttonCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *buttonCount = 0;
    return nullptr;
  }

  *buttonCount = controller->m_buttonMapping.size();
  return controller->m_buttonMapping.data();
}

void PADSetAxisMapping(u32 port, PADAxisMapping mapping) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  auto iter = std::find_if(controller->m_axisMapping.begin(), controller->m_axisMapping.end(),
                           [mapping](const auto& pair) { return mapping.padAxis == pair.padAxis; });
  if (iter == controller->m_axisMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllAxisMappings(u32 port, PADAxisMapping axes[10]) {
  for (uint32_t i = 0; i < 10; ++i) {
    PADSetAxisMapping(port, axes[i]);
  }
}

PADAxisMapping* PADGetAxisMappings(uint32_t port, uint32_t* axisCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *axisCount = 0;
    return nullptr;
  }

  *axisCount = controller->m_axisMapping.size();
  return controller->m_axisMapping.data();
}

void __PADWriteDeadZones(FILE* file, aurora::input::GameController& controller) {
  fwrite(&controller.m_deadZones, 1, sizeof(PADDeadZones), file);
}

void PADSerializeMappings() {
  std::string basePath{aurora::g_config.configPath};

  bool wroteGameCubeAlready = false;
  for (auto& controller : aurora::input::g_GameControllers) {
    if (!controller.second.m_mappingLoaded) {
      __PADLoadMapping(&controller.second);
    }
    FILE* file = fopen(fmt::format("{}/{}_{:04X}_{:04X}.controller", basePath,
                                   aurora::input::controller_name(controller.second.m_index), controller.second.m_vid,
                                   controller.second.m_pid)
                           .c_str(),
                       "wb");
    if (file == nullptr) {
      return;
    }

    uint32_t magic = SBIG('CTRL');
    uint32_t version = 1;
    fwrite(&magic, 1, sizeof(magic), file);
    fwrite(&version, 1, sizeof(magic), file);
    fwrite(&controller.second.m_isGameCube, 1, 1, file);
    fseek(file, (ftell(file) + 31) & ~31, SEEK_SET);
    int32_t dataStart = ftell(file);
    if (!controller.second.m_isGameCube) {
      __PADWriteDeadZones(file, controller.second);
      fwrite(controller.second.m_buttonMapping.data(), 1, sizeof(PADButtonMapping) * controller.second.m_buttonMapping.size(),
             file);
      fwrite(controller.second.m_axisMapping.data(), 1, sizeof(PADAxisMapping) * controller.second.m_axisMapping.size(),
             file);
    } else {
      if (!wroteGameCubeAlready) {
        /* This needs to remain at 4 since the adapter only has 4 ports */
        for (uint32_t i = 0; i < 4; ++i) {
          /* Just use the current controller's configs for this */
          __PADWriteDeadZones(file, controller.second);
          fwrite(g_defaultButtons.data(), 1, sizeof(PADButtonMapping) * g_defaultButtons.size(), file);
          fwrite(g_defaultAxes.data(), 1, sizeof(PADAxisMapping) * g_defaultAxes.size(), file);
        }
        fflush(file);
        wroteGameCubeAlready = true;
      }
      uint32_t port = aurora::input::player_index(controller.second.m_index);
      fseek(file, dataStart + ((sizeof(PADDeadZones) + sizeof(PADButtonMapping)) * port), SEEK_SET);
      __PADWriteDeadZones(file, controller.second);
      fwrite(controller.second.m_buttonMapping.data(), 1, sizeof(PADButtonMapping) * controller.second.m_buttonMapping.size(),
             file);
      fwrite(controller.second.m_axisMapping.data(), 1, sizeof(PADAxisMapping) * controller.second.m_axisMapping.size(),
             file);
    }
    fclose(file);
  }
}

PADDeadZones* PADGetDeadZones(uint32_t port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return nullptr;
  }
  return &controller->m_deadZones;
}

static constexpr std::array<std::pair<PADButton, std::string_view>, 12> skButtonNames = {{
    {PAD_BUTTON_LEFT, "Left"sv},
    {PAD_BUTTON_RIGHT, "Right"sv},
    {PAD_BUTTON_DOWN, "Down"sv},
    {PAD_BUTTON_UP, "Up"sv},
    {PAD_TRIGGER_Z, "Z"sv},
    {PAD_TRIGGER_R, "R"sv},
    {PAD_TRIGGER_L, "L"sv},
    {PAD_BUTTON_A, "A"sv},
    {PAD_BUTTON_B, "B"sv},
    {PAD_BUTTON_X, "X"sv},
    {PAD_BUTTON_Y, "Y"sv},
    {PAD_BUTTON_START, "Start"sv},
}};

static constexpr std::array<std::pair<PADButton, std::string_view>, 12> skAxisNames = {{
    {PAD_AXIS_LEFT_X_POS, "Left X+"sv},
    {PAD_AXIS_LEFT_X_NEG, "Left X-"sv},
    {PAD_AXIS_LEFT_Y_POS, "Left Y+"sv},
    {PAD_AXIS_LEFT_Y_NEG, "Left Y-"sv},
    {PAD_AXIS_RIGHT_X_POS, "Right X+"sv},
    {PAD_AXIS_RIGHT_X_NEG, "Right X-"sv},
    {PAD_AXIS_RIGHT_Y_POS, "Right Y+"sv},
    {PAD_AXIS_RIGHT_Y_NEG, "Right Y-"sv},
    {PAD_AXIS_TRIGGER_L, "Trigger L"sv},
    {PAD_AXIS_TRIGGER_R, "Trigger R"sv},
}};

static constexpr std::array<std::pair<PADButton, std::string_view>, 12> skAxisDirLabels = {{
    {PAD_AXIS_LEFT_X_POS, "Right"sv},
    {PAD_AXIS_LEFT_X_NEG, "Left"sv},
    {PAD_AXIS_LEFT_Y_POS, "Up"sv},
    {PAD_AXIS_LEFT_Y_NEG, "Down"sv},
    {PAD_AXIS_RIGHT_X_POS, "Right"sv},
    {PAD_AXIS_RIGHT_X_NEG, "Left"sv},
    {PAD_AXIS_RIGHT_Y_POS, "Up"sv},
    {PAD_AXIS_RIGHT_Y_NEG, "Down"sv},
    {PAD_AXIS_TRIGGER_L, "N/A"sv},
    {PAD_AXIS_TRIGGER_R, "N/A"sv},
}};

const char* PADGetButtonName(PADButton button) {
  auto it = std::find_if(skButtonNames.begin(), skButtonNames.end(),
                         [&button](const auto& pair) { return button == pair.first; });

  if (it != skButtonNames.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetNativeButtonName(uint32_t button) {
  return SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
}

const char* PADGetAxisName(PADAxis axis) {
  auto it = std::find_if(skAxisNames.begin(), skAxisNames.end(),
                         [&axis](const auto& pair) { return axis == pair.first; });

  if (it != skAxisNames.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetAxisDirectionLabel(PADAxis axis) {
  auto it = std::find_if(skAxisDirLabels.begin(), skAxisDirLabels.end(),
                         [&axis](const auto& pair) { return axis == pair.first; });

  if (it != skAxisDirLabels.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetNativeAxisName(PADSignedNativeAxis axis) {
  return SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axis.nativeAxis));
}

int32_t PADGetNativeButtonPressed(uint32_t port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return -1;
  }

  for (uint32_t i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
    if (SDL_GetGamepadButton(controller->m_controller, static_cast<SDL_GamepadButton>(i)) != 0u) {
      return i;
    }
  }
  return -1;
}

PADSignedNativeAxis PADGetNativeAxisPulled(uint32_t port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return {-1, AXIS_SIGN_POSITIVE};
  }

  for (int32_t i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
    auto axisVal = SDL_GetGamepadAxis(controller->m_controller, static_cast<SDL_GamepadAxis>(i));
    if (axisVal >= 16384) {
      if (i == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || i == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        return {i, AXIS_SIGN_POSITIVE};
      } else {
        return {i, AXIS_SIGN_POSITIVE};
      }
    } else if (axisVal <= -16384) {
      return {i, AXIS_SIGN_NEGATIVE};
    }
  }
  return {-1, AXIS_SIGN_POSITIVE};
}

void PADRestoreDefaultMapping(uint32_t port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }
  controller->m_buttonMapping = g_defaultButtons;
  controller->m_axisMapping = g_defaultAxes;
}

void PADBlockInput(bool block) { gBlockPAD = block; }

SDL_Gamepad* PADGetSDLGamepadForIndex(u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return ctrl->m_controller;
}

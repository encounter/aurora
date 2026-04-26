#include "../../input.hpp"
#include "../../internal.hpp"
#include <dolphin/pad.h>
#include <dolphin/si.h>

#include <array>
#include <sys/stat.h>

static const int32_t k_mappingsFileVersion = 3;

static std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtons{{
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

static std::array<PADAxisMapping, PAD_AXIS_COUNT> g_defaultAxes{{
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

template <typename T, size_t N>
constexpr const std::array<T, N>& toStdArray(const T (&array)[N]) {
  static_assert(sizeof(array) == sizeof(std::array<T, N>));
  return reinterpret_cast<const std::array<T, N>&>(array);
}

static bool g_initialized;

void PADSetSpec(u32 spec) {}
BOOL PADInit() {
  if (g_initialized) {
    return true;
  }

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

  auto path = fmt::format("{}/{}_{:04X}_{:04X}.controller", basePath, PADGetName(playerIndex), controller->m_vid,
                          controller->m_pid);
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
  if (version != k_mappingsFileVersion) {
    aurora::input::Log.warn("Invalid controller mapping version! (Expected {0}, found {1})", k_mappingsFileVersion,
                            version);
    return;
  }

  bool isGameCube = false;
  fread(&isGameCube, 1, 1, file);
  fseek(file, (ftell(file) + 31) & ~31, SEEK_SET);
  uint32_t dataStart = ftell(file);
  if (isGameCube) {
    uint32_t dzSecLen = sizeof(PADDeadZones);
    uint32_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
    uint32_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
    fseek(file, dataStart + ((dzSecLen + btnSecLen + axisSecLen) * playerIndex), SEEK_SET);
  }

  fread(&controller->m_deadZones, 1, sizeof(PADDeadZones), file);
  fread(&controller->m_buttonMapping, 1, sizeof(PADButtonMapping) * PAD_BUTTON_COUNT, file);
  fread(&controller->m_axisMapping, 1, sizeof(PADAxisMapping) * PAD_AXIS_COUNT, file);
  if (!isGameCube) {
    fread(&controller->m_rumbleIntensityLow, 1, sizeof(u16), file);
    fread(&controller->m_rumbleIntensityHigh, 1, sizeof(u16), file);
  }
  fclose(file);
}

static void EnsureMappingLoaded(aurora::input::GameController* controller) {
  if (!controller->m_mappingLoaded) {
    __PADLoadMapping(controller);
  }
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
    return static_cast<Sint16>(
        std::min(SDL_GetGamepadAxis(controller->m_controller, static_cast<SDL_GamepadAxis>(nativeAxis.nativeAxis)) *
                     nativeAxis.sign,
                 SDL_JOYSTICK_AXIS_MAX));
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

    EnsureMappingLoaded(controller);
    status[i].err = PAD_ERR_NONE;
    std::for_each(
        controller->m_buttonMapping.begin(), controller->m_buttonMapping.end(),
        [&controller, &i, &status](const auto& mapping) {
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

    Sint16 tl = std::max((Sint16)0, _get_axis_value(controller, PAD_AXIS_TRIGGER_L));
    Sint16 tr = std::max((Sint16)0, _get_axis_value(controller, PAD_AXIS_TRIGGER_R));
    if (/*!controller->m_isGameCube && */ controller->m_deadZones.emulateTriggers) {
      if (tl > controller->m_deadZones.leftTriggerActivationZone) {
        status[i].button |= PAD_TRIGGER_L;
      }
      if (tr > controller->m_deadZones.rightTriggerActivationZone) {
        status[i].button |= PAD_TRIGGER_R;
      }
    }
    tl /= 128;
    tr /= 128;

    status[i].triggerLeft = static_cast<int8_t>(tl);
    status[i].triggerRight = static_cast<int8_t>(tr);

    if (controller->m_hasRumble) {
      rumbleSupport |= PAD_CHAN0_BIT >> i;
    }

    // Update the LED colors when the controller is read (which should happen once per frame in most games)
    SDL_SetGamepadLED(controller->m_controller, controller->m_ledRed, controller->m_ledGreen, controller->m_ledBlue);
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
      aurora::input::controller_rumble(instance, controller->m_rumbleIntensityLow, controller->m_rumbleIntensityHigh,
                                       0);
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

void PADSetAllButtonMappings(uint32_t port, PADButtonMapping buttons[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    PADSetButtonMapping(port, buttons[i]);
  }
}

PADButtonMapping* PADGetButtonMappings(uint32_t port, uint32_t* buttonCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *buttonCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);

  *buttonCount = PAD_BUTTON_COUNT;
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

void PADSetAllAxisMappings(u32 port, PADAxisMapping axes[PAD_AXIS_COUNT]) {
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    PADSetAxisMapping(port, axes[i]);
  }
}

PADAxisMapping* PADGetAxisMappings(uint32_t port, uint32_t* axisCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *axisCount = 0;
    return nullptr;
  }

  *axisCount = PAD_AXIS_COUNT;
  return controller->m_axisMapping.data();
}

void __PADWriteDeadZones(FILE* file, aurora::input::GameController& controller) {
  fwrite(&controller.m_deadZones, 1, sizeof(PADDeadZones), file);
}

void PADSerializeMappings() {
  std::filesystem::path basePath{aurora::g_config.configPath};

  for (auto& controller : aurora::input::g_GameControllers) {
    EnsureMappingLoaded(&controller.second);
    std::filesystem::path filePath =
        basePath / fmt::format("{}_{:04X}_{:04X}.controller", aurora::input::controller_name(controller.second.m_index),
                               controller.second.m_vid, controller.second.m_pid);
    std::string filePathStr = filePath.string();

    // don't truncate the file if it already exists
    const char* openMode = std::filesystem::exists(filePath) ? "r+b" : "wb";
    FILE* file = fopen(filePathStr.c_str(), openMode);
    if (file == nullptr) {
      return;
    }
    fseek(file, 0, SEEK_SET);

    // write header
    uint32_t magic = SBIG('CTRL');
    fwrite(&magic, 1, sizeof(magic), file);
    fwrite(&k_mappingsFileVersion, 1, sizeof(k_mappingsFileVersion), file);
    fwrite(&controller.second.m_isGameCube, 1, 1, file);

    // start writing data at next 32-byte aligned offset
    int32_t dataStart = (ftell(file) + 31) & ~31;
    fseek(file, dataStart, SEEK_SET);
    if (controller.second.m_isGameCube) {
      // GameCube adapters expose 4 input devices with the same vid/pid, we store all 4 in the same file
      uint32_t port = aurora::input::player_index(controller.second.m_index);
      uint32_t dzSecLen = sizeof(PADDeadZones);
      uint32_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
      uint32_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
      // skip to offset in file for this particular port
      fseek(file, dataStart + ((dzSecLen + btnSecLen + axisSecLen) * port), SEEK_SET);
    }
    __PADWriteDeadZones(file, controller.second);
    fwrite(controller.second.m_buttonMapping.data(), 1, sizeof(PADButtonMapping) * PAD_BUTTON_COUNT, file);
    fwrite(controller.second.m_axisMapping.data(), 1, sizeof(PADAxisMapping) * PAD_AXIS_COUNT, file);

    if (!controller.second.m_isGameCube) {
      fwrite(&controller.second.m_rumbleIntensityLow, 1, sizeof(u16), file);
      fwrite(&controller.second.m_rumbleIntensityHigh, 1, sizeof(u16), file);
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

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_BUTTON_COUNT> skButtonNames = {{
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

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_AXIS_COUNT> skAxisNames = {{
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

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_AXIS_COUNT> skAxisDirLabels = {{
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
  auto it =
      std::find_if(skAxisNames.begin(), skAxisNames.end(), [&axis](const auto& pair) { return axis == pair.first; });

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

void PADSetDefaultMapping(const PADDefaultMapping* mapping) {
  if (g_initialized) {
    aurora::input::Log.fatal("PADSetDefaultMapping called after PADInit()!");
  }

  g_defaultButtons = toStdArray(mapping->buttons);
  g_defaultAxes = toStdArray(mapping->axes);
}

BOOL PADSetColor(u32 port, u8 red, u8 green, u8 blue) {
  const auto ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  ctrl->m_ledRed = red;
  ctrl->m_ledGreen = green;
  ctrl->m_ledBlue = blue;
  return true;
}

BOOL PADGetColor(u32 port, u8* red, u8* green, u8* blue) {
  const auto ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  *red = ctrl->m_ledRed;
  *green = ctrl->m_ledGreen;
  *blue = ctrl->m_ledBlue;
  return TRUE;
}

BOOL PADSetSensorEnabled(u32 port, PADSensorType sensor, BOOL enabled) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);

  if (ctrl == nullptr) {
    return FALSE;
  }

  return SDL_SetGamepadSensorEnabled(ctrl->m_controller, static_cast<SDL_SensorType>(sensor), enabled ? true : false)
             ? TRUE
             : FALSE;
}

BOOL PADHasSensor(u32 port, PADSensorType sensor) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  return SDL_GamepadHasSensor(ctrl->m_controller, static_cast<SDL_SensorType>(sensor)) ? TRUE : FALSE;
}

BOOL PADGetSensorData(u32 port, PADSensorType sensor, f32* data, const int nValues) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  return SDL_GetGamepadSensorData(ctrl->m_controller, static_cast<SDL_SensorType>(sensor), data, nValues);
}

BOOL PADSetRumbleIntensity(u32 port, u16 low, u16 high) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr || ctrl->m_isGameCube || !ctrl->m_hasRumble) {
    return FALSE;
  }
  ctrl->m_rumbleIntensityLow = low;
  ctrl->m_rumbleIntensityHigh = high;

  return TRUE;
}

BOOL PADGetRumbleIntensity(u32 port, u16* low, u16* high) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr || ctrl->m_isGameCube || !ctrl->m_hasRumble) {
    *low = 0;
    *high = 0;
    return FALSE;
  }

  *low = ctrl->m_rumbleIntensityLow;
  *high = ctrl->m_rumbleIntensityHigh;
  return TRUE;
}

BOOL PADSupportsRumbleIntensity(u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  return !ctrl->m_isGameCube && ctrl->m_hasRumble;
}

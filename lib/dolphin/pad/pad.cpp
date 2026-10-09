#include "adapter.hpp"
#include "../../gamepad.hpp"
#include "../../device.hpp"
#include "../../internal.hpp"
#include "../../io.hpp"
#include <aurora/input.hpp>
#include <dolphin/pad.h>
#include <dolphin/si.h>

#include <array>
#include <filesystem>
#include <limits>
#include <ranges>
#include <sys/stat.h>
#include <SDL3/SDL.h>

namespace {
constexpr aurora::Module Log{"aurora::pad"};

constexpr int32_t k_mappingsFileVersion = 4;
constexpr int32_t k_minMappingsFileVersion = 3;

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsStandard{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsXBox360{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsXBoxOne{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsSteam{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS3{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS4{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS5{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsGamecube{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {SDL_GAMEPAD_BUTTON_MISC3, PAD_TRIGGER_L},
    {SDL_GAMEPAD_BUTTON_MISC4, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsNSOGamecube{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
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

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsProCon{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyConRight{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyConLeft{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyPair{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> kUnboundKeys{{
    {PAD_KEY_INVALID, PAD_BUTTON_A},
    {PAD_KEY_INVALID, PAD_BUTTON_B},
    {PAD_KEY_INVALID, PAD_BUTTON_X},
    {PAD_KEY_INVALID, PAD_BUTTON_Y},
    {PAD_KEY_INVALID, PAD_BUTTON_START},
    {PAD_KEY_INVALID, PAD_TRIGGER_Z},
    {PAD_KEY_INVALID, PAD_TRIGGER_L},
    {PAD_KEY_INVALID, PAD_TRIGGER_R},
    {PAD_KEY_INVALID, PAD_BUTTON_UP},
    {PAD_KEY_INVALID, PAD_BUTTON_DOWN},
    {PAD_KEY_INVALID, PAD_BUTTON_LEFT},
    {PAD_KEY_INVALID, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> kUnboundKeyAxes{{
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_X_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_X_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_Y_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_Y_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_X_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_X_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_Y_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_Y_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_L, 0},
    {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_R, 0},
}};

std::array<PADAxisMapping, PAD_AXIS_COUNT> g_defaultAxes{{
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

std::array<PADKeyboardState, PAD_MAX_CONTROLLERS> g_defaultKeyboardBindings = [] {
  std::array<PADKeyboardState, PAD_MAX_CONTROLLERS> defaults;
  for (auto& state : defaults) {
    state.m_buttonMapping = kUnboundKeys;
    state.m_axisMapping = kUnboundKeyAxes;
  }
  return defaults;
}();
std::array<PADKeyboardState, PAD_MAX_CONTROLLERS> g_keyboardBindings;

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

constexpr PADCLampRegion ClampRegion{
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

bool g_initialized;
bool g_keyboardBindingsLoaded = false;
} // namespace

void PADSetSpec(u32 spec [[maybe_unused]]) {}

static void load_keyboard_bindings();
static void save_keyboard_bindings();

static bool seek_aligned(SDL_IOStream* file, Sint64& dataStart) {
  const Sint64 position = SDL_TellIO(file);
  if (position < 0 || position > std::numeric_limits<Sint64>::max() - 31) {
    return false;
  }
  dataStart = (position + 31) & ~Sint64{31};
  return SDL_SeekIO(file, dataStart, SDL_IO_SEEK_SET) == dataStart;
}

static bool device_rumble_available_for_port(const u32 port) {
  return port == PAD_CHAN0 && aurora::device::rumble_available();
}

static bool should_use_device_rumble(const u32 port, const aurora::gamepad::GameController* controller) {
  return device_rumble_available_for_port(port) &&
         (controller == nullptr ||
          (!controller->m_isGameCube && (!controller->m_hasRumble || controller->m_forceDeviceRumble)));
}

static bool device_gyro_available_for_port(const u32 port) {
  return port == PAD_CHAN0 && aurora::device::gyro_available();
}

static bool device_accel_available_for_port(const u32 port) {
  return port == PAD_CHAN0 && aurora::device::accel_available();
}

static bool device_sensor_available_for_port(const u32 port, const PADSensorType sensor) {
  switch (sensor) {
  case PAD_SENSOR_ACCEL:
    return device_accel_available_for_port(port);
  case PAD_SENSOR_GYRO:
    return device_gyro_available_for_port(port);
  default:
    return false;
  }
}

static bool get_device_sensor_data(const PADSensorType sensor, f32* data, const int nValues) {
  switch (sensor) {
  case PAD_SENSOR_ACCEL:
    return aurora::device::accel(data, nValues);
  case PAD_SENSOR_GYRO:
    return aurora::device::gyro(data, nValues);
  default:
    return false;
  }
}

static bool controller_has_sensor(const aurora::gamepad::GameController* controller, const PADSensorType sensor) {
  return controller != nullptr && SDL_GamepadHasSensor(controller->m_controller, static_cast<SDL_SensorType>(sensor));
}

static bool should_use_device_sensor(const u32 port, const aurora::gamepad::GameController* controller,
                                     const PADSensorType sensor) {
  return device_sensor_available_for_port(port, sensor) && !controller_has_sensor(controller, sensor);
}

// ReSharper disable once CppDFAConstantFunctionResult
BOOL PADInit() {
  if (g_initialized) {
    return true;
  }
  g_initialized = true;

  for (u32 port = 0; port < g_keyboardBindings.size(); ++port) {
    auto& state = g_keyboardBindings[port];
    const auto& defaults = g_defaultKeyboardBindings[port];
    state.m_buttonMapping = defaults.m_buttonMapping;
    state.m_axisMapping = defaults.m_axisMapping;
  }

  if (!g_keyboardBindingsLoaded) {
    g_keyboardBindingsLoaded = true;
    load_keyboard_bindings();
  }

  aurora::pad::detail::initialize();
  return true;
}

BOOL PADRecalibrate(u32 mask [[maybe_unused]]) { return true; }

BOOL PADReset(u32 mask [[maybe_unused]]) { return true; }

void PADSetAnalogMode(u32 mode [[maybe_unused]]) {}

aurora::gamepad::GameController* __PADGetControllerForIndex(const u32 idx) /*  NOLINT(*-reserved-identifier) */
{
  if (idx >= aurora::gamepad::g_GameControllers.size()) {
    return nullptr;
  }

  uint32_t tmp = 0;
  auto iter = aurora::gamepad::g_GameControllers.begin();
  while (tmp < idx) {
    ++iter;
    ++tmp;
  }
  if (iter == aurora::gamepad::g_GameControllers.end()) {
    return nullptr;
  }

  return &iter->second;
}

u32 PADCount() { return aurora::gamepad::g_GameControllers.size(); }

const char* PADGetNameForControllerIndex(const u32 idx) {
  const auto* ctrl = __PADGetControllerForIndex(idx);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return SDL_GetGamepadName(ctrl->m_controller);
}

void PADSetPortForIndex(const u32 idx, const u32 port) {
  const auto* ctrl = __PADGetControllerForIndex(idx);
  if (ctrl == nullptr) {
    return;
  }

  const int32_t oldPort = SDL_GetGamepadPlayerIndex(ctrl->m_controller);
  if (const auto* dest = aurora::gamepad::get_controller_for_player(port); dest != nullptr && dest != ctrl) {
    SDL_SetGamepadPlayerIndex(dest->m_controller, -1);
  }
  if (oldPort >= 0 && oldPort != port) {
    aurora::gamepad::persist_controller_for_player(oldPort, nullptr);
  }
  SDL_SetGamepadPlayerIndex(ctrl->m_controller, static_cast<Sint32>(port));
  aurora::gamepad::persist_controller_for_player(port, ctrl);
}

int32_t PADGetIndexForPort(const u32 port) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return -1;
  }
  int32_t index = 0;
  for (auto iter = aurora::gamepad::g_GameControllers.begin(); iter != aurora::gamepad::g_GameControllers.end();
       ++iter, ++index) {
    if (&iter->second == ctrl) {
      break;
    }
  }

  return index;
}

void PADClearPort(const u32 port) {
  aurora::gamepad::persist_controller_for_player(port, nullptr);
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return;
  }
  SDL_SetGamepadPlayerIndex(ctrl->m_controller, -1);
}

const std::array<PADButtonMapping, PAD_BUTTON_COUNT>&
aurora::pad::detail::default_buttons(const gamepad::GameController& controller) {
  switch (SDL_GetGamepadType(controller.m_controller)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
    return g_defaultButtonsXBox360;
  case SDL_GAMEPAD_TYPE_XBOXONE:
    return g_defaultButtonsXBoxOne;
  case SDL_GAMEPAD_TYPE_STANDARD:
#if SDL_VERSION < SDL_VERSIONNUM(3, 4, 18)
    if (controller.m_vid == 0x28de && controller.m_pid == 0x1304) {
      return g_defaultButtonsSteam;
    }
#endif
    return g_defaultButtonsStandard;
  case SDL_GAMEPAD_TYPE_PS3:
    return g_defaultButtonsPS3;
  case SDL_GAMEPAD_TYPE_PS4:
    return g_defaultButtonsPS4;
  case SDL_GAMEPAD_TYPE_PS5:
    return g_defaultButtonsPS5;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    if (controller.m_pid == 0x2073) {
      return g_defaultButtonsNSOGamecube;
    }
    return g_defaultButtonsProCon;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    return g_defaultButtonsJoyConRight;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    return g_defaultButtonsJoyConLeft;
  case SDL_GAMEPAD_TYPE_GAMECUBE:
    return g_defaultButtonsGamecube;
#if SDL_VERSION_ATLEAST(3, 4, 18)
  case SDL_GAMEPAD_TYPE_STEAM:
    return g_defaultButtonsSteam;
#endif
  default:
    return g_defaultButtonsStandard;
  }
}

const std::array<PADAxisMapping, PAD_AXIS_COUNT>& aurora::pad::detail::default_axes() { return g_defaultAxes; }

const PADKeyboardState& aurora::pad::detail::keyboard_state(uint32_t port) { return g_keyboardBindings[port]; }

void __PADSetDefaultMapping(aurora::gamepad::GameController* controller) /*  NOLINT(*-reserved-identifier) */
{
  controller->m_buttonMapping = aurora::pad::detail::default_buttons(*controller);
}

void __PADLoadMapping(aurora::gamepad::GameController* controller) /*  NOLINT(*-reserved-identifier) */ {
  int32_t playerIndex = SDL_GetGamepadPlayerIndex(controller->m_controller);
  if (playerIndex < 0 || aurora::g_config.userPath == nullptr) {
    return;
  }

  if (!controller->m_mappingLoaded) {
    __PADSetDefaultMapping(controller);
    controller->m_axisMapping = g_defaultAxes;
  }

  controller->m_mappingLoaded = true;

  const auto path =
      aurora::io::fs_path_from_string(aurora::g_config.userPath) /
      fmt::format("{}_{:04X}_{:04X}.controller", PADGetName(playerIndex), controller->m_vid, controller->m_pid);
  auto file = aurora::io::open_file(path, "rb");
  if (!file) {
    return;
  }

  uint32_t magic = 0;
  if (!SDL_ReadU32LE(file.get(), &magic) || magic != SBIG('CTRL')) {
    Log.warn("Invalid controller mapping magic!");
    return;
  }

  uint32_t version = 0;
  if (!SDL_ReadU32LE(file.get(), &version) || version < k_minMappingsFileVersion || version > k_mappingsFileVersion) {
    Log.warn("Invalid controller mapping version! (Expected {0}..{1}, found {2})", k_minMappingsFileVersion,
             k_mappingsFileVersion, version);
    return;
  }

  uint8_t isGameCubeValue = 0;
  Sint64 dataStart = 0;
  if (!SDL_ReadU8(file.get(), &isGameCubeValue) || !seek_aligned(file.get(), dataStart)) {
    Log.warn("Unable to read controller bindings header! Path: \"{}\"", aurora::io::fs_path_to_string(path));
    return;
  }
  const bool isGameCube = isGameCubeValue != 0;
  if (isGameCube) {
    if (playerIndex >= PAD_CHANMAX) {
      return;
    }
    constexpr uint32_t dzSecLen = sizeof(PADDeadZones);
    constexpr uint32_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
    constexpr uint32_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
    const Sint64 portOffset = dataStart + (dzSecLen + btnSecLen + axisSecLen) * playerIndex;
    if (SDL_SeekIO(file.get(), portOffset, SDL_IO_SEEK_SET) != portOffset) {
      Log.warn("Unable to seek in controller bindings! Path: \"{}\"", aurora::io::fs_path_to_string(path));
      return;
    }
  }

  auto deadZones = controller->m_deadZones;
  auto buttonMapping = controller->m_buttonMapping;
  auto axisMapping = controller->m_axisMapping;
  auto rumbleIntensityLow = controller->m_rumbleIntensityLow;
  auto rumbleIntensityHigh = controller->m_rumbleIntensityHigh;
  auto forceDeviceRumble = controller->m_forceDeviceRumble;
  bool ok = aurora::io::read_exact(file.get(), &deadZones, sizeof(deadZones)) &&
            aurora::io::read_exact(file.get(), buttonMapping.data(), sizeof(buttonMapping)) &&
            aurora::io::read_exact(file.get(), axisMapping.data(), sizeof(axisMapping));
  if (!isGameCube) {
    ok = ok && SDL_ReadU16LE(file.get(), &rumbleIntensityLow) && SDL_ReadU16LE(file.get(), &rumbleIntensityHigh);
    if (version >= k_mappingsFileVersion) {
      uint8_t forceDeviceRumbleValue = 0;
      ok = ok && SDL_ReadU8(file.get(), &forceDeviceRumbleValue);
      forceDeviceRumble = forceDeviceRumbleValue != 0;
    }
  }
  if (!ok) {
    Log.warn("Unable to read controller bindings! Path: \"{}\"", aurora::io::fs_path_to_string(path));
    return;
  }
  controller->m_deadZones = deadZones;
  controller->m_buttonMapping = buttonMapping;
  controller->m_axisMapping = axisMapping;
  controller->m_rumbleIntensityLow = rumbleIntensityLow;
  controller->m_rumbleIntensityHigh = rumbleIntensityHigh;
  controller->m_forceDeviceRumble = forceDeviceRumble;

  bool axisCorrupt = false;
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    if (controller->m_axisMapping[i].padAxis != static_cast<PADAxis>(i)) {
      axisCorrupt = true;
      break;
    }
  }
  if (axisCorrupt) {
    Log.warn("__PADLoadMapping port={}: corrupt axis data in file, resetting axes to defaults", playerIndex);
    controller->m_axisMapping = g_defaultAxes;
  }

  bool buttonCorrupt = false;
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    if (controller->m_buttonMapping[i].padButton == 0) {
      buttonCorrupt = true;
      break;
    }
  }
  if (buttonCorrupt) {
    Log.warn("__PADLoadMapping port={}: corrupt button data in file, resetting buttons to defaults", playerIndex);
    __PADSetDefaultMapping(controller);
  }
}

static void EnsureMappingLoaded(aurora::gamepad::GameController* controller) {
  if (!controller->m_mappingLoaded) {
    __PADLoadMapping(controller);
  }
}

void aurora::pad::detail::ensure_mapping_loaded(gamepad::GameController* controller) {
  EnsureMappingLoaded(controller);
}

u32 PADRead(PADStatus* status) {
  if (!g_initialized) {
    Log.fatal("PADRead called before PADInit()!");
  }

  // Pick up capture changes made since the last event.
  aurora::input::reconcile();

  uint32_t rumbleSupport = 0;
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    memset(&status[i], 0, sizeof(PADStatus));
    // Support device rumble on port 0 regardless of whether a controller is connected.
    if (device_rumble_available_for_port(i)) {
      rumbleSupport |= PAD_CHAN0_BIT;
    }
    if (!aurora::pad::detail::read(i, status[i])) {
      status[i].err = PAD_ERR_NO_CONTROLLER;
      continue;
    }

    if (auto* controller = aurora::gamepad::get_controller_for_player(i)) {
      if (controller->m_hasRumble) {
        rumbleSupport |= PAD_CHAN0_BIT >> i;
      }

      // Update the LED colors when they exist and the controller is read (which should happen once per frame in most
      // games)
      if (controller->m_hasRgbLed && controller->m_isColorDirty) {
        SDL_SetGamepadLED(controller->m_controller, controller->m_ledRed, controller->m_ledGreen,
                          controller->m_ledBlue);
        controller->m_isColorDirty = false;
      }
    }
  }
  return rumbleSupport;
}

void PADSetVirtualStatus(const u32 port, const PADStatus* virtualStatus) {
  if (port < PAD_CHANMAX && virtualStatus != nullptr) {
    aurora::pad::detail::set_virtual_status(port, virtualStatus);
  }
}

void PADClearVirtualStatus(const u32 port) {
  if (port < PAD_CHANMAX) {
    aurora::pad::detail::set_virtual_status(port, nullptr);
  }
}

void PADClearAllVirtualStatus() {
  for (u32 port = 0; port < PAD_CHANMAX; ++port) {
    aurora::pad::detail::set_virtual_status(port, nullptr);
  }
}

BOOL PADConsumeCancellation(const u32 port) {
  return port < PAD_CHANMAX && aurora::pad::detail::consume_cancellation(port) ? TRUE : FALSE;
}

BOOL PADIsInputCaptured(const u32 port) {
  return port < PAD_CHANMAX && aurora::pad::detail::captured(port) ? TRUE : FALSE;
}

void PADControlMotor(const u32 chan, const u32 cmd) {
  const auto controller = aurora::gamepad::get_controller_for_player(chan);
  if (should_use_device_rumble(chan, controller)) {
    u16 low = 0;
    u16 high = 0;
    if (controller != nullptr) {
      EnsureMappingLoaded(controller);
      low = controller->m_rumbleIntensityLow;
      high = controller->m_rumbleIntensityHigh;
    } else {
      aurora::gamepad::get_device_rumble_intensity(&low, &high);
    }
    if (cmd == PAD_MOTOR_STOP || cmd == PAD_MOTOR_STOP_HARD) {
      aurora::device::rumble(0, 0, 0);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::device::rumble(low, high, 0);
    }
    return;
  }

  if (controller == nullptr) {
    return;
  }

  const auto instance = aurora::gamepad::get_instance_for_player(chan);
  if (controller->m_isGameCube) {
    if (cmd == PAD_MOTOR_STOP) {
      aurora::gamepad::controller_rumble(instance, 0, 1, 0);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::gamepad::controller_rumble(instance, 1, 1, 0);
    } else if (cmd == PAD_MOTOR_STOP_HARD) {
      aurora::gamepad::controller_rumble(instance, 0, 0, 0);
    }
  } else {
    if (cmd == PAD_MOTOR_STOP) {
      aurora::gamepad::controller_rumble(instance, 0, 0, 1);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::gamepad::controller_rumble(instance, controller->m_rumbleIntensityLow, controller->m_rumbleIntensityHigh,
                                         0);
    } else if (cmd == PAD_MOTOR_STOP_HARD) {
      aurora::gamepad::controller_rumble(instance, 0, 0, 0);
    }
  }
}

void PADControlAllMotors(const u32* cmdArr) {
  for (u32 i = 0; i < PAD_CHANMAX; ++i) {
    PADControlMotor(i, cmdArr[i]);
  }
}

void ClampTrigger(u8* trigger, const u8 min, const u8 max) {
  if (*trigger <= min) {
    *trigger = 0;
  } else {
    if (*trigger > max) {
      *trigger = max;
    }
    *trigger -= min;
  }
}

void ClampCircle(s8* px, s8* py, const s8 radius, const s8 min) {
  int x = *px; // NOLINT(*-str34-c)
  int y = *py; // NOLINT(*-str34-c)

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

  if (const int squared = x * x + y * y; radius * radius < squared) {
    const auto length = static_cast<int32_t>(std::sqrt(squared));
    x = x * radius / length;
    y = y * radius / length;
  }

  *px = static_cast<int8_t>(x);
  *py = static_cast<int8_t>(y);
}

void ClampStick(s8* px, s8* py, const s8 max, const s8 xy, const s8 min) {
  int32_t x = *px; // NOLINT(*-str34-c)
  int32_t y = *py; // NOLINT(*-str34-c)

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

  x = x * max / (INT8_MAX - min);
  y = y * max / (INT8_MAX - min);

  if (xy * y <= xy * x) {
    if (const int32_t d = xy * x + (max - xy) * y; xy * max < d) {
      x = xy * max * x / d;
      y = xy * max * y / d;
    }
  } else {
    if (const int32_t d = xy * y + (max - xy) * x; xy * max < d) {
      x = xy * max * x / d;
      y = xy * max * y / d;
    }
  }

  *px = static_cast<s8>(signX * x);
  *py = static_cast<s8>(signY * y);
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

void PADGetVidPid(const u32 port, u32* vid, u32* pid) {
  *vid = 0;
  *pid = 0;
  const auto* controller = aurora::gamepad::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  *vid = controller->m_vid;
  *pid = controller->m_pid;
}

const char* PADGetName(const u32 port) {
  const auto* controller = aurora::gamepad::get_controller_for_player(port);
  if (controller == nullptr) {
    return nullptr;
  }

  return SDL_GetGamepadName(controller->m_controller);
}

void PADSetButtonMapping(const u32 port, const PADButtonMapping mapping) {
  auto* controller = aurora::gamepad::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  const auto iter = std::ranges::find_if(controller->m_buttonMapping,
                                         [mapping](const auto& pair) { return mapping.padButton == pair.padButton; });
  if (iter == controller->m_buttonMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllButtonMappings(const u32 port, const PADButtonMapping buttons[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    PADSetButtonMapping(port, buttons[i]);
  }
}

PADButtonMapping* PADGetButtonMappings(const u32 port, u32* buttonCount) {
  auto* controller = aurora::gamepad::get_controller_for_player(port);
  if (controller == nullptr) {
    *buttonCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);

  *buttonCount = PAD_BUTTON_COUNT;
  return controller->m_buttonMapping.data();
}

void PADSetAxisMapping(const u32 port, const PADAxisMapping mapping) {
  auto* controller = aurora::gamepad::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  const auto iter = std::ranges::find_if(controller->m_axisMapping,
                                         [mapping](const auto& pair) { return mapping.padAxis == pair.padAxis; });
  if (iter == controller->m_axisMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllAxisMappings(const u32 port, const PADAxisMapping axes[PAD_AXIS_COUNT]) {
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    PADSetAxisMapping(port, axes[i]);
  }
}

PADAxisMapping* PADGetAxisMappings(const u32 port, u32* axisCount) {
  auto* controller = aurora::gamepad::get_controller_for_player(port);
  if (controller == nullptr) {
    *axisCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);
  *axisCount = PAD_AXIS_COUNT;
  return controller->m_axisMapping.data();
}

BOOL PADSetKeyButtonBinding(const u32 port, const PADKeyButtonBinding binding) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return FALSE;
  }

  for (auto& state = g_keyboardBindings[port]; auto& [scancode, padButton] : state.m_buttonMapping) {
    if (padButton == binding.padButton) {
      scancode = binding.scancode;
      return TRUE;
    }
  }

  return FALSE;
}

BOOL PADSetKeyButtonBindings(const u32 port, PADKeyButtonBinding bindings[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    if (!PADSetKeyButtonBinding(port, bindings[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

PADKeyButtonBinding* PADGetKeyButtonBindings(const u32 port, u32* buttonCount) {
  if (port >= PAD_MAX_CONTROLLERS || !g_keyboardBindings[port].m_mappingsSet) {
    return nullptr;
  }
  auto& state = g_keyboardBindings[port];
  *buttonCount = PAD_BUTTON_COUNT;
  return state.m_buttonMapping.data();
}

BOOL PADSetKeyAxisBinding(const u32 port, const PADKeyAxisBinding binding) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return FALSE;
  }

  for (auto& state = g_keyboardBindings[port]; auto& b : state.m_axisMapping) {
    if (b.padAxis == binding.padAxis) {
      b.scancode = binding.scancode;
      return TRUE;
    }
  }

  return FALSE;
}
BOOL PADSetKeyAxisBindings(const u32 port, PADKeyAxisBinding bindings[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    if (!PADSetKeyAxisBinding(port, bindings[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

PADKeyAxisBinding* PADGetKeyAxisBindings(const u32 port, u32* axisCount) {
  if (port >= PAD_MAX_CONTROLLERS || !g_keyboardBindings[port].m_mappingsSet) {
    return nullptr;
  }
  auto& state = g_keyboardBindings[port];
  *axisCount = PAD_AXIS_COUNT;
  return state.m_axisMapping.data();
}

BOOL PADSetDefaultKeyBindings(const u32 port, const PADDefaultKeyBindings* bindings) {
  if (g_initialized || port >= PAD_MAX_CONTROLLERS || bindings == nullptr) {
    return FALSE;
  }

  auto defaults = g_defaultKeyboardBindings[port];
  for (auto& button : defaults.m_buttonMapping) {
    const auto* binding = std::ranges::find(bindings->buttons, button.padButton, &PADKeyButtonBinding::padButton);
    if (binding == std::end(bindings->buttons)) {
      return FALSE;
    }
    button = *binding;
  }
  for (auto& axis : defaults.m_axisMapping) {
    const auto* binding = std::ranges::find(bindings->axes, axis.padAxis, &PADKeyAxisBinding::padAxis);
    if (binding == std::end(bindings->axes)) {
      return FALSE;
    }
    axis = *binding;
  }
  g_defaultKeyboardBindings[port] = std::move(defaults);
  return TRUE;
}

void PADRestoreDefaultKeyBindings(const u32 port) {
  if (!g_initialized || port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  auto& state = g_keyboardBindings[port];
  const auto& defaults = g_defaultKeyboardBindings[port];
  state.m_buttonMapping = defaults.m_buttonMapping;
  state.m_axisMapping = defaults.m_axisMapping;
}

void PADSetKeyboardActive(const u32 port, const BOOL active) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_keyboardBindings[port].m_mappingsSet = active != FALSE;
}

void PADClearKeyBindings(const u32 port) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_keyboardBindings[port].m_buttonMapping = kUnboundKeys;
  g_keyboardBindings[port].m_axisMapping = kUnboundKeyAxes;
  g_keyboardBindings[port].m_mappingsSet = false;
}

constexpr uint32_t k_keyboardMagic = SBIG('KBND');
constexpr int32_t k_keyboardVersion = 3;

static void load_keyboard_bindings() {
  if (aurora::g_config.userPath == nullptr) {
    return;
  }
  const auto filePath = aurora::io::fs_path_from_string(aurora::g_config.userPath) / "keyboard_bindings.dat";
  auto file = aurora::io::open_file(filePath, "rb");
  if (!file) {
    return;
  }

  uint32_t magic = 0;
  if (!SDL_ReadU32LE(file.get(), &magic) || magic != k_keyboardMagic) {
    Log.warn("keyboard_bindings.dat: invalid magic");
    return;
  }

  uint32_t version = 0;
  if (!SDL_ReadU32LE(file.get(), &version) || version != k_keyboardVersion) {
    Log.warn("keyboard_bindings.dat: version mismatch (expected {}, got {})", k_keyboardVersion, version);
    return;
  }

  Sint64 dataStart = 0;
  if (!seek_aligned(file.get(), dataStart)) {
    Log.warn("keyboard_bindings.dat: unable to seek to bindings");
    return;
  }

  auto bindings = g_keyboardBindings;
  bool ok = true;
  for (uint32_t port = 0; port < bindings.size(); ++port) {
    auto& [buttonMapping, axisMapping, mappingsSet] = bindings[port];
    uint8_t mappingsSetValue = 0;
    ok = ok && SDL_ReadU8(file.get(), &mappingsSetValue) &&
         aurora::io::read_exact(file.get(), buttonMapping.data(), sizeof(buttonMapping)) &&
         aurora::io::read_exact(file.get(), axisMapping.data(), sizeof(axisMapping));
    if (!ok) {
      Log.warn("keyboard_bindings.dat: truncated bindings for port {}", port);
      return;
    }
    mappingsSet = mappingsSetValue != 0;

    bool kbButtonCorrupt = false;
    for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
      if (buttonMapping[i].padButton != kUnboundKeys[i].padButton) {
        kbButtonCorrupt = true;
        break;
      }
    }
    if (kbButtonCorrupt) {
      Log.warn("keyboard_bindings.dat port={}: corrupt button identifiers, resetting to defaults", port);
      buttonMapping = g_defaultKeyboardBindings[port].m_buttonMapping;
    }

    bool kbAxisCorrupt = false;
    for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
      if (axisMapping[i].padAxis != kUnboundKeyAxes[i].padAxis) {
        kbAxisCorrupt = true;
        break;
      }
    }
    if (kbAxisCorrupt) {
      Log.warn("keyboard_bindings.dat port={}: corrupt axis identifiers, resetting to defaults", port);
      axisMapping = g_defaultKeyboardBindings[port].m_axisMapping;
    }

    if (mappingsSet) {
      const bool anyBound =
          std::ranges::any_of(buttonMapping,
                              [](const PADKeyButtonBinding& b) { return b.scancode != PAD_KEY_INVALID; }) ||
          std::ranges::any_of(axisMapping, [](const PADKeyAxisBinding& b) { return b.scancode != PAD_KEY_INVALID; });
      if (!anyBound) {
        mappingsSet = false;
      }
    }
  }
  g_keyboardBindings = std::move(bindings);
}

static void save_keyboard_bindings() {
  if (aurora::g_config.userPath == nullptr) {
    return;
  }
  const auto filePath = aurora::io::fs_path_from_string(aurora::g_config.userPath) / "keyboard_bindings.dat";
  const auto pathString = aurora::io::fs_path_to_string(filePath);
  auto file = aurora::io::open_atomic_file(filePath);
  if (!file) {
    Log.warn("save_keyboard_bindings: failed to open {} for writing: {}", pathString, SDL_GetError());
    return;
  }

  bool ok = SDL_WriteU32LE(file.get(), k_keyboardMagic) && SDL_WriteS32LE(file.get(), k_keyboardVersion);
  Sint64 dataStart = 0;
  ok = ok && seek_aligned(file.get(), dataStart);

  for (const auto& [buttonMapping, axisMapping, mappingsSet] : g_keyboardBindings) {
    ok = ok && SDL_WriteU8(file.get(), mappingsSet) &&
         aurora::io::write_exact(file.get(), buttonMapping.data(), sizeof(buttonMapping)) &&
         aurora::io::write_exact(file.get(), axisMapping.data(), sizeof(axisMapping));
  }
  if (!ok || !file.commit()) {
    Log.warn("save_keyboard_bindings: failed to write {}: {}", pathString, SDL_GetError());
  }
}

void PADSerializeMappings() {
  if (aurora::g_config.userPath == nullptr) {
    return;
  }
  const auto basePath = aurora::io::fs_path_from_string(aurora::g_config.userPath);

  for (auto& controller : aurora::gamepad::g_GameControllers | std::views::values) {
    EnsureMappingLoaded(&controller);
    const auto filePath =
        basePath / fmt::format("{}_{:04X}_{:04X}.controller", aurora::gamepad::controller_name(controller.m_index),
                               controller.m_vid, controller.m_pid);
    const auto filePathStr = aurora::io::fs_path_to_string(filePath);

    auto file = aurora::io::open_atomic_file(filePath, aurora::io::AtomicFileMode::Preserve);
    if (!file) {
      Log.warn("Unable to open controller bindings! Path: \"{}\": {}", filePathStr, SDL_GetError());
      continue;
    }

    // write header
    constexpr uint32_t magic = SBIG('CTRL');
    bool ok = SDL_SeekIO(file.get(), 0, SDL_IO_SEEK_SET) == 0 && SDL_WriteU32LE(file.get(), magic) &&
              SDL_WriteU32LE(file.get(), k_mappingsFileVersion) && SDL_WriteU8(file.get(), controller.m_isGameCube);

    // start writing data at next 32-byte aligned offset
    Sint64 dataStart = 0;
    ok = ok && seek_aligned(file.get(), dataStart);
    if (controller.m_isGameCube) {
      // GameCube adapters expose 4 input devices with the same vid/pid, we store all 4 in the same file
      const auto port = aurora::gamepad::player_index(controller.m_index);
      if (port < 0 || port >= PAD_CHANMAX) {
        Log.warn("Unable to write controller bindings for invalid port {}! Path: \"{}\"", port, filePathStr);
        continue;
      }
      constexpr int64_t dzSecLen = sizeof(PADDeadZones);
      constexpr int64_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
      constexpr int64_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
      // skip to offset in file for this particular port
      const Sint64 portOffset = dataStart + (dzSecLen + btnSecLen + axisSecLen) * port;
      ok = ok && SDL_SeekIO(file.get(), portOffset, SDL_IO_SEEK_SET) == portOffset;
    }
    ok = ok && aurora::io::write_exact(file.get(), &controller.m_deadZones, sizeof(controller.m_deadZones)) &&
         aurora::io::write_exact(file.get(), controller.m_buttonMapping.data(), sizeof(controller.m_buttonMapping)) &&
         aurora::io::write_exact(file.get(), controller.m_axisMapping.data(), sizeof(controller.m_axisMapping));

    if (!controller.m_isGameCube) {
      ok = ok && SDL_WriteU16LE(file.get(), controller.m_rumbleIntensityLow) &&
           SDL_WriteU16LE(file.get(), controller.m_rumbleIntensityHigh) &&
           SDL_WriteU8(file.get(), controller.m_forceDeviceRumble);
    }
    if (!ok || !file.commit()) {
      Log.warn("Unable to write controller bindings! Path: \"{}\": {}", filePathStr, SDL_GetError());
    }
  }

  save_keyboard_bindings();
}

PADDeadZones* PADGetDeadZones(const u32 port) {
  auto* controller = aurora::gamepad::get_controller_for_player(port);
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

const char* PADGetButtonName(const PADButton button) {

  if (const auto iter =
          std::ranges::find_if(skButtonNames, [&button](const auto& pair) { return button == pair.first; });
      iter != skButtonNames.end()) {
    return iter->second.data();
  }

  return nullptr;
}

const char* PADGetNativeButtonName(u32 button) {
  return SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
}

const char* PADGetAxisName(const PADAxis axis) {
  if (const auto it = std::ranges::find_if(skAxisNames, [&axis](const auto& pair) { return axis == pair.first; });
      it != skAxisNames.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetAxisDirectionLabel(const PADAxis axis) {
  if (const auto it = std::ranges::find_if(skAxisDirLabels, [&axis](const auto& pair) { return axis == pair.first; });
      it != skAxisDirLabels.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetNativeAxisName(PADSignedNativeAxis axis) {
  return SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axis.nativeAxis));
}

int32_t PADGetNativeButtonPressed(const u32 port) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return -1;
  }
  const auto source = aurora::pad::detail::controller_source(port);
  if (source == aurora::input::kInvalidSourceId) {
    return -1;
  }

  for (int32_t i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
    if (aurora::input::raw_button_pressed(source, static_cast<SDL_GamepadButton>(i))) {
      return i;
    }
  }
  return -1;
}

PADSignedNativeAxis PADGetNativeAxisPulled(const u32 port) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return {-1, AXIS_SIGN_POSITIVE};
  }
  const auto source = aurora::pad::detail::controller_source(port);
  if (source == aurora::input::kInvalidSourceId) {
    return {-1, AXIS_SIGN_POSITIVE};
  }

  for (int32_t i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
    const float value = aurora::input::raw_axis(source, static_cast<SDL_GamepadAxis>(i));
    if (value >= 0.5f) {
      return {i, AXIS_SIGN_POSITIVE};
    }

    if (value <= -0.5f) {
      // Triggers only report their positive direction.
      if (i == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || i == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        continue;
      }
      return {i, AXIS_SIGN_NEGATIVE};
    }
  }
  return {-1, AXIS_SIGN_POSITIVE};
}

void PADRestoreDefaultMapping(const u32 port) {
  auto* controller = aurora::gamepad::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }
  __PADSetDefaultMapping(controller);
  controller->m_axisMapping = g_defaultAxes;
}

void PADBlockInput(const bool block) { aurora::pad::detail::set_blocked(block); }

SDL_Gamepad* PADGetSDLGamepadForIndex(const u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return ctrl->m_controller;
}

void PADSetDefaultMapping(const PADDefaultMapping* mapping, const PADControllerType type) {
  if (g_initialized) {
    Log.fatal("PADSetDefaultMapping called after PADInit()!");
  }

  switch (type) {
  case PAD_TYPE_STANDARD:
    g_defaultButtonsStandard = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_XBOX360:
    g_defaultButtonsXBox360 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_XBOXONE:
    g_defaultButtonsXBoxOne = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS3:
    g_defaultButtonsPS3 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS4:
    g_defaultButtonsPS4 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS5:
    g_defaultButtonsPS5 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_SWITCH_PROCON:
    g_defaultButtonsProCon = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_LEFT:
    g_defaultButtonsJoyConLeft = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_RIGHT:
    g_defaultButtonsJoyConRight = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_PAIR:
    g_defaultButtonsJoyPair = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_GAMECUBE:
    g_defaultButtonsGamecube = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_NSO_GAMECUBE:
    g_defaultButtonsNSOGamecube = toStdArray(mapping->buttons);
    break;
  default:
    break;
  }
  g_defaultAxes = toStdArray(mapping->axes);
}

BOOL PADSetColor(const u32 port, const u8 red, const u8 green, const u8 blue) {
  const auto ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  ctrl->m_ledRed = red;
  ctrl->m_ledGreen = green;
  ctrl->m_ledBlue = blue;
  ctrl->m_isColorDirty = true;
  return true;
}

BOOL PADGetColor(const u32 port, u8* red, u8* green, u8* blue) {
  const auto ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  *red = ctrl->m_ledRed;
  *green = ctrl->m_ledGreen;
  *blue = ctrl->m_ledBlue;
  return TRUE;
}

BOOL PADSetSensorEnabled(const u32 port, const PADSensorType sensor, const BOOL enabled) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);

  if (controller_has_sensor(ctrl, sensor)) {
    return SDL_SetGamepadSensorEnabled(ctrl->m_controller, static_cast<SDL_SensorType>(sensor), enabled ? true : false)
               ? TRUE
               : FALSE;
  }

  return should_use_device_sensor(port, ctrl, sensor) ? TRUE : FALSE;
}

BOOL PADHasSensor(const u32 port, const PADSensorType sensor) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (controller_has_sensor(ctrl, sensor)) {
    return TRUE;
  }

  return should_use_device_sensor(port, ctrl, sensor) ? TRUE : FALSE;
}

BOOL PADGetSensorData(const u32 port, const PADSensorType sensor, f32* data, const int nValues) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (controller_has_sensor(ctrl, sensor)) {
    return SDL_GetGamepadSensorData(ctrl->m_controller, static_cast<SDL_SensorType>(sensor), data, nValues);
  }

  if (should_use_device_sensor(port, ctrl, sensor)) {
    return get_device_sensor_data(sensor, data, nValues) ? TRUE : FALSE;
  }

  return FALSE;
}

BOOL PADHasLED(const u32 port) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);

  if (ctrl == nullptr) {
    return FALSE;
  }

  return ctrl->m_hasRgbLed;
}

BOOL PADSetRumbleIntensity(const u32 port, const u16 low, const u16 high) {
  auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl != nullptr) {
    if (ctrl->m_isGameCube || (!ctrl->m_hasRumble && !should_use_device_rumble(port, ctrl))) {
      return FALSE;
    }
    EnsureMappingLoaded(ctrl);
    ctrl->m_rumbleIntensityLow = low;
    ctrl->m_rumbleIntensityHigh = high;
    return TRUE;
  }

  if (!should_use_device_rumble(port, nullptr)) {
    return FALSE;
  }
  aurora::gamepad::set_device_rumble_intensity(low, high);
  return TRUE;
}

BOOL PADGetRumbleIntensity(const u32 port, u16* low, u16* high) {
  auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl != nullptr) {
    if (ctrl->m_isGameCube || (!ctrl->m_hasRumble && !should_use_device_rumble(port, ctrl))) {
      *low = 0;
      *high = 0;
      return FALSE;
    }
    EnsureMappingLoaded(ctrl);
    *low = ctrl->m_rumbleIntensityLow;
    *high = ctrl->m_rumbleIntensityHigh;
    return TRUE;
  }

  if (!should_use_device_rumble(port, nullptr)) {
    *low = 0;
    *high = 0;
    return FALSE;
  }

  aurora::gamepad::get_device_rumble_intensity(low, high);
  return TRUE;
}

BOOL PADSupportsRumbleIntensity(const u32 port) {
  if (const auto* ctrl = aurora::gamepad::get_controller_for_player(port)) {
    if (!ctrl->m_isGameCube && (ctrl->m_hasRumble || should_use_device_rumble(port, ctrl))) {
      return TRUE;
    }
    return FALSE;
  }
  return should_use_device_rumble(port, nullptr) ? TRUE : FALSE;
}

BOOL PADCanForceDeviceRumble(const u32 port) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  return ctrl != nullptr && !ctrl->m_isGameCube && ctrl->m_hasRumble && device_rumble_available_for_port(port) ? TRUE
                                                                                                               : FALSE;
}

BOOL PADGetForceDeviceRumble(const u32 port) {
  auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr || !PADCanForceDeviceRumble(port)) {
    return FALSE;
  }

  EnsureMappingLoaded(ctrl);
  return ctrl->m_forceDeviceRumble ? TRUE : FALSE;
}

BOOL PADSetForceDeviceRumble(const u32 port, const BOOL force) {
  auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr || !PADCanForceDeviceRumble(port)) {
    return FALSE;
  }

  EnsureMappingLoaded(ctrl);
  ctrl->m_forceDeviceRumble = force != FALSE;
  return TRUE;
}

BOOL PADIsGCAdapter(const u32 port) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }
  return ctrl->m_isGameCube;
}

PADBatteryState PADGetBatteryState(const u32 port, f32* perc) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return PAD_BATTERYSTATE_ERROR;
  }

  int tmp = 0;
  const auto ret = SDL_GetGamepadPowerInfo(ctrl->m_controller, &tmp);
  if (tmp != -1) {
    *perc = static_cast<float>(tmp) / 100.f;
  } else {
    *perc = static_cast<float>(tmp);
  }
  return static_cast<PADBatteryState>(ret);
}

PADControllerType PADGetControllerType(const u32 port) {
  const auto* ctrl = aurora::gamepad::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return PAD_TYPE_UNKNOWN;
  }

  auto type = SDL_GetGamepadType(ctrl->m_controller);
  return static_cast<PADControllerType>(type);
}

PADControllerType PADGetControllerTypeForIndex(const u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return PAD_TYPE_UNKNOWN;
  }

  auto type = SDL_GetGamepadType(ctrl->m_controller);
  if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && ctrl->m_pid == 0x2073) {
    return PAD_TYPE_NSO_GAMECUBE;
  }
  return static_cast<PADControllerType>(type);
}

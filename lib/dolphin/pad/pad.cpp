#include "../../gamepad.hpp"
#include "../../device.hpp"
#include "../../internal.hpp"
#include "../../io.hpp"
#include <aurora/binding.hpp>
#include <aurora/input.hpp>
#include <aurora/pad.hpp>
#include <dolphin/pad.h>
#include <dolphin/si.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <sys/stat.h>
#include <vector>

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

std::array<PADStatus, PAD_CHANMAX> g_virtualPadStatus{};
std::array<bool, PAD_CHANMAX> g_virtualPadActive{};

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

std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> g_defaultKeys{{
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

std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> g_defaultKeyAxis{{
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

struct PADKeyboardState {
  std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> m_buttonMapping{};
  std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> m_axisMapping{};
  bool m_mappingsSet = false;
};

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
bool g_blockPAD = false;

bool is_mouse_scancode(const s32 scancode) { return scancode < PAD_KEY_INVALID; }

void register_layers();
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

  std::ranges::for_each(g_keyboardBindings, [](auto& state) {
    state.m_buttonMapping = g_defaultKeys;
    state.m_axisMapping = g_defaultKeyAxis;
  });

  if (!g_keyboardBindingsLoaded) {
    g_keyboardBindingsLoaded = true;
    load_keyboard_bindings();
  }

  (void)aurora::pad::controls();
  register_layers();
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

void __PADSetDefaultMapping(aurora::gamepad::GameController* controller) /*  NOLINT(*-reserved-identifier) */
{
  switch (SDL_GetGamepadType(controller->m_controller)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
    controller->m_buttonMapping = g_defaultButtonsXBox360;
    break;
  case SDL_GAMEPAD_TYPE_XBOXONE:
    controller->m_buttonMapping = g_defaultButtonsXBoxOne;
    break;
  case SDL_GAMEPAD_TYPE_STANDARD:
    controller->m_buttonMapping = g_defaultButtonsStandard;
    break;
  case SDL_GAMEPAD_TYPE_PS3:
    controller->m_buttonMapping = g_defaultButtonsPS3;
    break;
  case SDL_GAMEPAD_TYPE_PS4:
    controller->m_buttonMapping = g_defaultButtonsPS4;
    break;
  case SDL_GAMEPAD_TYPE_PS5:
    controller->m_buttonMapping = g_defaultButtonsPS5;
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    if (controller->m_pid == 0x2073) {
      controller->m_buttonMapping = g_defaultButtonsNSOGamecube;
    } else {
      controller->m_buttonMapping = g_defaultButtonsProCon;
    }
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    controller->m_buttonMapping = g_defaultButtonsJoyConRight;
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    controller->m_buttonMapping = g_defaultButtonsJoyConLeft;
    break;
  case SDL_GAMEPAD_TYPE_GAMECUBE:
    controller->m_buttonMapping = g_defaultButtonsGamecube;
    break;
  default:
    controller->m_buttonMapping = g_defaultButtonsStandard;
    break;
  }
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

static bool operator==(const PADSignedNativeAxis& lhs, const PADSignedNativeAxis& rhs) {
  return lhs.nativeAxis == rhs.nativeAxis && lhs.sign == rhs.sign;
}
static bool operator==(const PADButtonMapping& lhs, const PADButtonMapping& rhs) {
  return lhs.nativeButton == rhs.nativeButton && lhs.padButton == rhs.padButton;
}
static bool operator==(const PADAxisMapping& lhs, const PADAxisMapping& rhs) {
  return lhs.nativeAxis == rhs.nativeAxis && lhs.nativeButton == rhs.nativeButton && lhs.padAxis == rhs.padAxis;
}
static bool operator==(const PADDeadZones& lhs, const PADDeadZones& rhs) {
  return lhs.emulateTriggers == rhs.emulateTriggers && lhs.useDeadzones == rhs.useDeadzones &&
         lhs.stickDeadZone == rhs.stickDeadZone && lhs.substickDeadZone == rhs.substickDeadZone &&
         lhs.leftTriggerActivationZone == rhs.leftTriggerActivationZone &&
         lhs.rightTriggerActivationZone == rhs.rightTriggerActivationZone;
}
static bool operator==(const PADKeyButtonBinding& lhs, const PADKeyButtonBinding& rhs) {
  return lhs.scancode == rhs.scancode && lhs.padButton == rhs.padButton;
}
static bool operator==(const PADKeyAxisBinding& lhs, const PADKeyAxisBinding& rhs) {
  return lhs.scancode == rhs.scancode && lhs.padAxis == rhs.padAxis && lhs.influence == rhs.influence;
}

namespace {
using aurora::binding::Binding;
using aurora::binding::BindingSet;
using aurora::binding::ControlId;
using aurora::binding::kInvalidControlId;
using aurora::binding::PhysicalInput;
using aurora::input::kInvalidSourceId;
using aurora::input::SourceId;

constexpr std::array<std::pair<SDL_GamepadButton, PADExtButton>, PAD_EXT_BUTTON_COUNT> kExtButtonMappings{{
    {SDL_GAMEPAD_BUTTON_BACK, PAD_BUTTON_BACK},
    {SDL_GAMEPAD_BUTTON_GUIDE, PAD_BUTTON_GUIDE},
    {SDL_GAMEPAD_BUTTON_MISC1, PAD_BUTTON_MISC1},
    {SDL_GAMEPAD_BUTTON_MISC2, PAD_BUTTON_MISC2},
    {SDL_GAMEPAD_BUTTON_MISC3, PAD_BUTTON_MISC3},
    {SDL_GAMEPAD_BUTTON_MISC4, PAD_BUTTON_MISC4},
    {SDL_GAMEPAD_BUTTON_MISC5, PAD_BUTTON_MISC5},
    {SDL_GAMEPAD_BUTTON_MISC6, PAD_BUTTON_MISC6},
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, PAD_BUTTON_RIGHT_PADDLE1},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, PAD_BUTTON_LEFT_PADDLE1},
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, PAD_BUTTON_RIGHT_PADDLE2},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, PAD_BUTTON_LEFT_PADDLE2},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_BUTTON_RIGHT_STICK},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, PAD_BUTTON_LEFT_STICK},
    {SDL_GAMEPAD_BUTTON_TOUCHPAD, PAD_BUTTON_TOUCHPAD},
}};

constexpr std::array<const char*, PAD_EXT_BUTTON_COUNT> kExtButtonNames{
    "aurora.pad.ext.back",         "aurora.pad.ext.guide",         "aurora.pad.ext.misc1",
    "aurora.pad.ext.misc2",        "aurora.pad.ext.misc3",         "aurora.pad.ext.misc4",
    "aurora.pad.ext.misc5",        "aurora.pad.ext.misc6",         "aurora.pad.ext.right_paddle1",
    "aurora.pad.ext.left_paddle1", "aurora.pad.ext.right_paddle2", "aurora.pad.ext.left_paddle2",
    "aurora.pad.ext.right_stick",  "aurora.pad.ext.left_stick",    "aurora.pad.ext.touchpad",
};

constexpr std::array<std::pair<PADButton, ControlId aurora::pad::Controls::*>, PAD_BUTTON_COUNT> kButtonControls{{
    {PAD_BUTTON_A, &aurora::pad::Controls::a},
    {PAD_BUTTON_B, &aurora::pad::Controls::b},
    {PAD_BUTTON_X, &aurora::pad::Controls::x},
    {PAD_BUTTON_Y, &aurora::pad::Controls::y},
    {PAD_TRIGGER_Z, &aurora::pad::Controls::z},
    {PAD_BUTTON_START, &aurora::pad::Controls::start},
    {PAD_TRIGGER_L, &aurora::pad::Controls::l},
    {PAD_TRIGGER_R, &aurora::pad::Controls::r},
    {PAD_BUTTON_UP, &aurora::pad::Controls::up},
    {PAD_BUTTON_DOWN, &aurora::pad::Controls::down},
    {PAD_BUTTON_LEFT, &aurora::pad::Controls::left},
    {PAD_BUTTON_RIGHT, &aurora::pad::Controls::right},
}};

struct AxisTarget {
  ControlId control = kInvalidControlId;
  float scale = 0.f;
  float deadZone = 0.f;
};

// Half-axis PAD mapping entries become bindings on one full-axis control.
AxisTarget axis_target(u32 padAxis, float stickDeadZone, float substickDeadZone) {
  const auto& c = aurora::pad::controls();
  switch (padAxis) {
  case PAD_AXIS_LEFT_X_POS:
    return {c.leftX, 1.f, stickDeadZone};
  case PAD_AXIS_LEFT_X_NEG:
    return {c.leftX, -1.f, stickDeadZone};
  case PAD_AXIS_LEFT_Y_POS:
    return {c.leftY, 1.f, stickDeadZone};
  case PAD_AXIS_LEFT_Y_NEG:
    return {c.leftY, -1.f, stickDeadZone};
  case PAD_AXIS_RIGHT_X_POS:
    return {c.rightX, 1.f, substickDeadZone};
  case PAD_AXIS_RIGHT_X_NEG:
    return {c.rightX, -1.f, substickDeadZone};
  case PAD_AXIS_RIGHT_Y_POS:
    return {c.rightY, 1.f, substickDeadZone};
  case PAD_AXIS_RIGHT_Y_NEG:
    return {c.rightY, -1.f, substickDeadZone};
  case PAD_AXIS_TRIGGER_L:
    return {c.triggerL, 1.f, 0.f};
  case PAD_AXIS_TRIGGER_R:
    return {c.triggerR, 1.f, 0.f};
  default:
    return {};
  }
}

std::optional<PhysicalInput> controller_input(const PADAxisMapping& mapping, SourceId source) {
  using Direction = PhysicalInput::GamepadAxis::Direction;
  if (mapping.nativeAxis.nativeAxis >= 0 && mapping.nativeAxis.nativeAxis < SDL_GAMEPAD_AXIS_COUNT) {
    return PhysicalInput{
        .source = source,
        .control = PhysicalInput::GamepadAxis{
            .axis = static_cast<SDL_GamepadAxis>(mapping.nativeAxis.nativeAxis),
            .direction = mapping.nativeAxis.sign == AXIS_SIGN_NEGATIVE ? Direction::Negative : Direction::Positive,
        }};
  }
  if (mapping.nativeButton >= 0 && mapping.nativeButton < SDL_GAMEPAD_BUTTON_COUNT) {
    return PhysicalInput{
        .source = source,
        .control = PhysicalInput::GamepadButton{.button = static_cast<SDL_GamepadButton>(mapping.nativeButton)}};
  }
  return std::nullopt;
}

// keyboard_bindings.dat scancodes: non-negative values are SDL scancodes, values
// below PAD_KEY_INVALID are mouse buttons (-2 is SDL_BUTTON_LEFT).
std::optional<PhysicalInput> keyboard_input(s32 scancode) {
  if (scancode > PAD_KEY_INVALID && scancode < SDL_SCANCODE_COUNT) {
    return PhysicalInput{.source = aurora::input::keyboard_source().id,
                         .control = PhysicalInput::Key{.scancode = static_cast<SDL_Scancode>(scancode)}};
  }
  if (const int32_t button = -(scancode + 1); is_mouse_scancode(scancode) && button >= 1 && button <= 5) {
    return PhysicalInput{.source = aurora::input::mouse_source().id,
                         .control = PhysicalInput::MouseButton{.button = static_cast<uint8_t>(button)}};
  }
  return std::nullopt;
}

// Everything a port's binding set is derived from. Mapping arrays are exposed
// through mutable pointers (PADGetButtonMappings, PADGetDeadZones, ...), so the
// adapter compares a snapshot on each use instead of relying on setters.
struct PortInputs {
  SDL_JoystickID instance = 0;
  SourceId controllerSource = kInvalidSourceId;
  bool keyboardActive = false;
  std::array<PADButtonMapping, PAD_BUTTON_COUNT> buttons{};
  std::array<PADAxisMapping, PAD_AXIS_COUNT> axes{};
  PADDeadZones deadZones{};
  std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> keys{};
  std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> keyAxes{};
  uint64_t actionGeneration = 0;

  bool operator==(const PortInputs&) const = default;

  static PortInputs read(u32 port, uint64_t actionGeneration) {
    PortInputs inputs{.actionGeneration = actionGeneration};
    if (auto* controller = aurora::gamepad::get_controller_for_player(port)) {
      EnsureMappingLoaded(controller);
      inputs.instance = aurora::gamepad::get_instance_for_player(port);
      inputs.controllerSource = aurora::input::source_for_gamepad(inputs.instance);
      inputs.buttons = controller->m_buttonMapping;
      inputs.axes = controller->m_axisMapping;
      inputs.deadZones = controller->m_deadZones;
    }
    const auto& keyboard = g_keyboardBindings[port];
    inputs.keyboardActive = keyboard.m_mappingsSet;
    if (inputs.keyboardActive) {
      inputs.keys = keyboard.m_buttonMapping;
      inputs.keyAxes = keyboard.m_axisMapping;
    }
    return inputs;
  }

  // Keys and mouse buttons come from the keyboard and mouse while keyboard mode is
  // active; gamepad controls come from the assigned controller.
  [[nodiscard]] SourceId source_for(const PhysicalInput& input) const {
    if (input.control.is<PhysicalInput::Key>()) {
      return keyboardActive ? aurora::input::keyboard_source().id : kInvalidSourceId;
    }
    if (input.control.is<PhysicalInput::MouseButton>()) {
      return keyboardActive ? aurora::input::mouse_source().id : kInvalidSourceId;
    }
    return controllerSource;
  }

  [[nodiscard]] std::shared_ptr<const BindingSet> resolve(const std::vector<Binding>& actions) const {
    auto set = std::make_shared<BindingSet>();
    auto& out = set->bindings;
    if (controllerSource != kInvalidSourceId) {
      add_controller_bindings(out);
    }
    if (keyboardActive) {
      add_keyboard_bindings(out);
    }
    for (auto binding : actions) {
      binding.input.source = source_for(binding.input);
      bool resolved = binding.input.source != kInvalidSourceId;
      for (auto& held : binding.held) {
        held.input.source = source_for(held.input);
        resolved = resolved && held.input.source != kInvalidSourceId;
      }
      if (resolved) {
        out.push_back(std::move(binding));
      }
    }
    return set;
  }

private:
  void add_controller_bindings(std::vector<Binding>& out) const {
    const auto& c = aurora::pad::controls();
    bool leftTriggerSet = false;
    bool rightTriggerSet = false;
    for (const auto& mapping : buttons) {
      const ControlId target = aurora::pad::control_for_button(mapping.padButton);
      if (target == kInvalidControlId || mapping.nativeButton >= SDL_GAMEPAD_BUTTON_COUNT) {
        continue;
      }
      out.push_back({
          .input =
              {
                  .source = controllerSource,
                  .control = PhysicalInput::GamepadButton{static_cast<SDL_GamepadButton>(mapping.nativeButton)},
              },
          .target = target,
      });
      leftTriggerSet = leftTriggerSet || mapping.padButton == PAD_TRIGGER_L;
      rightTriggerSet = rightTriggerSet || mapping.padButton == PAD_TRIGGER_R;
    }

    const float stickDeadZone = deadZones.useDeadzones ? deadZones.stickDeadZone / 32767.f : 0.f;
    const float substickDeadZone = deadZones.useDeadzones ? deadZones.substickDeadZone / 32767.f : 0.f;
    for (const auto& mapping : axes) {
      const auto target = axis_target(mapping.padAxis, stickDeadZone, substickDeadZone);
      const auto input = controller_input(mapping, controllerSource);
      if (target.control == kInvalidControlId || !input) {
        continue;
      }
      out.push_back({
          .input = *input,
          .target = target.control,
          .deadZone = input->control.is<PhysicalInput::GamepadAxis>() ? target.deadZone : 0.f,
          .scale = target.scale,
      });
    }

    if (deadZones.emulateTriggers) {
      // Analog triggers act as digital L/R past the activation zone when no
      // native button is mapped to them.
      const auto emulate = [&](PADAxis padAxis, ControlId target, u16 zone) {
        const auto mapping = std::ranges::find(axes, padAxis, &PADAxisMapping::padAxis);
        if (const auto input = mapping != axes.end() ? controller_input(*mapping, controllerSource) : std::nullopt) {
          out.push_back({
              .input = *input,
              .target = target,
              .threshold = std::clamp(static_cast<float>(zone + 1) / 32767.f, 1.f / 32767.f, 1.f),
          });
        }
      };
      if (!leftTriggerSet) {
        emulate(PAD_AXIS_TRIGGER_L, c.l, deadZones.leftTriggerActivationZone);
      }
      if (!rightTriggerSet) {
        emulate(PAD_AXIS_TRIGGER_R, c.r, deadZones.rightTriggerActivationZone);
      }
    }

    for (size_t i = 0; i < kExtButtonMappings.size(); ++i) {
      out.push_back({
          .input = {.source = controllerSource,
                    .control = PhysicalInput::GamepadButton{.button = kExtButtonMappings[i].first}},
          .target = c.ext[i],
      });
    }
  }

  void add_keyboard_bindings(std::vector<Binding>& out) const {
    for (const auto& binding : keys) {
      const ControlId target = aurora::pad::control_for_button(binding.padButton);
      if (const auto input = keyboard_input(binding.scancode); target != kInvalidControlId && input) {
        out.push_back({.input = *input, .target = target});
      }
    }
    for (const auto& binding : keyAxes) {
      const auto target = axis_target(binding.padAxis, 0.f, 0.f);
      if (const auto input = keyboard_input(binding.scancode); target.control != kInvalidControlId && input) {
        out.push_back({.input = *input, .target = target.control, .scale = target.scale});
      }
    }
  }
};

s8 to_stick(float value) { return static_cast<s8>(std::clamp(std::lround(value * 127.f), -127l, 127l)); }

u8 to_trigger(float value) { return static_cast<u8>(std::clamp(std::lround(value * 255.f), 0l, 255l)); }

struct PortState {
  PortInputs inputs;
  bool built = false;
  std::shared_ptr<const BindingSet> set;
  uint64_t generation = 0;
  aurora::binding::State gameplay;
  aurora::binding::ProducerId virtualProducer = aurora::binding::kInvalidProducerId;
  bool cancelled = false;
  u32 cancellationCount = 0;
  std::vector<Binding> actions;
  uint64_t actionGeneration = 0;

  // Rebuilds the binding set when the port's configuration changed. Rebuilding
  // cancels old contributions; held inputs need a fresh press.
  void sync(u32 port) {
    auto next = PortInputs::read(port, actionGeneration);
    if (built && next == inputs) {
      return;
    }
    inputs = std::move(next);
    built = true;
    set = inputs.resolve(actions);
    generation = ++s_generation;
    note_cancellations(gameplay.set_bindings(set));
  }

  void process(const aurora::input::InputSource& source, const aurora::input::InputEvent& event) {
    if (set->references(source.id)) {
      note_cancellations(gameplay.process(source, event).changes);
    }
  }

  void note_cancellations(const std::vector<aurora::binding::ControlChange>& changes) {
    if (std::ranges::any_of(changes, [](const auto& change) {
          return change.reason == aurora::binding::ControlChange::Reason::Cancelled;
        })) {
      mark_cancelled();
    }
  }

  void mark_cancelled() {
    cancelled = true;
    ++cancellationCount;
  }

  [[nodiscard]] bool captured() const {
    return std::ranges::any_of(aurora::input::sources(), [&](const aurora::input::InputSource& source) {
      return set->references(source.id) && aurora::input::captured_above(source.id, aurora::input::kGameLayerPriority);
    });
  }

  // The PADSetVirtualStatus shim is one producer per port (null clears it). Like
  // the previous PADBlockInput behavior, virtual input is withheld while touch is
  // captured above gameplay, which includes PADBlockInput itself.
  void apply_virtual_status(const PADStatus* status) {
    if (virtualProducer == aurora::binding::kInvalidProducerId) {
      virtualProducer = gameplay.add_producer("PADSetVirtualStatus");
    }
    if (status == nullptr) {
      gameplay.clear_producer(virtualProducer);
      return;
    }
    if (aurora::input::captured_above(aurora::input::touch_source().id, aurora::input::kGameLayerPriority)) {
      // Withheld input is cancelled rather than released, so release-triggered
      // game actions stay guarded (PADConsumeCancellation).
      if (!gameplay.clear_producer(virtualProducer).empty()) {
        mark_cancelled();
      }
      return;
    }
    const auto& c = aurora::pad::controls();
    for (const auto& [bit, member] : kButtonControls) {
      gameplay.set_value(virtualProducer, c.*member, (status->button & bit) != 0 ? 1.f : 0.f);
    }
    for (size_t i = 0; i < c.ext.size(); ++i) {
      gameplay.set_value(virtualProducer, c.ext[i],
                         (status->extButton & kExtButtonMappings[i].second) != 0 ? 1.f : 0.f);
    }
    gameplay.set_value(virtualProducer, c.leftX, static_cast<float>(status->stickX) / 127.f);
    gameplay.set_value(virtualProducer, c.leftY, static_cast<float>(status->stickY) / 127.f);
    gameplay.set_value(virtualProducer, c.rightX, static_cast<float>(status->substickX) / 127.f);
    gameplay.set_value(virtualProducer, c.rightY, static_cast<float>(status->substickY) / 127.f);
    gameplay.set_value(virtualProducer, c.triggerL, static_cast<float>(status->triggerLeft) / 255.f);
    gameplay.set_value(virtualProducer, c.triggerR, static_cast<float>(status->triggerRight) / 255.f);
  }

  void read(PADStatus& status) const {
    const auto& c = aurora::pad::controls();
    for (const auto& [bit, member] : kButtonControls) {
      if (gameplay.value(c.*member) >= 0.5f) {
        status.button |= bit;
      }
    }
    for (size_t i = 0; i < c.ext.size(); ++i) {
      if (gameplay.value(c.ext[i]) >= 0.5f) {
        status.extButton |= kExtButtonMappings[i].second;
      }
    }
    status.stickX = to_stick(gameplay.value(c.leftX));
    status.stickY = to_stick(gameplay.value(c.leftY));
    status.substickX = to_stick(gameplay.value(c.rightX));
    status.substickY = to_stick(gameplay.value(c.rightY));
    status.triggerLeft = to_trigger(gameplay.value(c.triggerL));
    status.triggerRight = to_trigger(gameplay.value(c.triggerR));

    // If the digital button is activated, set the analog value to max.
    if ((status.button & PAD_TRIGGER_L) != 0) {
      status.triggerLeft = 180;
    }
    if ((status.button & PAD_TRIGGER_R) != 0) {
      status.triggerRight = 180;
    }
  }

  static inline uint64_t s_generation = 0;
};

std::array<PortState, PAD_MAX_CONTROLLERS> g_ports;
aurora::input::LayerId g_padLayer = aurora::input::kInvalidLayerId;
aurora::input::LayerId g_blockLayer = aurora::input::kInvalidLayerId;

PortState& synced_port(u32 port) {
  auto& state = g_ports[port];
  state.sync(port);
  return state;
}

aurora::input::EventResult pad_layer_event(const aurora::input::InputSource& source,
                                           const aurora::input::InputEvent& event, void*) {
  for (u32 port = 0; port < PAD_CHANMAX; ++port) {
    synced_port(port).process(source, event);
  }
  return aurora::input::EventResult::Pass;
}

aurora::input::EventResult block_layer_event(const aurora::input::InputSource&, const aurora::input::InputEvent&,
                                             void*) {
  return aurora::input::EventResult::Pass;
}

bool block_layer_captures(const aurora::input::InputSource&, void*) { return g_blockPAD; }

void register_layers() {
  if (g_padLayer == aurora::input::kInvalidLayerId) {
    g_padLayer = aurora::input::register_layer({
        .label = "aurora.pad",
        .priority = aurora::input::kGameLayerPriority,
        .onEvent = pad_layer_event,
    });
  }
  if (g_blockLayer == aurora::input::kInvalidLayerId) {
    // PADBlockInput compatibility: captures every source just above gameplay.
    // TODO drop PADBlockInput entirely at some point
    g_blockLayer = aurora::input::register_layer({
        .label = "aurora.pad.block",
        .priority = aurora::input::kGameLayerPriority + 1,
        .onEvent = block_layer_event,
        .capturesSource = block_layer_captures,
    });
  }
}
} // namespace

namespace aurora::pad {
const Controls& controls() {
  static const Controls sControls = [] {
    const auto button = [](const char* name) {
      return binding::register_control({.name = name, .kind = binding::ControlKind::Button});
    };
    const auto axis = [](const char* name) {
      return binding::register_control({.name = name, .kind = binding::ControlKind::Axis});
    };
    Controls c;
    c.a = button("aurora.pad.a");
    c.b = button("aurora.pad.b");
    c.x = button("aurora.pad.x");
    c.y = button("aurora.pad.y");
    c.z = button("aurora.pad.z");
    c.start = button("aurora.pad.start");
    c.l = button("aurora.pad.l");
    c.r = button("aurora.pad.r");
    c.up = button("aurora.pad.up");
    c.down = button("aurora.pad.down");
    c.left = button("aurora.pad.left");
    c.right = button("aurora.pad.right");
    c.leftX = axis("aurora.pad.left_x");
    c.leftY = axis("aurora.pad.left_y");
    c.rightX = axis("aurora.pad.right_x");
    c.rightY = axis("aurora.pad.right_y");
    c.triggerL = axis("aurora.pad.trigger_l");
    c.triggerR = axis("aurora.pad.trigger_r");
    for (size_t i = 0; i < c.ext.size(); ++i) {
      c.ext[i] = button(kExtButtonNames[i]);
    }
    return c;
  }();
  return sControls;
}

ControlId control_for_button(PADButton button) {
  const auto& c = controls();
  for (const auto& [bit, member] : kButtonControls) {
    if (bit == button) {
      return c.*member;
    }
  }
  return kInvalidControlId;
}

ControlId control_for_ext_button(PADExtButton button) {
  const auto& c = controls();
  for (size_t i = 0; i < kExtButtonMappings.size(); ++i) {
    if (kExtButtonMappings[i].second == button) {
      return c.ext[i];
    }
  }
  return kInvalidControlId;
}

PortBindings binding_set(uint32_t port) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return {};
  }
  const auto& state = synced_port(port);
  return {.set = state.set, .generation = state.generation};
}

SourceId source(uint32_t port) {
  return port < PAD_MAX_CONTROLLERS ? synced_port(port).inputs.controllerSource : kInvalidSourceId;
}

bool keyboard_active(uint32_t port) { return port < PAD_MAX_CONTROLLERS && g_keyboardBindings[port].m_mappingsSet; }

void set_action_bindings(uint32_t port, std::vector<Binding> bindings) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_ports[port].actions = std::move(bindings);
  ++g_ports[port].actionGeneration;
}

const std::vector<Binding>& action_bindings(uint32_t port) {
  static const std::vector<Binding> sEmpty;
  return port < PAD_MAX_CONTROLLERS ? g_ports[port].actions : sEmpty;
}
} // namespace aurora::pad

u32 PADRead(PADStatus* status) {
  if (!g_initialized) {
    Log.fatal("PADRead called before PADInit()!");
  }

  // Observe UI capture changes made since the last event before sampling.
  aurora::input::reconcile();

  uint32_t rumbleSupport = 0;
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    memset(&status[i], 0, sizeof(PADStatus));
    // Support device rumble on port 0 regardless of whether a controller is connected.
    if (device_rumble_available_for_port(i)) {
      rumbleSupport |= PAD_CHAN0_BIT;
    }
    auto& port = synced_port(i);
    port.apply_virtual_status(g_virtualPadActive[i] ? &g_virtualPadStatus[i] : nullptr);
    auto* controller = aurora::gamepad::get_controller_for_player(i);
    if (controller == nullptr && !port.inputs.keyboardActive && !g_virtualPadActive[i]) {
      status[i].err = PAD_ERR_NO_CONTROLLER;
      continue;
    }

    status[i].err = PAD_ERR_NONE;
    port.read(status[i]);

    if (controller != nullptr) {
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
  if (port >= PAD_CHANMAX || virtualStatus == nullptr) {
    return;
  }

  g_virtualPadStatus[port] = *virtualStatus;
  g_virtualPadStatus[port].err = PAD_ERR_NONE;
  g_virtualPadActive[port] = true;
}

void PADClearVirtualStatus(const u32 port) {
  if (port >= PAD_CHANMAX) {
    return;
  }

  g_virtualPadStatus[port] = {};
  g_virtualPadActive[port] = false;
}

void PADClearAllVirtualStatus() {
  g_virtualPadStatus.fill({});
  g_virtualPadActive.fill(false);
}

BOOL PADConsumeCancellation(const u32 port) {
  if (port >= PAD_CHANMAX) {
    return FALSE;
  }
  return std::exchange(synced_port(port).cancelled, false) ? TRUE : FALSE;
}

u32 PADGetCancellationCount(const u32 port) { return port < PAD_CHANMAX ? synced_port(port).cancellationCount : 0; }

BOOL PADIsInputCaptured(const u32 port) {
  if (port >= PAD_CHANMAX) {
    return FALSE;
  }
  return synced_port(port).captured() ? TRUE : FALSE;
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
  g_keyboardBindings[port].m_buttonMapping = g_defaultKeys;
  g_keyboardBindings[port].m_axisMapping = g_defaultKeyAxis;
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
      if (buttonMapping[i].padButton != g_defaultKeys[i].padButton) {
        kbButtonCorrupt = true;
        break;
      }
    }
    if (kbButtonCorrupt) {
      Log.warn("keyboard_bindings.dat port={}: corrupt button identifiers, resetting to defaults", port);
      buttonMapping = g_defaultKeys;
    }

    bool kbAxisCorrupt = false;
    for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
      if (axisMapping[i].padAxis != g_defaultKeyAxis[i].padAxis) {
        kbAxisCorrupt = true;
        break;
      }
    }
    if (kbAxisCorrupt) {
      Log.warn("keyboard_bindings.dat port={}: corrupt axis identifiers, resetting to defaults", port);
      axisMapping = g_defaultKeyAxis;
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
  const auto source = synced_port(port).inputs.controllerSource;
  if (source == kInvalidSourceId) {
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
  const auto source = synced_port(port).inputs.controllerSource;
  if (source == kInvalidSourceId) {
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

void PADBlockInput(const bool block) {
  g_blockPAD = block;
  aurora::input::reconcile();
}

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

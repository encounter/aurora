#include "../../input/port.hpp"
#include <aurora/pad.hpp>
#include "../../gamepad.hpp"
#include "../../device.hpp"
#include "../../internal.hpp"
#include "../../io.hpp"
#include <aurora/input.hpp>
#include <dolphin/pad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace {
constexpr aurora::Module Log{"aurora::pad"};

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

std::optional<aurora::input::SensorKind> sensor_kind(PADSensorType sensor) {
  if (sensor < PAD_SENSOR_ACCEL || sensor > PAD_SENSOR_GYRO_RIGHT) {
    return std::nullopt;
  }
  return static_cast<aurora::input::SensorKind>(sensor - PAD_SENSOR_ACCEL);
}

aurora::input::SourceId sensor_source(u32 port, aurora::input::SensorKind sensor) {
  const auto source = aurora::pad::port_controller(port);
  if (aurora::input::has_sensor(source, sensor)) {
    return source;
  }
  return port == 0 ? aurora::input::device_source().id : 0;
}

} // namespace

void PADSetSpec(u32 spec [[maybe_unused]]) {}

static bool device_rumble_available_for_port(const u32 port) {
  return port == PAD_CHAN0 && aurora::device::rumble_available();
}

static bool should_use_device_rumble(const u32 port, const aurora::gamepad::GameController* controller) {
  return device_rumble_available_for_port(port) &&
         (controller == nullptr ||
          (!controller->m_isGameCube && (!controller->m_hasRumble || controller->m_forceDeviceRumble)));
}

BOOL PADInit() {
  if (!g_initialized) {
    aurora::pad::detail::initialize_profiles();
    aurora::pad::detail::initialize();
    g_initialized = true;
  }
  return TRUE;
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

  aurora::pad::set_port_controller(port, aurora::input::source_for_gamepad(SDL_GetGamepadID(ctrl->m_controller)));
  aurora::pad::save_profiles();
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

void PADClearPort(u32 port) {
  aurora::pad::clear_port(port);
  aurora::pad::save_profiles();
}

static void EnsureMappingLoaded(aurora::gamepad::GameController* controller) {
  const auto source = aurora::input::source_for_gamepad(controller->m_index);
  if (const auto* profile = aurora::pad::profile(aurora::pad::ProfileTarget::controller(source))) {
    controller->m_rumbleIntensityLow = static_cast<u16>(profile->settings.rumbleIntensityLow * 65535.f);
    controller->m_rumbleIntensityHigh = static_cast<u16>(profile->settings.rumbleIntensityHigh * 65535.f);
    controller->m_forceDeviceRumble = profile->settings.forceDeviceRumble;
  }
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
  if (controller != nullptr) {
    EnsureMappingLoaded(controller);
  }

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

void PADBlockInput(const bool block) { aurora::pad::detail::set_blocked(block); }

SDL_Gamepad* PADGetSDLGamepadForIndex(const u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return ctrl->m_controller;
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

BOOL PADSetSensorEnabled(u32 port, PADSensorType sensor, BOOL enabled) {
  const auto kind = sensor_kind(sensor);
  return port < 4 && kind && aurora::input::set_sensor_enabled(sensor_source(port, *kind), *kind, enabled != FALSE);
}

BOOL PADHasSensor(u32 port, PADSensorType sensor) {
  const auto kind = sensor_kind(sensor);
  return port < 4 && kind && aurora::input::has_sensor(sensor_source(port, *kind), *kind);
}

BOOL PADGetSensorData(u32 port, PADSensorType sensor, f32* data, int count) {
  if (port >= 4 || data == nullptr || count <= 0 || PADIsInputCaptured(port)) {
    return FALSE;
  }

  const auto kind = sensor_kind(sensor);
  if (!kind) {
    return FALSE;
  }

  const auto sample = aurora::input::raw_sensor(sensor_source(port, *kind), *kind);
  if (!sample) {
    return FALSE;
  }

  std::copy_n(sample->values.begin(), std::min(count, 3), data);
  return TRUE;
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
    auto target = aurora::pad::ProfileTarget::controller(aurora::input::source_for_gamepad(ctrl->m_index));
    auto settings = aurora::pad::profile(target)->settings;
    settings.rumbleIntensityLow = static_cast<float>(low) / 65535.f;
    settings.rumbleIntensityHigh = static_cast<float>(high) / 65535.f;
    aurora::pad::set_settings(target, settings);
    aurora::pad::save_profiles();
    return TRUE;
  }

  if (!should_use_device_rumble(port, nullptr)) {
    return FALSE;
  }

  aurora::gamepad::set_device_rumble_intensity(low, high);
  aurora::pad::save_profiles();
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
  auto target = aurora::pad::ProfileTarget::controller(aurora::input::source_for_gamepad(ctrl->m_index));
  auto settings = aurora::pad::profile(target)->settings;
  settings.forceDeviceRumble = force != FALSE;
  aurora::pad::set_settings(target, settings);
  aurora::pad::save_profiles();
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

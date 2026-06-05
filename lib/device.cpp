#include "device.hpp"

#include <algorithm>
#include <string_view>

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_video.h>

namespace aurora::device {
namespace detail {
void shutdown_rumble() noexcept;
}

namespace {
SDL_Sensor* g_gyro = nullptr;
SDL_Sensor* g_accel = nullptr;
#if defined(SDL_PLATFORM_ANDROID)
SDL_Haptic* g_haptic = nullptr;

constexpr std::string_view kAndroidDeviceHapticName = "VIBRATOR_SERVICE";
constexpr uint32_t kIndefiniteRumbleDurationMs = 30000;

float rumble_strength(const uint16_t low, const uint16_t high) noexcept {
  const float mixed = static_cast<float>(low) * 0.6f + static_cast<float>(high) * 0.4f;
  return std::clamp(mixed / 65535.0f, 0.0f, 1.0f);
}

bool open_haptic() noexcept {
  if (g_haptic != nullptr) {
    return true;
  }

  int count = 0;
  SDL_HapticID* haptics = SDL_GetHaptics(&count);
  if (haptics == nullptr) {
    return false;
  }

  SDL_HapticID selected = 0;
  for (int i = 0; i < count; ++i) {
    const char* name = SDL_GetHapticNameForID(haptics[i]);
    if (name != nullptr && std::string_view{name} == kAndroidDeviceHapticName) {
      selected = haptics[i];
      break;
    }
  }
  SDL_free(haptics);

  if (selected == 0) {
    return false;
  }

  g_haptic = SDL_OpenHaptic(selected);
  if (g_haptic == nullptr) {
    return false;
  }
  if (!SDL_HapticRumbleSupported(g_haptic) || !SDL_InitHapticRumble(g_haptic)) {
    SDL_CloseHaptic(g_haptic);
    g_haptic = nullptr;
    return false;
  }

  return true;
}
#endif

SDL_Sensor* open_sensor(const SDL_SensorType type, SDL_Sensor*& sensor) noexcept {
  if (sensor != nullptr) {
    return sensor;
  }

  int count = 0;
  SDL_SensorID* sensors = SDL_GetSensors(&count);
  if (sensors == nullptr) {
    return nullptr;
  }

  SDL_SensorID selected = 0;
  for (int i = 0; i < count; ++i) {
    if (SDL_GetSensorTypeForID(sensors[i]) == type) {
      selected = sensors[i];
      break;
    }
  }
  SDL_free(sensors);

  if (selected == 0) {
    return nullptr;
  }

  sensor = SDL_OpenSensor(selected);
  return sensor;
}

int orientation_quarter_turns(const SDL_DisplayOrientation orientation) noexcept {
  switch (orientation) {
  case SDL_ORIENTATION_PORTRAIT:
    return 0;
  case SDL_ORIENTATION_LANDSCAPE:
    return 1;
  case SDL_ORIENTATION_PORTRAIT_FLIPPED:
    return 2;
  case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
    return 3;
  default:
    return -1;
  }
}

void rotate_sensor_to_display(float* data, const int n_values) noexcept {
  if (n_values < 2) {
    return;
  }

  const SDL_DisplayID display = SDL_GetPrimaryDisplay();
  if (display == 0) {
    return;
  }

  const int natural = orientation_quarter_turns(SDL_GetNaturalDisplayOrientation(display));
  const int current = orientation_quarter_turns(SDL_GetCurrentDisplayOrientation(display));
  if (natural < 0 || current < 0) {
    return;
  }

  const float x = data[0];
  const float y = data[1];
  switch ((current - natural + 4) % 4) {
  case 1:
    data[0] = -y;
    data[1] = x;
    break;
  case 2:
    data[0] = -x;
    data[1] = -y;
    break;
  case 3:
    data[0] = y;
    data[1] = -x;
    break;
  default:
    break;
  }
}

bool read_sensor_data(const SDL_SensorType type, SDL_Sensor*& cached_sensor, float* data, const int n_values) noexcept {
  if (data == nullptr || n_values <= 0) {
    return false;
  }

  SDL_Sensor* sensor = open_sensor(type, cached_sensor);
  if (sensor == nullptr) {
    return false;
  }

  SDL_UpdateSensors();
  if (!SDL_GetSensorData(sensor, data, n_values)) {
    return false;
  }
  rotate_sensor_to_display(data, n_values);
  return true;
}

void close_sensor(SDL_Sensor*& sensor) noexcept {
  if (sensor != nullptr) {
    SDL_CloseSensor(sensor);
    sensor = nullptr;
  }
}
} // namespace

#if defined(SDL_PLATFORM_ANDROID)
bool rumble_available() noexcept { return open_haptic(); }

void rumble(const uint16_t low_freq_intensity, const uint16_t high_freq_intensity,
            const uint16_t duration_ms) noexcept {
  if (!open_haptic()) {
    return;
  }

  const float strength = rumble_strength(low_freq_intensity, high_freq_intensity);
  if (strength <= 0.0f) {
    SDL_StopHapticRumble(g_haptic);
    return;
  }

  SDL_PlayHapticRumble(g_haptic, strength,
                       duration_ms == 0 ? kIndefiniteRumbleDurationMs : static_cast<uint32_t>(duration_ms));
}

namespace detail {
void shutdown_rumble() noexcept {
  if (g_haptic != nullptr) {
    SDL_StopHapticRumble(g_haptic);
    SDL_CloseHaptic(g_haptic);
    g_haptic = nullptr;
  }
}
} // namespace detail
#elif !defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
bool rumble_available() noexcept { return false; }

void rumble(const uint16_t lowFreq, const uint16_t highFreq, const uint16_t durationMs) noexcept {
  static_cast<void>(lowFreq);
  static_cast<void>(highFreq);
  static_cast<void>(durationMs);
}

namespace detail {
void shutdown_rumble() noexcept {}
} // namespace detail
#endif

bool gyro_available() noexcept { return open_sensor(SDL_SENSOR_GYRO, g_gyro) != nullptr; }

bool gyro(float* data, const int n_values) noexcept {
  return read_sensor_data(SDL_SENSOR_GYRO, g_gyro, data, n_values);
}

bool accel_available() noexcept { return open_sensor(SDL_SENSOR_ACCEL, g_accel) != nullptr; }

bool accel(float* data, const int n_values) noexcept {
  return read_sensor_data(SDL_SENSOR_ACCEL, g_accel, data, n_values);
}

void shutdown() noexcept {
  detail::shutdown_rumble();
  close_sensor(g_gyro);
  close_sensor(g_accel);
}
} // namespace aurora::device

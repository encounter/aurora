#include "phone_haptics.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>
#include <algorithm>

namespace aurora::input {

namespace {
constexpr uint32_t kContinuousRumbleMs = 30000;

bool g_enabled = false;
PhoneHapticsIntensity g_intensity;

#if defined(SDL_PLATFORM_ANDROID)
SDL_Haptic* g_haptic = nullptr;
bool g_checkedHaptics = false;

SDL_Haptic* get_phone_haptic() {
  if (g_checkedHaptics) {
    return g_haptic;
  }

  g_checkedHaptics = true;

  int hapticCount = 0;
  SDL_HapticID* haptics = SDL_GetHaptics(&hapticCount);
  for (int i = 0; i < hapticCount; ++i) {
    SDL_Haptic* haptic = SDL_OpenHaptic(haptics[i]);
    if (haptic == nullptr) {
      continue;
    }

    if (SDL_HapticRumbleSupported(haptic) && SDL_InitHapticRumble(haptic)) {
      g_haptic = haptic;
      break;
    }

    SDL_CloseHaptic(haptic);
  }
  SDL_free(haptics);

  return g_haptic;
}

float rumble_strength(uint16_t low_freq_intensity, uint16_t high_freq_intensity) {
  const auto intensity = std::max(low_freq_intensity, high_freq_intensity);
  return static_cast<float>(intensity) / static_cast<float>(UINT16_MAX);
}

uint32_t rumble_duration(uint16_t duration_ms) { return duration_ms == 0 ? kContinuousRumbleMs : duration_ms; }
#endif
} // namespace

bool phone_haptics_supported() noexcept {
#if defined(SDL_PLATFORM_ANDROID)
  return get_phone_haptic() != nullptr;
#else
  return false;
#endif
}

bool phone_haptics_enabled() noexcept { return g_enabled; }

void set_phone_haptics_enabled(bool enabled) noexcept {
  g_enabled = enabled;
  if (!g_enabled) {
    phone_haptics_stop();
  }
}

void set_phone_haptics_intensity(PhoneHapticsIntensity intensity) noexcept { g_intensity = intensity; }

PhoneHapticsIntensity get_phone_haptics_intensity() noexcept { return g_intensity; }

void phone_haptics_rumble(PhoneHapticsIntensity intensity, uint16_t duration_ms) noexcept {
  phone_haptics_rumble(intensity.low_freq, intensity.high_freq, duration_ms);
}

void phone_haptics_rumble(uint16_t low_freq_intensity, uint16_t high_freq_intensity, uint16_t duration_ms) noexcept {
#if defined(SDL_PLATFORM_ANDROID)
  if (!g_enabled) {
    return;
  }

  SDL_Haptic* haptic = get_phone_haptic();
  if (haptic == nullptr) {
    return;
  }

  const float strength = rumble_strength(low_freq_intensity, high_freq_intensity);
  if (strength <= 0.0f) {
    SDL_StopHapticRumble(haptic);
    return;
  }

  SDL_PlayHapticRumble(haptic, strength, rumble_duration(duration_ms));
#endif
}

void phone_haptics_stop() noexcept {
#if defined(SDL_PLATFORM_ANDROID)
  if (g_haptic != nullptr) {
    SDL_StopHapticRumble(g_haptic);
  }
#endif
}

} // namespace aurora::input

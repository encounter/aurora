#import <CoreHaptics/CoreHaptics.h>

#include "phone_haptics.hpp"
#include <algorithm>

namespace aurora::input {

namespace {
constexpr NSTimeInterval kContinuousRumbleSeconds = 30.0;

bool g_enabled = false;
PhoneHapticsIntensity g_intensity;
CHHapticEngine *g_engine = nil;
id<CHHapticPatternPlayer> g_player = nil;

bool supports_haptics() {
  if (@available(iOS 13.0, *)) {
    return [CHHapticEngine capabilitiesForHardware].supportsHaptics;
  }
  return false;
}

bool start_engine() {
  if (!supports_haptics()) {
    return false;
  }

  if (g_engine == nil) {
    NSError *error = nil;
    g_engine = [[CHHapticEngine alloc] initAndReturnError:&error];
    if (g_engine == nil || error != nil) {
      g_engine = nil;
      return false;
    }
  }

  NSError *error = nil;
  if (![g_engine startAndReturnError:&error]) {
    return false;
  }
  return error == nil;
}

float intensity_value(uint16_t low_freq_intensity, uint16_t high_freq_intensity) {
  const auto intensity = std::max(low_freq_intensity, high_freq_intensity);
  return static_cast<float>(intensity) / static_cast<float>(UINT16_MAX);
}

float sharpness_value(uint16_t low_freq_intensity, uint16_t high_freq_intensity) {
  const auto total = static_cast<uint32_t>(low_freq_intensity) + static_cast<uint32_t>(high_freq_intensity);
  if (total == 0) {
    return 0.5f;
  }
  return static_cast<float>(high_freq_intensity) / static_cast<float>(total);
}

NSTimeInterval rumble_duration(uint16_t duration_ms) {
  if (duration_ms == 0) {
    return kContinuousRumbleSeconds;
  }
  return static_cast<NSTimeInterval>(duration_ms) / 1000.0;
}
} // namespace

bool phone_haptics_supported() noexcept {
  @autoreleasepool {
    return supports_haptics();
  }
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
  @autoreleasepool {
    if (!g_enabled || !start_engine()) {
      return;
    }

    const float intensity = intensity_value(low_freq_intensity, high_freq_intensity);
    if (intensity <= 0.0f) {
      phone_haptics_stop();
      return;
    }

    NSError *error = nil;
    NSArray<CHHapticEventParameter *> *parameters = @[
      [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:intensity],
      [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                    value:sharpness_value(low_freq_intensity, high_freq_intensity)],
    ];
    CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                         parameters:parameters
                                                       relativeTime:0.0
                                                           duration:rumble_duration(duration_ms)];
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ] parameters:@[] error:&error];
    if (pattern == nil || error != nil) {
      return;
    }

    [g_player stopAtTime:0 error:nil];
    g_player = [g_engine createPlayerWithPattern:pattern error:&error];
    if (g_player == nil || error != nil) {
      return;
    }

    [g_player startAtTime:0 error:nil];
  }
}

void phone_haptics_stop() noexcept {
  @autoreleasepool {
    [g_player stopAtTime:0 error:nil];
    g_player = nil;
  }
}

} // namespace aurora::input

#pragma once

#include <cstdint>

namespace aurora::input {

struct PhoneHapticsIntensity {
  uint16_t low_freq = 32767;
  uint16_t high_freq = 32767;
};

bool phone_haptics_supported() noexcept;
bool phone_haptics_enabled() noexcept;
void set_phone_haptics_enabled(bool enabled) noexcept;
void set_phone_haptics_intensity(PhoneHapticsIntensity intensity) noexcept;
PhoneHapticsIntensity get_phone_haptics_intensity() noexcept;
void phone_haptics_rumble(PhoneHapticsIntensity intensity, uint16_t duration_ms) noexcept;
void phone_haptics_rumble(uint16_t low_freq_intensity, uint16_t high_freq_intensity, uint16_t duration_ms) noexcept;
void phone_haptics_stop() noexcept;

} // namespace aurora::input

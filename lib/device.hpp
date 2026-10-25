#pragma once

#include <SDL3/SDL_sensor.h>

#include <cstdint>

namespace aurora::device {

bool rumble_available() noexcept;
void rumble(uint16_t lowFreq, uint16_t highFreq, uint16_t durationMs) noexcept;

bool gyro_available() noexcept;
bool gyro(float* data, int n_values) noexcept;

bool accel_available() noexcept;
bool accel(float* data, int n_values) noexcept;

SDL_Sensor* sensor(SDL_SensorType type) noexcept;
void rotate_sensor(float* data, int count) noexcept;
void shutdown() noexcept;

} // namespace aurora::device

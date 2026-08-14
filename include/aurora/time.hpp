#pragma once

#include <chrono>
#include <cstdint>

namespace aurora::time {

inline constexpr float kMaximumTimeScale = 16.0f;

class native_clock {
public:
  using rep = int64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<native_clock>;

  static constexpr bool is_steady = true;

  static time_point now() noexcept;
};

class game_clock {
public:
  using rep = int64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<game_clock>;

  static constexpr bool is_steady = false;

  static time_point now() noexcept;
};

using wall_clock = std::chrono::system_clock;

/** Sets the clock timescale. Default 1.0f. 0.0f is paused. Range 0.0f-kMaximumTimeScale. */
void set_scale(float scale) noexcept;

float scale() noexcept;

} // namespace aurora::time

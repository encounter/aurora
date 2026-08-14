#pragma once

#include <aurora/time.hpp>

#include <cstdint>

namespace aurora::time::internal {

enum class PauseReason : uint32_t {
  Window = 1u << 0,
  Surface = 1u << 1,
  Background = 1u << 2,
};

void set_pause_reason(PauseReason reason, bool paused) noexcept;

using NowFunction = native_clock::time_point (*)() noexcept;

// Test hooks
void set_now_function(NowFunction function) noexcept;
void reset() noexcept;

} // namespace aurora::time::internal

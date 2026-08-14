#include <aurora/time.hpp>

#include "time_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <shared_mutex>

namespace aurora::time {
namespace {

native_clock::time_point default_native_now() noexcept {
  const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
  return native_clock::time_point{std::chrono::duration_cast<native_clock::duration>(elapsed)};
}

std::atomic<internal::NowFunction> s_nativeNow{default_native_now};

struct ClockState {
  std::shared_mutex mutex;
  native_clock::time_point nativeAnchor = default_native_now();
  game_clock::time_point gameAnchor;
  double requestedScale = 1.0;
  uint32_t pauseReasons = 0;
};

ClockState& state() {
  static ClockState clockState;
  return clockState;
}

native_clock::time_point get_native_now() noexcept { return s_nativeNow.load(std::memory_order_acquire)(); }

game_clock::time_point game_now_locked(const ClockState& clockState,
                                       const native_clock::time_point nativeNow) noexcept {
  if (clockState.pauseReasons != 0 || clockState.requestedScale == 0.0) {
    return clockState.gameAnchor;
  }

  return clockState.gameAnchor +
         std::chrono::duration_cast<game_clock::duration>(std::chrono::duration<double, std::nano>{
             (nativeNow - clockState.nativeAnchor).count() * clockState.requestedScale});
}

void rebase_locked(ClockState& clockState, const native_clock::time_point nativeNow) noexcept {
  clockState.gameAnchor = game_now_locked(clockState, nativeNow);
  clockState.nativeAnchor = nativeNow;
}

} // namespace

native_clock::time_point native_clock::now() noexcept { return get_native_now(); }

game_clock::time_point game_clock::now() noexcept {
  auto& clockState = state();
  std::shared_lock lock{clockState.mutex};
  return game_now_locked(clockState, get_native_now());
}

void set_scale(const float scale) noexcept {
  if (!std::isfinite(scale) || scale < 0.0f) {
    return;
  }

  auto& clockState = state();
  std::unique_lock lock{clockState.mutex};
  const double clampedScale = std::min(static_cast<double>(scale), static_cast<double>(kMaximumTimeScale));
  if (clockState.requestedScale == clampedScale) {
    return;
  }
  const auto nativeNow = get_native_now();
  rebase_locked(clockState, nativeNow);
  clockState.requestedScale = clampedScale;
}

float scale() noexcept {
  auto& clockState = state();
  std::shared_lock lock{clockState.mutex};
  return static_cast<float>(clockState.requestedScale);
}

namespace internal {

void set_pause_reason(const PauseReason reason, const bool paused) noexcept {
  auto& clockState = state();
  std::unique_lock lock{clockState.mutex};
  const auto mask = static_cast<uint32_t>(reason);
  if (((clockState.pauseReasons & mask) != 0) == paused) {
    return;
  }

  const auto nativeNow = get_native_now();
  rebase_locked(clockState, nativeNow);
  if (paused) {
    clockState.pauseReasons |= mask;
  } else {
    clockState.pauseReasons &= ~mask;
  }
}

void set_now_function(const NowFunction function) noexcept {
  s_nativeNow.store(function != nullptr ? function : default_native_now, std::memory_order_release);
  reset();
}

void reset() noexcept {
  auto& clockState = state();
  std::unique_lock lock{clockState.mutex};
  clockState.nativeAnchor = get_native_now();
  clockState.gameAnchor = {};
  clockState.requestedScale = 1.0;
  clockState.pauseReasons = 0;
}

} // namespace internal
} // namespace aurora::time

#include "../lib/time_internal.hpp"

#include <aurora/time.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <limits>

namespace {

using namespace std::chrono_literals;

aurora::time::native_clock::time_point s_now{};

aurora::time::native_clock::time_point fake_now() noexcept { return s_now; }

class TimeTest : public testing::Test {
protected:
  void SetUp() override {
    s_now = {};
    aurora::time::internal::set_now_function(fake_now);
  }

  void TearDown() override { aurora::time::internal::set_now_function(nullptr); }
};

TEST_F(TimeTest, ScalesGameTimeWithoutScalingNativeTime) {
  s_now += 1s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 1s);
  EXPECT_EQ(aurora::time::native_clock::now().time_since_epoch(), 1s);

  aurora::time::set_scale(4.0f);
  s_now += 2s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 9s);
  EXPECT_EQ(aurora::time::native_clock::now().time_since_epoch(), 3s);
}

TEST_F(TimeTest, ScaleChangesPreserveContinuity) {
  s_now += 2s;
  aurora::time::set_scale(4.0f);
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 2s);

  s_now += 1s;
  aurora::time::set_scale(0.5f);
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 6s);

  s_now += 2s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 7s);
}

TEST_F(TimeTest, ExplicitAndLifecyclePausesAreIndependent) {
  aurora::time::set_scale(4.0f);
  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Window, true);
  s_now += 3s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 0s);
  EXPECT_FLOAT_EQ(aurora::time::scale(), 4.0f);

  aurora::time::set_scale(0.0f);
  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Window, false);
  s_now += 1s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 0s);

  aurora::time::set_scale(2.0f);
  s_now += 1s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 2s);
}

TEST_F(TimeTest, MultiplePauseReasonsMustAllClear) {
  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Window, true);
  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Surface, true);
  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Window, false);
  s_now += 1s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 0s);

  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Surface, false);
  s_now += 1s;
  EXPECT_EQ(aurora::time::game_clock::now().time_since_epoch(), 1s);
}

TEST_F(TimeTest, InvalidScalesAreIgnoredAndLargeScalesAreClamped) {
  aurora::time::set_scale(-1.0f);
  EXPECT_FLOAT_EQ(aurora::time::scale(), 1.0f);
  aurora::time::set_scale(std::numeric_limits<float>::quiet_NaN());
  EXPECT_FLOAT_EQ(aurora::time::scale(), 1.0f);

  aurora::time::set_scale(aurora::time::kMaximumTimeScale * 2.0f);
  EXPECT_FLOAT_EQ(aurora::time::scale(), aurora::time::kMaximumTimeScale);
}

} // namespace

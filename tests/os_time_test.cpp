#include "../lib/time_internal.hpp"

#include <aurora/time.hpp>
#include <dolphin/os.h>
#include <gtest/gtest.h>

#include <chrono>

namespace {

using namespace std::chrono_literals;

aurora::time::native_clock::time_point s_now{};

aurora::time::native_clock::time_point fake_now() noexcept { return s_now; }

class GameOSTimeTest : public testing::Test {
protected:
  void SetUp() override {
    s_now = {};
    aurora::time::internal::set_now_function(fake_now);
  }

  void TearDown() override { aurora::time::internal::set_now_function(nullptr); }
};

TEST(OSTimeTest, SystemTimeUsesUtcGcnEpoch) {
  constexpr auto gcnEpoch = 946684800s;
  const auto expected =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch() - gcnEpoch);
  const auto actual = std::chrono::seconds{OSTicksToSeconds(OSGetSystemTime())};

  EXPECT_LE(std::chrono::abs(actual - expected), 1s);
}

TEST(OSTimeTest, CalendarConversionRoundTripsLocalTime) {
  OSCalendarTime input{
      .sec = 56,
      .min = 34,
      .hour = 12,
      .mday = 29,
      .mon = 1,
      .year = 2024,
      .msec = 123,
      .usec = 456,
  };

  OSCalendarTime output{};
  OSTicksToCalendarTime(OSCalendarTimeToTicks(&input), &output);

  EXPECT_EQ(output.sec, input.sec);
  EXPECT_EQ(output.min, input.min);
  EXPECT_EQ(output.hour, input.hour);
  EXPECT_EQ(output.mday, input.mday);
  EXPECT_EQ(output.mon, input.mon);
  EXPECT_EQ(output.year, input.year);
  EXPECT_EQ(output.msec, input.msec);
  EXPECT_EQ(output.usec, input.usec);
  EXPECT_EQ(output.yday, 59);
}

TEST_F(GameOSTimeTest, StopsWhilePausedAndRespectsScale) {
  const OSTime start = OSGetTime();

  s_now += 1s;
  EXPECT_EQ(OSTicksToSeconds(OSGetTime() - start), 1);

  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Background, true);
  s_now += 10s;
  EXPECT_EQ(OSTicksToSeconds(OSGetTime() - start), 1);

  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Background, false);
  aurora::time::set_scale(4.0f);
  s_now += 1s;
  EXPECT_EQ(OSTicksToSeconds(OSGetTime() - start), 5);
}

} // namespace

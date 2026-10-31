#include "../lib/time_internal.hpp"

#include <aurora/time.hpp>
#include <dolphin/os.h>
#include <gtest/gtest.h>

#include <chrono>
#include <ctime>

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

TEST(OSTimeTest, TimeUsesLocalGcnEpoch) {
  const std::time_t now = std::time(nullptr);
  std::tm localTm{};
#if defined(_WIN32)
  localtime_s(&localTm, &now);
#else
  localtime_r(&now, &localTm);
#endif
  const auto local =
      std::chrono::local_days{std::chrono::year{localTm.tm_year + 1900} / (localTm.tm_mon + 1) / localTm.tm_mday} +
      std::chrono::hours{localTm.tm_hour} + std::chrono::minutes{localTm.tm_min} + std::chrono::seconds{localTm.tm_sec};
  const auto expected = local - std::chrono::local_days{std::chrono::year{2000} / 1 / 1};
  const auto actual = std::chrono::seconds{OSTicksToSeconds(OSGetTime())};

  EXPECT_LE(std::chrono::abs(actual - expected), 1s);
}

TEST(OSTimeTest, CalendarConversionIgnoresTimeZone) {
  OSCalendarTime output{};
  OSTicksToCalendarTime(0, &output);

  EXPECT_EQ(output.sec, 0);
  EXPECT_EQ(output.min, 0);
  EXPECT_EQ(output.hour, 0);
  EXPECT_EQ(output.mday, 1);
  EXPECT_EQ(output.mon, 0);
  EXPECT_EQ(output.year, 2000);

  OSCalendarTime input{
      .sec = 0,
      .min = 0,
      .hour = 0,
      .mday = 2,
      .mon = 0,
      .year = 2000,
  };
  EXPECT_EQ(OSCalendarTimeToTicks(&input), OSSecondsToTicks(86400LL));
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

#include <chrono>
#include <ctime>
#include <mutex>

#include "internal.hpp"
#include <aurora/time.hpp>
#include <dolphin/os.h>

namespace chrono = std::chrono;
using namespace std::literals::chrono_literals;

using SystemDuration = chrono::system_clock::duration;
using SystemTime = chrono::time_point<chrono::system_clock>;
using LocalTime = chrono::local_time<SystemDuration>;

namespace {
// GCN epoch: 2000-01-01 00:00:00 UTC = 946684800 seconds after Unix epoch
constexpr SystemTime kGcnEpochUnix{946684800s};
std::once_flag s_gameEpochOnce;
OSTime s_gameEpochOffset;

LocalTime system_time_to_local_time(SystemTime time) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
  return chrono::zoned_time(chrono::current_zone(), time).get_local_time();
#else
  // Apple libc++ currently ships <chrono> with the C++20 timezone database API disabled
  // (_LIBCPP_HAS_TIME_ZONE_DATABASE == 0), so zoned_time/current_zone are unavailable there.
  const auto wholeSeconds = chrono::floor<chrono::seconds>(time);
  const auto fractionalSeconds = chrono::duration_cast<SystemDuration>(time - wholeSeconds);
  const std::time_t wallClock = chrono::system_clock::to_time_t(time);
  std::tm localTm{};

#if defined(_WIN32)
  const errno_t result = localtime_s(&localTm, &wallClock);
  AURORA_ASSERT(result == 0, "localtime_s failed in system_time_to_local_time");
#else
  const std::tm* result = localtime_r(&wallClock, &localTm);
  AURORA_ASSERT(result != nullptr, "localtime_r failed in system_time_to_local_time");
#endif

  const auto localDate = chrono::local_days{chrono::year{localTm.tm_year + 1900} /
                                            chrono::month{static_cast<unsigned>(localTm.tm_mon + 1)} /
                                            chrono::day{static_cast<unsigned>(localTm.tm_mday)}};
  const auto localTimeOfDay =
      chrono::hours{localTm.tm_hour} + chrono::minutes{localTm.tm_min} + chrono::seconds{localTm.tm_sec};
  return LocalTime{chrono::duration_cast<SystemDuration>(localDate.time_since_epoch()) +
                   chrono::duration_cast<SystemDuration>(localTimeOfDay) + fractionalSeconds};
#endif
}

SystemTime local_time_to_system_time(LocalTime time) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
  return chrono::zoned_time(chrono::current_zone(), time).get_sys_time();
#else
  // Apple libc++ currently ships <chrono> with the C++20 timezone database API disabled
  // (_LIBCPP_HAS_TIME_ZONE_DATABASE == 0), so zoned_time/current_zone are unavailable there.
  const auto& localDays = chrono::floor<chrono::days>(time);
  const chrono::year_month_day ymd{localDays};
  const chrono::hh_mm_ss hms{chrono::floor<chrono::microseconds>(time - localDays)};

  std::tm localTm{};
  localTm.tm_sec = hms.seconds().count();
  localTm.tm_min = hms.minutes().count();
  localTm.tm_hour = hms.hours().count();
  localTm.tm_mday = static_cast<unsigned int>(ymd.day());
  localTm.tm_mon = static_cast<unsigned int>(ymd.month()) - 1;
  localTm.tm_year = static_cast<int>(ymd.year()) - 1900;
  localTm.tm_isdst = -1;

  const std::time_t utcTime = mktime(&localTm);

  AURORA_ASSERT(utcTime != -1, "mktime failed in local_time_to_system_time");
  static_assert(std::is_same_v<std::chrono::microseconds, decltype(hms)::precision>,
                "hms precision must be in microseconds");

  return chrono::system_clock::from_time_t(utcTime) + hms.subseconds();
#endif
}

template <typename Duration>
OSTime duration_to_ticks(const Duration duration) {
  const auto seconds = chrono::floor<chrono::seconds>(duration);
  const auto nanoseconds = chrono::duration_cast<chrono::nanoseconds>(duration - seconds);
  return seconds.count() * static_cast<OSTime>(OS_TIMER_CLOCK) +
         nanoseconds.count() * static_cast<OSTime>(OS_TIMER_CLOCK) / 1000000000LL;
}

void initialize_game_epoch() {
  s_gameEpochOffset = OSGetSystemTime() - duration_to_ticks(aurora::time::game_clock::now().time_since_epoch());
}
} // namespace

OSTick OSGetTick() { return OSGetTime() & 0xFFFFFFFF; }

OSTime OSGetTime() {
  std::call_once(s_gameEpochOnce, initialize_game_epoch);
  return s_gameEpochOffset + duration_to_ticks(aurora::time::game_clock::now().time_since_epoch());
}

OSTime OSGetNativeTime() { return duration_to_ticks(aurora::time::native_clock::now().time_since_epoch()); }

void AuroraInitClock() {
  std::call_once(s_gameEpochOnce, initialize_game_epoch);
  if (OSBaseAddress == 0) {
    return;
  }

  __OSBusClock = OS_TIMER_CLOCK * OS_TIMER_CLOCK_DIVIDER;
}

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td) {
  // We assume that all input times (ticks) are in UTC, relative to GCN epoch
  // So convert that to the local time
  const LocalTime local =
      system_time_to_local_time(SystemTime{chrono::microseconds{OSTicksToMicroseconds(ticks)} + kGcnEpochUnix});

  // Break up the time into the components we want
  const auto localDays = chrono::floor<chrono::days>(local);
  const chrono::year_month_weekday ymwd{localDays};
  const chrono::year_month_day ymd{localDays};
  const chrono::hh_mm_ss hms{chrono::floor<chrono::microseconds>(local - localDays)};

  td->sec = hms.seconds().count();
  td->min = hms.minutes().count();
  td->hour = hms.hours().count();
  td->mday = static_cast<unsigned int>(ymd.day());
  td->mon = static_cast<unsigned int>(ymd.month()) - 1;
  td->year = static_cast<int>(ymd.year());
  td->wday = ymwd.weekday().c_encoding();
  td->yday = (chrono::local_days{ymd} - chrono::local_days{ymd.year() / 1 / 1}).count();

  static_assert(std::is_same_v<std::chrono::microseconds, decltype(hms)::precision>,
                "hms precision must be in microseconds");
  td->msec = std::chrono::duration_cast<chrono::milliseconds>(hms.subseconds()).count();
  td->usec =
      std::chrono::duration_cast<chrono::microseconds>(hms.subseconds() - chrono::milliseconds{td->msec}).count();

  AURORA_ASSERT(0 <= td->usec, "0 <= td->usec");
  AURORA_ASSERT(0 <= td->msec, "0 <= td->msec");
  AURORA_ASSERT(0 <= td->sec, "0 <= td->sec");
}

OSTime OSCalendarTimeToTicks(OSCalendarTime* td) {
  const LocalTime& local =
      chrono::local_days{chrono::year{td->year} / chrono::month{static_cast<unsigned int>(td->mon) + 1} /
                         chrono::day{static_cast<unsigned int>(td->mday)}} +
      chrono::hours{td->hour} + chrono::minutes{td->min} + chrono::seconds{td->sec} + chrono::milliseconds{td->msec} +
      chrono::microseconds{td->usec};

  const SystemTime sys = local_time_to_system_time(local);

  return OSMicrosecondsToTicks(chrono::duration_cast<chrono::microseconds>(sys - kGcnEpochUnix).count());
}

OSTime OSGetSystemTime() { return duration_to_ticks(aurora::time::wall_clock::now() - kGcnEpochUnix); }

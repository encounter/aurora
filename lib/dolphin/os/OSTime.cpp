#include <chrono>

#include "internal.hpp"
#include <dolphin/os.h>

static const int YearDays[MONTH_MAX] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

static const int LeapYearDays[MONTH_MAX] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

namespace chrono = std::chrono;
using TickDuration = chrono::duration<s64, std::ratio<1, OS_TIMER_CLOCK>>;

static chrono::time_point<chrono::system_clock> startupTime = chrono::system_clock::now();
static chrono::time_point<chrono::steady_clock> startupSteadyTime = chrono::steady_clock::now();

OSTick OSGetTick() {
    return OSGetTime() & 0xFFFFFFFF;
}

OSTime OSGetTime() {
    // System time is provided in the number of timer ticks since 2000-01-01 00:00:00

    using Decananoseconds = chrono::duration<s64, std::ratio<1, 100'000'000>>;

    static const auto gcnEpoch = chrono::local_days{chrono::year{2000} / chrono::January / 1};

    auto startupOffset = chrono::steady_clock::now() - startupSteadyTime;
    auto nowTime = chrono::zoned_time(std::chrono::current_zone(), startupTime + startupOffset).get_local_time();
    auto clockTime = nowTime - gcnEpoch;

    // The current time is around the order of 1e17 ns, which is about 1/22 of INT64_MAX. The
    // effective ratio of ticks/nanoseconds is 81/200, which means if we try to convert directly
    // from ns to ticks we cause an overflow when we multiply by 81. As a workaround, we first
    // convert to units of 10 ns. This brings the effective multiplicand from 81 down to 8.1 which
    // should be good for the next 300 or so years, at which point chrono::system_clock::now() would
    // have broken already anyway.
    auto ticksTotal = chrono::duration_cast<TickDuration>(chrono::duration_cast<Decananoseconds>(clockTime));
    return ticksTotal.count();
}

void AuroraInitClock() {
  if (OSBaseAddress == 0) {
    return;
  }

  __OSBusClock = OS_TIMER_CLOCK * OS_TIMER_CLOCK_DIVIDER;
}


static int IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int GetYearDays(int year, int mon) {
    const int* md = (IsLeapYear(year)) ? LeapYearDays : YearDays;

    return md[mon];
}

static int GetLeapDays(int year) {
    ASSERT(0 <= year);
    
    if (year < 1) {
        return 0;
    }
    return (year + 3) / 4 - (year - 1) / 100 + (year - 1) / 400;
}

static void GetDates(int days, OSCalendarTime* td) {
    int year;
    int n;
    int month;
    const int* md;

    ASSERT(0 <= days);

    td->wday = (days + 6) % WEEK_DAY_MAX;

    for (year = days / YEAR_DAY_MAX;
         days < (n = year * YEAR_DAY_MAX + GetLeapDays(year)); year--) {
        ;
    }

    days -= n;
    td->year = year;
    td->yday = days;

    md = IsLeapYear(year) ? LeapYearDays : YearDays;
    for (month = MONTH_MAX; days < md[--month];) {
        ;
    }
    td->mon = month;
    td->mday = days - md[month] + 1;
}

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td) {
    int days;
    int secs;
    OSTime d;

    d = ticks % OS_SEC_TO_TICKS(1);    
    if (d < 0) {
        d += OS_SEC_TO_TICKS(1);
        ASSERT(0 <= d);
    }

    td->usec = OS_TICKS_TO_USEC(d) % USEC_MAX;
    td->msec = OS_TICKS_TO_MSEC(d) % MSEC_MAX;

    ASSERT(0 <= td->usec);
    ASSERT(0 <= td->msec);

    ticks -= d;

    ASSERT(ticks % OSSecondsToTicks(1) == 0);
    ASSERT(0 <= OSTicksToSeconds(ticks) / 86400 + BIAS && OSTicksToSeconds(ticks) / 86400 + BIAS <= INT_MAX);

    days = (OS_TICKS_TO_SEC(ticks) / SECS_IN_DAY) + BIAS;    
    secs = OS_TICKS_TO_SEC(ticks) % SECS_IN_DAY;
    if (secs < 0) {
        days -= 1;
        secs += SECS_IN_DAY;
        ASSERT(0 <= secs);
    }

    GetDates(days, td);
    td->hour = secs / 60 / 60;
    td->min = secs / 60 % 60;
    td->sec = secs % 60;
}

OSTime OSCalendarTimeToTicks(OSCalendarTime* td) {
    OSTime secs;
    int ov_mon;
    int mon;
    int year;

    ov_mon = td->mon / MONTH_MAX;
    mon = td->mon - (ov_mon * MONTH_MAX);

    if (mon < 0) {
        mon += MONTH_MAX;
        ov_mon--;
    }

    ASSERT((ov_mon <= 0 && 0 <= td->year + ov_mon) || (0 < ov_mon && td->year <= INT_MAX - ov_mon));
    
    year = td->year + ov_mon;

    secs = (OSTime)SECS_IN_YEAR * year +
           (OSTime)SECS_IN_DAY * (GetLeapDays(year) + GetYearDays(year, mon) + td->mday - 1) +
           (OSTime)SECS_IN_HOUR * td->hour +
           (OSTime)SECS_IN_MIN * td->min +
           td->sec -
           (OSTime)0xEB1E1BF80ULL;

    return OS_SEC_TO_TICKS(secs) + OS_MSEC_TO_TICKS((OSTime)td->msec) +
           OS_USEC_TO_TICKS((OSTime)td->usec);
}

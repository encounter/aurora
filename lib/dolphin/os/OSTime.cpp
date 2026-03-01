#include <chrono>

#include "internal.hpp"
#include <dolphin/os.h>

namespace chrono = std::chrono;
using TickDuration = chrono::duration<s64, std::ratio<1, OS_REAL_TIMER_CLOCK>>;

OSTick OSGetTick() {
    return OSGetTime() & 0xFFFFFFFF;
}

OSTime OSGetTime() {
    auto clockTime = chrono::steady_clock::now().time_since_epoch();
    auto ticksTotal = chrono::duration_cast<TickDuration>(clockTime);
    return ticksTotal.count();
}

void AuroraInitClock() {
  if (OSBaseAddress == 0) {
    return;
  }

  OS_BUS_CLOCK = OS_REAL_TIMER_CLOCK * OS_TIMER_CLOCK_DIVIDER;
}

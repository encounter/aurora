#include <dolphin/os.h>

void OSInitStopwatch(OSStopwatch* sw, char* name) {
  sw->name = name;
  sw->total = 0;
  sw->hits = 0;
  sw->min = 0x00000000FFFFFFFF;
  sw->max = 0;
}

void OSStartStopwatch(OSStopwatch* sw) {
  sw->running = TRUE;
  sw->last = OSGetTime();
}

void OSStopStopwatch(OSStopwatch* sw) {
  if (!sw->running) {
    return;
  }

  const OSTime interval = OSGetTime() - sw->last;
  sw->total += interval;
  sw->running = FALSE;
  sw->hits++;
  if (sw->max < interval) {
    sw->max = interval;
  }
  if (interval < sw->min) {
    sw->min = interval;
  }
}

OSTime OSCheckStopwatch(OSStopwatch* sw) {
  OSTime currTotal = sw->total;
  if (sw->running) {
    currTotal += OSGetTime() - sw->last;
  }
  return currTotal;
}

void OSResetStopwatch(OSStopwatch* sw) { OSInitStopwatch(sw, sw->name); }

void OSDumpStopwatch(OSStopwatch* sw) {
  OSReport("Stopwatch [%s]\t:\n", sw->name);
  OSReport("\tTotal= %lld us\n", static_cast<long long>(OSTicksToMicroseconds(sw->total)));
  OSReport("\tHits = %u \n", sw->hits);
  OSReport("\tMin  = %lld us\n", static_cast<long long>(OSTicksToMicroseconds(sw->min)));
  OSReport("\tMax  = %lld us\n", static_cast<long long>(OSTicksToMicroseconds(sw->max)));
  const OSTime mean = sw->hits != 0 ? sw->total / sw->hits : 0;
  OSReport("\tMean = %lld us\n", static_cast<long long>(OSTicksToMicroseconds(mean)));
}

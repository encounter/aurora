#ifndef _DOLPHIN_OSSTOPWATCH_H_
#define _DOLPHIN_OSSTOPWATCH_H_

#include <dolphin/types.h>
#include <dolphin/os/OSTime.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OSStopwatch {
    char* name;
    u32 hits;
    OSTime total;
    OSTime min;
    OSTime max;
    OSTime last;
    BOOL running;
    u32 _padding;
} OSStopwatch;

void OSInitStopwatch(OSStopwatch* sw, char* name);
void OSStartStopwatch(OSStopwatch* sw);
void OSStopStopwatch(OSStopwatch* sw);
OSTime OSCheckStopwatch(OSStopwatch* sw);
void OSResetStopwatch(OSStopwatch* sw);
void OSDumpStopwatch(OSStopwatch* sw);

#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_OSSTOPWATCH_H_

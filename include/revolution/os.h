#pragma once

#include <dolphin/os.h>

#ifdef __cplusplus
extern "C" {
#endif

void* OSGetMEM1ArenaLo(void);
void* OSGetMEM1ArenaHi(void);
void OSSetMEM1ArenaLo(void* newLo);
void OSSetMEM1ArenaHi(void* newHi);
void* OSAllocFromMEM1ArenaLo(u32 size, u32 align);
void* OSAllocFromMEM1ArenaHi(u32 size, u32 align);
void* OSGetMEM2ArenaLo(void);
void* OSGetMEM2ArenaHi(void);
void OSSetMEM2ArenaLo(void* newLo);
void OSSetMEM2ArenaHi(void* newHi);
void* OSAllocFromMEM2ArenaLo(u32 size, u32 align);
void* OSAllocFromMEM2ArenaHi(u32 size, u32 align);
BOOL OSIsMEM1Region(const void* addr);
BOOL OSIsMEM2Region(const void* addr);

#ifdef __cplusplus
}
#endif

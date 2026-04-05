#ifndef _DOLPHIN_OSALLOC_H_
#define _DOLPHIN_OSALLOC_H_

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int OSHeapHandle;

extern volatile OSHeapHandle __OSCurrHeap;

void* OSAllocFromHeap(OSHeapHandle heap, u32 size);
void* OSAllocFixed(void* rstart, void* rend);
void OSFreeToHeap(OSHeapHandle heap, void* ptr);
OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap);
void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps);
OSHeapHandle OSCreateHeap(void* start, void* end);
void OSDestroyHeap(OSHeapHandle heap);
void OSAddToHeap(OSHeapHandle heap, void* start, void* end);
s32 OSCheckHeap(OSHeapHandle heap);
u32 OSReferentSize(void* ptr);
void OSDumpHeap(OSHeapHandle heap);
void OSVisitAllocated(void (*visitor)(void*, u32));

#define OSAlloc(size) OSAllocFromHeap(__OSCurrHeap, (size))
#define OSFree(ptr) OSFreeToHeap(__OSCurrHeap, (ptr))

#ifdef __cplusplus
}
#endif

#endif

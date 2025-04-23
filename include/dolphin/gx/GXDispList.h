#ifndef DOLPHIN_GXDISPLIST_H
#define DOLPHIN_GXDISPLIST_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef AURORA
#define GXCallDisplayListNative GXCallDisplayListLE
#else
#define GXCallDisplayListNative GXCallDisplayList
#endif

void GXBeginDisplayList(void* list, u32 size);
u32 GXEndDisplayList(void);
void GXCallDisplayListLE(const void* list, u32 nbytes);
void GXCallDisplayList(const void* list, u32 nbytes);

#ifdef __cplusplus
}
#endif

#endif

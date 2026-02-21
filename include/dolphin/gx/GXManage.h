#ifndef DOLPHIN_GXMANAGE_H
#define DOLPHIN_GXMANAGE_H

#include <dolphin/gx/GXFifo.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GXDrawDoneCallback)(void);

GXFifoObj* GXInit(void* base, u32 size);
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb);
void GXDrawDone(void);
void GXSetDrawDone(void);
void GXFlush(void);
void GXPixModeSync(void);
void GXTexModeSync(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef DOLPHIN_VI_H
#define DOLPHIN_VI_H

#include <dolphin/gx/GXStruct.h>

#ifdef __cplusplus
extern "C" {
#endif

void VIInit(void);
void VIConfigure(GXRenderModeObj *rm);
void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height);
void VIFlush(void);

void VIWaitForRetrace(void);

u32 VIGetTvFormat(void);
u32 VIGetRetraceCount(void);
u32 VIGetNextField(void);
u32 VIGetDTVStatus(void);

void VISetNextFrameBuffer(void *fb);
void VISetBlack(BOOL black);

typedef void (*VIRetraceCallback)(u32 retraceCount);

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb);
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb);

#ifdef TARGET_PC
void VISetWindowTitle(const char* title);
void VISetWindowFullscreen(bool fullscreen);
bool VIGetWindowFullscreen();
#endif

#ifdef __cplusplus
}
#endif

#endif

#ifndef DOLPHIN_VI_H
#define DOLPHIN_VI_H

#include <dolphin/gx/GXStruct.h>
#include <dolphin/vifuncs.h>

#ifdef __cplusplus
extern "C" {
#endif

void VIInit(void);
void VIConfigure(GXRenderModeObj *rm);
void VIFlush(void);
u32 VIGetTvFormat(void);
void VISetNextFrameBuffer(void *fb);
void VIWaitForRetrace(void);
void VISetBlack(BOOL black);

#ifdef TARGET_PC
void VISetWindowTitle(const char* title);
void VISetWindowFullscreen(bool fullscreen);
bool VIGetWindowFullscreen();
#endif

#ifdef __cplusplus
}
#endif

#endif

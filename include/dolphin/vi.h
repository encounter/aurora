#ifndef DOLPHIN_VI_H
#define DOLPHIN_VI_H

#include <dolphin/gx/GXStruct.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*VIRetraceCallback)(u32 retraceCount);

void VIInit(void);
void VIConfigure(const GXRenderModeObj *rm);
void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height);
void VIFlush(void);

void VIWaitForRetrace(void);

u32 VIGetTvFormat(void);
u32 VIGetRetraceCount(void);
u32 VIGetNextField(void);
u32 VIGetDTVStatus(void);

void* VIGetCurrentFrameBuffer(void);
void* VIGetNextFrameBuffer(void);
void VISetNextFrameBuffer(void *fb);
void VISetBlack(BOOL black);

typedef void (*VIRetraceCallback)(u32 retraceCount);

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb);
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb);

#ifdef TARGET_PC
void VISetWindowTitle(const char* title);
void VISetWindowFullscreen(bool fullscreen);
bool VIGetWindowFullscreen();
void VISetWindowSize(uint32_t width, uint32_t height);
void VISetWindowPosition(uint32_t x, uint32_t y);
void VICenterWindow();

/**
 * Sets the internal framebuffer resolution to a specific scale factor of the configured EFB size.
 * A value of 0.0f means "Auto", which will use the underlying swapchain size (usually the window size).
 */
void VISetFrameBufferScale(float scale);

/**
 * \brief Lock the GX framebuffer to a specific aspect ratio, without changing the native framebuffer.
 *
 * @param width Width part of the aspect ratio fraction.
 * @param height Height part of the aspect ratio fraction.
 */
void VILockAspectRatio(int width, int height);

/**
 * \brief Undoes a previous call to VILockAspectRatio.
 */
void VIUnlockAspectRatio();
#endif

#ifdef __cplusplus
}
#endif

#endif

#ifndef DOLPHIN_GDAURORA_H
#define DOLPHIN_GDAURORA_H

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 *
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 * Debug groups are *not* automatically scoped to a display list.
 */
void GDPushDebugGroup(const char* label);

/**
 * Pop a debug group previously pushed via GXPushDebugGroup().
 */
void GDPopDebugGroup();

/**
 * Sends a debug marker to the backend graphics API. These may show in debugging tools such as RenderDoc.
 */
void GDInsertDebugMarker(const char* label);

void GDLoadTexObj(const GXTexObj* obj, GXTexMapID id);

void GDLoadTlut(const GXTlutObj* obj, u32 idx);

#endif // DOLPHIN_GDAURORA_H

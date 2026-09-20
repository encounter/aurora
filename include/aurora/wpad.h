#ifndef AURORA_WPAD_H
#define AURORA_WPAD_H

#include <dolphin/mtx/GeoTypes.h>
#include <revolution/wpad.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call on the input thread, as with PAD. aurora_update samples automatically. */
void aurora_wpad_update(void);
void aurora_wpad_shutdown(void);
/* Select the emulated extension independently of the host controller type. */
BOOL aurora_wpad_set_device(s32 chan, u32 device);
/* Re-enable a channel disabled by WPADDisconnect. Physical reconnection also re-enables it. */
void aurora_wpad_reconnect(s32 chan);

/* Screen coordinates: (-1,-1) is top-left, (1,1) is bottom-right. NULL restores mouse input on port 0. */
typedef struct AuroraWpadPointer {
	f32 x, y;
	BOOL valid;
} AuroraWpadPointer;

void aurora_wpad_set_pointer(s32 chan, const AuroraWpadPointer* pointer);
/* Acceleration in g, in KPAD axes (stationary face-up: 0,-1,0). NULL restores host sensors. */
void aurora_wpad_set_acceleration(s32 chan, const Vec* core, const Vec* nunchuk);

#ifdef __cplusplus
}
#endif
#endif

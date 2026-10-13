#ifndef REVOLUTION_KPAD_H
#define REVOLUTION_KPAD_H

#include <dolphin/mtx/GeoTypes.h>
#include <revolution/wpad.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KPAD_BUTTON_MASK 0x0000ffff
#define KPAD_BUTTON_RPT 0x80000000

typedef struct Vec2 {
	f32 x, y;
} Vec2;

typedef WPADCallback KPADCallback;
typedef WPADChannel KPADChannel;
typedef WPADCallback KPADControlDpdCallback;

typedef union KPADEXStatus {
	struct {
		Vec2 stick;
		Vec acc;
		f32 acc_value, acc_speed;
	} fs;

	struct {
		u32 hold, trig, release;
		Vec2 lstick, rstick;
		f32 ltrigger, rtrigger;
	} cl;
} KPADEXStatus;

typedef struct KPADStatus {
	u32 hold, trig, release;
	Vec acc;
	f32 acc_value, acc_speed;
	Vec2 pos, vec;
	f32 speed;
	Vec2 horizon, hori_vec;
	f32 hori_speed;
	f32 dist, dist_vec, dist_speed;
	Vec2 acc_vertical;
	u8 dev_type;
	s8 wpad_err, dpd_valid_fg;
	u8 data_format;
	KPADEXStatus ex_status;
} KPADStatus;

void KPADInit(void);
void KPADReset(void);
s32 KPADRead(s32 chan, KPADStatus* samples, u32 count);
void KPADSetBtnRepeat(s32 chan, f32 delay, f32 pulse);
void KPADSetSensorHeight(s32 chan, f32 height);
void KPADSetPosParam(s32 chan, f32 radius, f32 sensitivity);
void KPADSetHoriParam(s32 chan, f32 radius, f32 sensitivity);
void KPADSetDistParam(s32 chan, f32 radius, f32 sensitivity);

#ifdef __cplusplus
}
#endif
#endif

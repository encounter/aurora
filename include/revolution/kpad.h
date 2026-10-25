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
  f32 x;
  f32 y;
} Vec2;

typedef WPADCallback KPADCallback;
typedef WPADChannel KPADChannel;
typedef WPADCallback KPADControlDpdCallback;

typedef union KPADEXStatus {
  struct {
    Vec2 stick;
    Vec acc;
    f32 acc_value;
    f32 acc_speed;
  } fs;

  struct {
    u32 hold;
    u32 trig;
    u32 release;
    Vec2 lstick;
    Vec2 rstick;
    f32 ltrigger;
    f32 rtrigger;
  } cl;
} KPADEXStatus;

typedef struct KPADStatus {
  u32 hold;
  u32 trig;
  u32 release;
  Vec acc;
  f32 acc_value;
  f32 acc_speed;
  Vec2 pos;
  Vec2 vec;
  f32 speed;
  Vec2 horizon;
  Vec2 hori_vec;
  f32 hori_speed;
  f32 dist;
  f32 dist_vec;
  f32 dist_speed;
  Vec2 acc_vertical;
  u8 dev_type;
  s8 wpad_err;
  s8 dpd_valid_fg;
  u8 data_format;
  KPADEXStatus ex_status;
} KPADStatus;

typedef struct KPADUnifiedWpadStatus {
  union {
    WPADStatus core;
    WPADFSStatus fs;
    WPADCLStatus cl;
  } u;
  u8 fmt;
  u8 padding;
} KPADUnifiedWpadStatus;

void KPADGetUnifiedWpadStatus(s32 chan, KPADUnifiedWpadStatus* status, u32 count);
void KPADDisableAimingMode(s32 chan);
void KPADSetAccParam(s32 chan, f32 radius, f32 sensitivity);
void KPADSetObjInterval(s32 chan, f32 interval);
void KPADSetFSStickClamp(s8 min, s8 max);
void KPADEnableDPD(s32 chan);
void KPADDisableDPD(s32 chan);
void KPADInit(void);
void KPADReset(void);
s32 KPADRead(s32 chan, KPADStatus* samples, s32 count);
void KPADSetBtnRepeat(s32 chan, f32 delay, f32 pulse);
void KPADSetSensorHeight(s32 chan, f32 height);
void KPADSetPosParam(s32 chan, f32 radius, f32 sensitivity);
void KPADSetHoriParam(s32 chan, f32 radius, f32 sensitivity);
void KPADSetDistParam(s32 chan, f32 radius, f32 sensitivity);

#ifdef __cplusplus
}
#endif
#endif

#ifndef REVOLUTION_WPAD_H
#define REVOLUTION_WPAD_H

#include <revolution/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WPAD_CHAN0 0
#define WPAD_CHAN1 1
#define WPAD_CHAN2 2
#define WPAD_CHAN3 3
#define WPAD_MAX_CONTROLLERS 4
#define WPAD_MOTOR_STOP 0
#define WPAD_MOTOR_RUMBLE 1
#define WPAD_BUTTON_LEFT 0x0001
#define WPAD_BUTTON_RIGHT 0x0002
#define WPAD_BUTTON_DOWN 0x0004
#define WPAD_BUTTON_UP 0x0008
#define WPAD_BUTTON_PLUS 0x0010
#define WPAD_BUTTON_2 0x0100
#define WPAD_BUTTON_1 0x0200
#define WPAD_BUTTON_B 0x0400
#define WPAD_BUTTON_A 0x0800
#define WPAD_BUTTON_MINUS 0x1000
#define WPAD_BUTTON_Z 0x2000
#define WPAD_BUTTON_C 0x4000
#define WPAD_BUTTON_HOME 0x8000
#define WPAD_CL_BUTTON_UP 0x0001
#define WPAD_CL_BUTTON_LEFT 0x0002
#define WPAD_CL_TRIGGER_ZR 0x0004
#define WPAD_CL_BUTTON_X 0x0008
#define WPAD_CL_BUTTON_A 0x0010
#define WPAD_CL_BUTTON_Y 0x0020
#define WPAD_CL_BUTTON_B 0x0040
#define WPAD_CL_TRIGGER_ZL 0x0080
#define WPAD_CL_TRIGGER_R 0x0200
#define WPAD_CL_BUTTON_PLUS 0x0400
#define WPAD_CL_BUTTON_HOME 0x0800
#define WPAD_CL_BUTTON_MINUS 0x1000
#define WPAD_CL_TRIGGER_L 0x2000
#define WPAD_CL_BUTTON_DOWN 0x4000
#define WPAD_CL_BUTTON_RIGHT 0x8000
#define WPAD_DPD_MAX_OBJECTS 4
#define WPAD_DPD_IMG_RESO_WX 1024
#define WPAD_DPD_IMG_RESO_WY 768
#define WPAD_DEV_CORE 0
#define WPAD_DEV_FREESTYLE 1
#define WPAD_DEV_CLASSIC 2
#define WPAD_DEV_NOT_SUPPORTED 252
#define WPAD_DEV_NOT_FOUND 253
#define WPAD_DEV_UNKNOWN 255
#define WPAD_FMT_CORE 0
#define WPAD_FMT_CORE_ACC 1
#define WPAD_FMT_CORE_ACC_DPD 2
#define WPAD_FMT_FREESTYLE 3
#define WPAD_FMT_FREESTYLE_ACC 4
#define WPAD_FMT_FREESTYLE_ACC_DPD 5
#define WPAD_FMT_CLASSIC 6
#define WPAD_FMT_CLASSIC_ACC 7
#define WPAD_FMT_CLASSIC_ACC_DPD 8
#define WPAD_FMT_CORE_ACC_DPD_FULL 9
#define WPAD_STATE_DISABLED 0
#define WPAD_STATE_ENABLING 1
#define WPAD_STATE_ENABLED 2
#define WPAD_STATE_SETUP 3
#define WPAD_STATE_DISABLING 4
#define WPAD_BATTERY_LEVEL_CRITICAL 0
#define WPAD_BATTERY_LEVEL_LOW 1
#define WPAD_BATTERY_LEVEL_MEDIUM 2
#define WPAD_BATTERY_LEVEL_HIGH 3
#define WPAD_BATTERY_LEVEL_MAX 4
#define WPAD_SENSOR_BAR_POS_BOTTOM 0
#define WPAD_SENSOR_BAR_POS_TOP 1
#define WPAD_ERR_NONE 0
#define WPAD_ERR_NO_CONTROLLER -1
#define WPAD_ERR_BUSY -2
#define WPAD_ERR_TRANSFER -3
#define WPAD_ERR_INVALID -4
#define WPAD_ERR_NOPERM -5
#define WPAD_ERR_BROKEN -6
#define WPAD_ERR_CORRUPTED -7

typedef s32 WPADChannel;
typedef u32 WPADDeviceType;
typedef void (*WPADCallback)(s32 chan, s32 result);
typedef WPADCallback WPADConnectCallback;
typedef WPADCallback WPADExtensionCallback;
typedef void (*WPADSamplingCallback)(s32 chan);
typedef void* (*WPADAlloc)(u32 size);
typedef u8 (*WPADFree)(void* ptr);

typedef struct DPDObject {
	s16 x, y;
	u16 size;
	u8 traceId;
} DPDObject;

typedef struct DPDObjEx {
	s16 range_x1, range_y1, range_x2, range_y2;
	u16 pixel;
	s8 radius;
} DPDObjEx;

typedef struct WPADStatus {
	u16 button;
	s16 accX, accY, accZ;
	DPDObject obj[WPAD_DPD_MAX_OBJECTS];
	u8 dev;
	s8 err;
} WPADStatus;

typedef struct WPADFSStatus {
	u16 button;
	s16 accX, accY, accZ;
	DPDObject obj[WPAD_DPD_MAX_OBJECTS];
	u8 dev;
	s8 err;
	s16 fsAccX, fsAccY, fsAccZ;
	s8 fsStickX, fsStickY;
} WPADFSStatus;

typedef struct WPADCLStatus {
	u16 button;
	s16 accX, accY, accZ;
	DPDObject obj[WPAD_DPD_MAX_OBJECTS];
	u8 dev;
	s8 err;
	u16 clButton;
	s16 clLStickX, clLStickY, clRStickX, clRStickY;
	u8 clTriggerL, clTriggerR;
} WPADCLStatus;

typedef struct WPADStatusEx {
	u16 button;
	s16 accX, accY, accZ;
	DPDObject obj[WPAD_DPD_MAX_OBJECTS];
	u8 dev;
	s8 err;
	DPDObjEx exp[WPAD_DPD_MAX_OBJECTS];
} WPADStatusEx;

typedef struct WPADAcc {
	s16 x, y, z;
} WPADAcc;

typedef struct WPADInfo {
	BOOL dpd, speaker, attach, lowBat, nearempty;
	u8 battery, led, protocol, firmware;
} WPADInfo;

void WPADInit(void);
s32 WPADGetStatus(void);
s32 WPADProbe(s32 chan, u32* type);
void WPADRead(s32 chan, void* status);
u32 WPADGetDataFormat(s32 chan);
s32 WPADSetDataFormat(s32 chan, u32 format);
void WPADSetAutoSamplingBuf(s32 chan, void* buffer, u32 count);
WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback callback);
WPADExtensionCallback WPADSetExtensionCallback(s32 chan, WPADExtensionCallback callback);
WPADSamplingCallback WPADSetSamplingCallback(s32 chan, WPADSamplingCallback callback);
void WPADDisconnect(s32 chan);
void WPADControlMotor(s32 chan, u32 command);
s32 WPADGetInfoAsync(s32 chan, WPADInfo* info, WPADCallback callback);
void WPADGetAccGravityUnit(s32 chan, u32 type, WPADAcc* acc);
BOOL WPADIsDpdEnabled(s32 chan);
s32 WPADControlDpd(s32 chan, u32 command, WPADCallback callback);
u8 WPADGetDpdSensitivity(void);
u8 WPADGetSensorBarPosition(void);
void WPADSetAutoSleepTime(u8 minutes);
void WPADRegisterAllocator(WPADAlloc alloc, WPADFree freeFunc);
u32 WPADGetWorkMemorySize(void);
BOOL WPADIsSpeakerEnabled(s32 chan);
s32 WPADControlSpeaker(s32 chan, u32 command, WPADCallback callback);
u8 WPADGetSpeakerVolume(void);
BOOL WPADCanSendStreamData(s32 chan);
s32 WPADSendStreamData(s32 chan, void* data, u16 length);
#define WPADStartMotor(chan) WPADControlMotor((chan), WPAD_MOTOR_RUMBLE)
#define WPADStopMotor(chan) WPADControlMotor((chan), WPAD_MOTOR_STOP)

#ifdef __cplusplus
}
#endif
#endif

#pragma once

#include <dolphin/pad.h>

typedef struct PADKeyButtonBinding {
  s32 scancode;
  PADButton padButton;
} PADKeyButtonBinding;

typedef struct PADKeyAxisBinding {
  s32 scancode;
  PADAxis padAxis;
  s16 influence; // normalized percentage between 0 and 1
} PADKeyAxisBinding;

// On-disk layouts of CTRL v3/v4 records; retained only for one-time import.
typedef struct PADDeadZones {
  bool emulateTriggers;
  bool useDeadzones;
  u16 stickDeadZone;
  u16 substickDeadZone;
  u16 leftTriggerActivationZone;
  u16 rightTriggerActivationZone;
} PADDeadZones;

typedef struct PADButtonMapping {
  u32 nativeButton;
  PADButton padButton;
} PADButtonMapping;

typedef struct PADAxisMapping {
  PADSignedNativeAxis nativeAxis;
  s32 nativeButton;
  PADAxis padAxis;
} PADAxisMapping;

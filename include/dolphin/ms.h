// Aurora exclusive API extension
// While the GC and Wii could both technically support mice, they never received official libraries
// This library is a theoretical implementation based loosely on PAD
#ifndef DOLPHIN_MS_H
#define DOLPHIN_MS_H
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MSStatus {
  f32 x;
  f32 y;
  f32 xrel;
  f32 yrel;
  u32 buttons;
  f32 scrollX;
  f32 scrollY;
} MSStatus;

#define MS_BUTTON_LEFT (1 << 1)
#define MS_BUTTON_MIDDLE (1 << 2)
#define MS_BUTTON_RIGHT (1 << 3)
#define MS_BUTTON_X1 (1 << 4)
#define MS_BUTTON_X2 (1 << 5)

void MSRead(MSStatus* status);

#ifdef __cplusplus
}
#endif

#endif // DUSK_MS_H

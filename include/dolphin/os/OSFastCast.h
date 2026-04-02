#ifndef _DOLPHIN_OSFASTCAST_H_
#define _DOLPHIN_OSFASTCAST_H_

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void OSInitFastCast(void) {}

static inline s16 __OSf32tos16(f32 inF) { return (s16) inF; }
static inline void OSf32tos16(f32 *f, s16 *out) { *out = __OSf32tos16(*f); }

static inline u8 __OSf32tou8(f32 inF) { return (u8)inF; }
static inline void OSf32tou8(f32 *f, u8 *out) { *out = __OSf32tou8(*f); }

static inline s8 __OSf32tos8(f32 inF) { return (s8) inF; }
static inline void OSf32tos8(f32 *f, s8 *out) { *out = __OSf32tos8(*f); }

static inline u16 __OSf32tou16(f32 inF) { return (u16) inF; }
static inline void OSf32tou16(f32 *f, u16 *out) { *out = __OSf32tou16(*f); }

static inline f32 __OSs8tof32(const s8* arg) { return (f32)*arg; }
static inline void OSs8tof32(const s8* in, f32* out) { *out = __OSs8tof32(in); }

static inline f32 __OSs16tof32(const s16* arg) { return (f32)*arg; }
static inline void OSs16tof32(const s16* in, f32* out) { *out = __OSs16tof32(in); }

static inline f32 __OSu8tof32(const u8* arg) { return (f32)*arg; }
static inline void OSu8tof32(const u8* in, f32* out) { *out = __OSu8tof32(in); }

static inline f32 __OSu16tof32(const u16* arg) { return (f32)*arg; }
static inline void OSu16tof32(const u16* in, f32* out) { *out = __OSu16tof32(in); }

#ifdef __cplusplus
}
#endif

#endif

#include <dolphin/mtx.h>

#ifndef _DOLPHIN_MTX44EXT
#define _DOLPHIN_MTX44EXT

#ifdef __cplusplus
extern "C" {
#endif

void C_MTX44Identity(Mtx44 m);
void C_MTX44Copy(const Mtx44 src, Mtx44 dst);
void C_MTX44Concat(const Mtx44 a, const Mtx44 b, Mtx44 ab);
void C_MTX44Transpose(const Mtx44 src, Mtx44 xPose);
u32 C_MTX44Inverse(const Mtx44 src, Mtx44 inv);

#ifdef GEKKO
void PSMTX44Identity(Mtx44 m);
void PSMTX44Copy(const Mtx44 src, Mtx44 dst);
void PSMTX44Concat(const Mtx44 a, const Mtx44 b, Mtx44 ab);
void PSMTX44Transpose(const Mtx44 src, Mtx44 xPose);
#endif

#ifdef MTX_USE_PS
#define MTX44Identity PSMTX44Identity
#define MTX44Copy PSMTX44Copy
#define MTX44Concat PSMTX44Concat
#define MTX44Transpose PSMTX44Transpose
#else
#define MTX44Identity C_MTX44Identity
#define MTX44Copy C_MTX44Copy
#define MTX44Concat C_MTX44Concat
#define MTX44Transpose C_MTX44Transpose
#endif

#define MTX44Inverse C_MTX44Inverse

void C_MTX44Trans(Mtx44 m, f32 xT, f32 yT, f32 zT);
void C_MTX44TransApply(const Mtx44 src, Mtx44 dst, f32 xT, f32 yT, f32 zT);
void C_MTX44Scale(Mtx44 m, f32 xS, f32 yS, f32 zS);
void C_MTX44ScaleApply(const Mtx44 src, Mtx44 dst, f32 xS, f32 yS, f32 zS);

void C_MTX44RotRad(Mtx44 m, char axis, f32 rad);
void C_MTX44RotTrig(Mtx44 m, char axis, f32 sinA, f32 cosA);
void C_MTX44RotAxisRad(Mtx44 m, const Vec* axis, f32 rad);

#ifdef GEKKO
void PSMTX44Trans(Mtx44 m, f32 xT, f32 yT, f32 zT);
void PSMTX44TransApply(const Mtx44 src, Mtx44 dst, f32 xT, f32 yT, f32 zT);
void PSMTX44Scale(Mtx44 m, f32 xS, f32 yS, f32 zS);
void PSMTX44ScaleApply(const Mtx44 src, Mtx44 dst, f32 xS, f32 yS, f32 zS);

void PSMTX44RotRad(Mtx44 m, char axis, f32 rad);
void PSMTX44RotTrig(Mtx44 m, char axis, f32 sinA, f32 cosA);
void PSMTX44RotAxisRad(Mtx44 m, const Vec* axis, f32 rad);
#endif

#ifdef MTX_USE_PS
#define MTX44Trans PSMTX44Trans
#define MTX44TransApply PSMTX44TransApply
#define MTX44Scale PSMTX44Scale
#define MTX44ScaleApply PSMTX44ScaleApply

#define MTX44RotRad PSMTX44RotRad
#define MTX44RotTrig PSMTX44RotTrig
#define MTX44RotAxisRad PSMTX44RotAxisRad

#else
#define MTX44Trans C_MTX44Trans
#define MTX44TransApply C_MTX44TransApply
#define MTX44Scale C_MTX44Scale
#define MTX44ScaleApply C_MTX44ScaleApply

#define MTX44RotRad C_MTX44RotRad
#define MTX44RotTrig C_MTX44RotTrig
#define MTX44RotAxisRad C_MTX44RotAxisRad
#endif

void C_MTX44MultVec(const Mtx44 m, const Vec* src, Vec* dst);
void C_MTX44MultVecArray(const Mtx44 m, const Vec* srcBase, Vec* dstBase, u32 count);
void C_MTX44MultVecSR(const Mtx44 m, const Vec* src, Vec* dst);
void C_MTX44MultVecArraySR(const Mtx44 m, const Vec* srcBase, Vec* dstBase, u32 count);

#ifdef GEKKO
void PSMTX44MultVec(const Mtx44 m, const Vec* src, Vec* dst);
void PSMTX44MultVecArray(const Mtx44 m, const Vec* srcBase, Vec* dstBase, u32 count);
void PSMTX44MultVecSR(const Mtx44 m, const Vec* src, Vec* dst);
void PSMTX44MultVecArraySR(const Mtx44 m, const Vec* srcBase, Vec* dstBase, u32 count);
#endif

#ifdef MTX_USE_PS
#define MTX44MultVec PSMTX44MultVec
#define MTX44MultVecArray PSMTX44MultVecArray
#define MTX44MultVecSR PSMTX44MultVecSR
#define MTX44MultVecArraySR PSMTX44MultVecArraySR
#else
#define MTX44MultVec C_MTX44MultVec
#define MTX44MultVecArray C_MTX44MultVecArray
#define MTX44MultVecSR C_MTX44MultVecSR
#define MTX44MultVecArraySR C_MTX44MultVecArraySR
#endif

#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_MTX44EXT

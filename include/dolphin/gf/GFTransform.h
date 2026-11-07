#ifndef _DOLPHIN_GF_GFTRANSFORM_H
#define _DOLPHIN_GF_GFTRANSFORM_H

#include <dolphin/mtx.h>

void GFLoadPosMtxImm(MtxP, u32);
void GFLoadNrmMtxImm(MtxP, u32);
void GFSetCurrentMtx(u32, u32, u32, u32, u32, u32, u32, u32, u32);

#endif /* _DOLPHIN_GF_GFTRANSFORM_H */

#include "gx.hpp"
#include "__gx.h"

static inline void SETVCDATTR(GXAttr attr, GXAttrType type) {
  switch (attr) {
  case GX_VA_PNMTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 0, type);
    break;
  case GX_VA_TEX0MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 1, type);
    break;
  case GX_VA_TEX1MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 2, type);
    break;
  case GX_VA_TEX2MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 3, type);
    break;
  case GX_VA_TEX3MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 4, type);
    break;
  case GX_VA_TEX4MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 5, type);
    break;
  case GX_VA_TEX5MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 6, type);
    break;
  case GX_VA_TEX6MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 7, type);
    break;
  case GX_VA_TEX7MTXIDX:
    SET_REG_FIELD(0, __gx->vcdLo, 1, 8, type);
    break;
  case GX_VA_POS:
    SET_REG_FIELD(0, __gx->vcdLo, 2, 9, type);
    break;
  case GX_VA_NRM:
    __gx->hasNrms = (type != 0);
    if (type != GX_NONE) {
      __gx->nrmType = type;
    }
    break;
  case GX_VA_NBT:
    __gx->hasBiNrms = (type != 0);
    if (type != GX_NONE) {
      __gx->nrmType = type;
    }
    break;
  case GX_VA_CLR0:
    SET_REG_FIELD(0, __gx->vcdLo, 2, 13, type);
    break;
  case GX_VA_CLR1:
    SET_REG_FIELD(0, __gx->vcdLo, 2, 15, type);
    break;
  case GX_VA_TEX0:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 0, type);
    break;
  case GX_VA_TEX1:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 2, type);
    break;
  case GX_VA_TEX2:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 4, type);
    break;
  case GX_VA_TEX3:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 6, type);
    break;
  case GX_VA_TEX4:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 8, type);
    break;
  case GX_VA_TEX5:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 10, type);
    break;
  case GX_VA_TEX6:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 12, type);
    break;
  case GX_VA_TEX7:
    SET_REG_FIELD(0, __gx->vcdHi, 2, 14, type);
    break;
  default:
    break;
  }
}

static inline void SETVAT(u32* va, u32* vb, u32* vc, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
  switch (attr) {
  case GX_VA_POS:
    SET_REG_FIELD(0, *va, 1, 0, cnt);
    SET_REG_FIELD(0, *va, 3, 1, type);
    SET_REG_FIELD(0, *va, 5, 4, frac);
    break;
  case GX_VA_NRM:
  case GX_VA_NBT:
    SET_REG_FIELD(0, *va, 3, 10, type);
    if (cnt == GX_NRM_NBT3) {
      SET_REG_FIELD(0, *va, 1, 9, 1);
      SET_REG_FIELD(0, *va, 1, 31, 1);
    } else {
      SET_REG_FIELD(0, *va, 1, 9, cnt);
      SET_REG_FIELD(0, *va, 1, 31, 0);
    }
    break;
  case GX_VA_CLR0:
    SET_REG_FIELD(0, *va, 1, 13, cnt);
    SET_REG_FIELD(0, *va, 3, 14, type);
    break;
  case GX_VA_CLR1:
    SET_REG_FIELD(0, *va, 1, 17, cnt);
    SET_REG_FIELD(0, *va, 3, 18, type);
    break;
  case GX_VA_TEX0:
    SET_REG_FIELD(0, *va, 1, 21, cnt);
    SET_REG_FIELD(0, *va, 3, 22, type);
    SET_REG_FIELD(0, *va, 5, 25, frac);
    break;
  case GX_VA_TEX1:
    SET_REG_FIELD(0, *vb, 1, 0, cnt);
    SET_REG_FIELD(0, *vb, 3, 1, type);
    SET_REG_FIELD(0, *vb, 5, 4, frac);
    break;
  case GX_VA_TEX2:
    SET_REG_FIELD(0, *vb, 1, 9, cnt);
    SET_REG_FIELD(0, *vb, 3, 10, type);
    SET_REG_FIELD(0, *vb, 5, 13, frac);
    break;
  case GX_VA_TEX3:
    SET_REG_FIELD(0, *vb, 1, 18, cnt);
    SET_REG_FIELD(0, *vb, 3, 19, type);
    SET_REG_FIELD(0, *vb, 5, 22, frac);
    break;
  case GX_VA_TEX4:
    SET_REG_FIELD(0, *vb, 1, 27, cnt);
    SET_REG_FIELD(0, *vb, 3, 28, type);
    SET_REG_FIELD(0, *vc, 5, 0, frac);
    break;
  case GX_VA_TEX5:
    SET_REG_FIELD(0, *vc, 1, 5, cnt);
    SET_REG_FIELD(0, *vc, 3, 6, type);
    SET_REG_FIELD(0, *vc, 5, 9, frac);
    break;
  case GX_VA_TEX6:
    SET_REG_FIELD(0, *vc, 1, 14, cnt);
    SET_REG_FIELD(0, *vc, 3, 15, type);
    SET_REG_FIELD(0, *vc, 5, 18, frac);
    break;
  case GX_VA_TEX7:
    SET_REG_FIELD(0, *vc, 1, 23, cnt);
    SET_REG_FIELD(0, *vc, 3, 24, type);
    SET_REG_FIELD(0, *vc, 5, 27, frac);
    break;
  default:
    break;
  }
}

extern "C" {

void GXSetVtxDesc(GXAttr attr, GXAttrType type) {
  SETVCDATTR(attr, type);
  if (__gx->hasNrms || __gx->hasBiNrms) {
    SET_REG_FIELD(0, __gx->vcdLo, 2, 11, __gx->nrmType);
  } else {
    SET_REG_FIELD(0, __gx->vcdLo, 2, 11, 0);
  }
  __gx->dirtyState |= 8;
}

void GXSetVtxDescv(GXVtxDescList* list) {
  while (list->attr != GX_VA_NULL) {
    SETVCDATTR(list->attr, list->type);
    ++list;
  }
  if (__gx->hasNrms || __gx->hasBiNrms) {
    SET_REG_FIELD(0, __gx->vcdLo, 2, 11, __gx->nrmType);
  } else {
    SET_REG_FIELD(0, __gx->vcdLo, 2, 11, 0);
  }
  __gx->dirtyState |= 8;
}

void GXClearVtxDesc() {
  __gx->vcdLo = 0;
  SET_REG_FIELD(0, __gx->vcdLo, 2, 9, 1); // GX_VA_POS = GX_DIRECT
  __gx->vcdHi = 0;
  __gx->hasNrms = 0;
  __gx->hasBiNrms = 0;
  __gx->dirtyState |= 8;
}

void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
  CHECK(vtxfmt >= GX_VTXFMT0 && vtxfmt < GX_MAX_VTXFMT, "invalid vtxfmt {}", underlying(vtxfmt));
  CHECK(attr >= GX_VA_POS && attr < GX_VA_MAX_ATTR, "invalid attr {}", underlying(attr));

  u32* va = &__gx->vatA[vtxfmt];
  u32* vb = &__gx->vatB[vtxfmt];
  u32* vc = &__gx->vatC[vtxfmt];
  SETVAT(va, vb, vc, attr, cnt, type, frac);
  __gx->dirtyState |= 0x10;
  __gx->dirtyVAT |= static_cast<u8>(1 << vtxfmt);
}

void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride) {
  GXAttr cpAttr = static_cast<GXAttr>(attr);
  if (attr == GX_VA_NBT) {
    cpAttr = GX_VA_NRM;
  }
  u32 cpIdx = cpAttr - GX_VA_POS;

  // Write CP array base and stride
  GX_WRITE_SOME_REG2(8, cpIdx | 0xA0, reinterpret_cast<uintptr_t>(data), cpIdx - 12);
  GX_WRITE_SOME_REG3(8, cpIdx | 0xB0, stride, cpIdx - 12);

  // Keep g_gxState in sync (TARGET_PC extension: store size)
  auto& array = g_gxState.arrays[attr];
  update_gx_state(array.data, data);
  update_gx_state(array.size, size);
  update_gx_state(array.stride, stride);
  array.cachedRange = {};
}

void GXSetTexCoordGen2(GXTexCoordID dst, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize, u32 postMtx) {
  CHECK(dst >= GX_TEXCOORD0 && dst <= GX_TEXCOORD7, "invalid tex coord {}", underlying(dst));

  u32 reg = 0;
  u32 row = 5;
  u32 form = 0;

  switch (src) {
  case GX_TG_POS:
    row = 0;
    form = 1;
    break;
  case GX_TG_NRM:
    row = 1;
    form = 1;
    break;
  case GX_TG_BINRM:
    row = 3;
    form = 1;
    break;
  case GX_TG_TANGENT:
    row = 4;
    form = 1;
    break;
  case GX_TG_COLOR0:
    row = 2;
    break;
  case GX_TG_COLOR1:
    row = 2;
    break;
  case GX_TG_TEX0:
    row = 5;
    break;
  case GX_TG_TEX1:
    row = 6;
    break;
  case GX_TG_TEX2:
    row = 7;
    break;
  case GX_TG_TEX3:
    row = 8;
    break;
  case GX_TG_TEX4:
    row = 9;
    break;
  case GX_TG_TEX5:
    row = 10;
    break;
  case GX_TG_TEX6:
    row = 11;
    break;
  case GX_TG_TEX7:
    row = 12;
    break;
  default:
    break;
  }

  switch (type) {
  case GX_TG_MTX2x4:
    SET_REG_FIELD(0, reg, 1, 1, 0);
    SET_REG_FIELD(0, reg, 1, 2, form);
    SET_REG_FIELD(0, reg, 3, 4, 0);
    SET_REG_FIELD(0, reg, 5, 7, row);
    break;
  case GX_TG_MTX3x4:
    SET_REG_FIELD(0, reg, 1, 1, 1);
    SET_REG_FIELD(0, reg, 1, 2, form);
    SET_REG_FIELD(0, reg, 3, 4, 0);
    SET_REG_FIELD(0, reg, 5, 7, row);
    break;
  case GX_TG_SRTG:
    SET_REG_FIELD(0, reg, 1, 1, 0);
    SET_REG_FIELD(0, reg, 1, 2, form);
    if (src == GX_TG_COLOR0) {
      SET_REG_FIELD(0, reg, 3, 4, 2);
    } else {
      SET_REG_FIELD(0, reg, 3, 4, 3);
    }
    SET_REG_FIELD(0, reg, 5, 7, 2);
    break;
  default:
    // Bump mapping types
    if (type >= 2 && type <= 9) {
      SET_REG_FIELD(0, reg, 1, 1, 0);
      SET_REG_FIELD(0, reg, 1, 2, form);
      SET_REG_FIELD(0, reg, 3, 4, 1);
      SET_REG_FIELD(0, reg, 5, 7, row);
      SET_REG_FIELD(0, reg, 3, 12, src - 12);
      SET_REG_FIELD(0, reg, 3, 15, type - 2);
    }
    break;
  }

  GX_WRITE_XF_REG(dst + 0x40, reg);

  // Post-transform register
  u32 postReg = 0;
  SET_REG_FIELD(0, postReg, 6, 0, postMtx - 64);
  SET_REG_FIELD(0, postReg, 1, 8, normalize);
  GX_WRITE_XF_REG(dst + 0x50, postReg);

  // Update matrix index shadows
  switch (dst) {
  case GX_TEXCOORD0:
    SET_REG_FIELD(0, __gx->matIdxA, 6, 6, mtx);
    break;
  case GX_TEXCOORD1:
    SET_REG_FIELD(0, __gx->matIdxA, 6, 12, mtx);
    break;
  case GX_TEXCOORD2:
    SET_REG_FIELD(0, __gx->matIdxA, 6, 18, mtx);
    break;
  case GX_TEXCOORD3:
    SET_REG_FIELD(0, __gx->matIdxA, 6, 24, mtx);
    break;
  case GX_TEXCOORD4:
    SET_REG_FIELD(0, __gx->matIdxB, 6, 0, mtx);
    break;
  case GX_TEXCOORD5:
    SET_REG_FIELD(0, __gx->matIdxB, 6, 6, mtx);
    break;
  case GX_TEXCOORD6:
    SET_REG_FIELD(0, __gx->matIdxB, 6, 12, mtx);
    break;
  default:
    SET_REG_FIELD(0, __gx->matIdxB, 6, 18, mtx);
    break;
  }
  __GXSetMatrixIndex(static_cast<GXAttr>(dst + 1));
}

void GXSetNumTexGens(u8 num) {
  SET_REG_FIELD(0, __gx->genMode, 4, 0, num);
  GX_WRITE_XF_REG(0x3F, num);
  __gx->dirtyState |= 4;
}

void GXInvalidateVtxCache() { GX_WRITE_U8(0x48); }

void GXSetLineWidth(u8 width, GXTexOffset offs) {
  SET_REG_FIELD(0, __gx->lpSize, 8, 0, width);
  SET_REG_FIELD(0, __gx->lpSize, 3, 16, offs);
  GX_WRITE_RAS_REG(__gx->lpSize);
  __gx->bpSent = 1;
}

// TODO GXSetPointSize
// TODO GXEnableTexOffsets
}

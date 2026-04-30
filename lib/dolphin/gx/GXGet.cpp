#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/common.hpp"
#include "../../gfx/texture.hpp"

extern "C" {

static inline u8 GetFracForNrm(GXCompType type) {
  switch (type) {
  case GX_S8:
    return 6;
  case GX_S16:
    return 14;
  default:
    return 0;
  }
}

void GXGetVtxDesc(GXAttr attr, GXAttrType* type) {
  CHECK(type != nullptr, "null vtx desc output");

  u32 cpType = 0;
  switch (attr) {
  case GX_VA_PNMTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 0);
    break;
  case GX_VA_TEX0MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 1);
    break;
  case GX_VA_TEX1MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 2);
    break;
  case GX_VA_TEX2MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 3);
    break;
  case GX_VA_TEX3MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 4);
    break;
  case GX_VA_TEX4MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 5);
    break;
  case GX_VA_TEX5MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 6);
    break;
  case GX_VA_TEX6MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 7);
    break;
  case GX_VA_TEX7MTXIDX:
    cpType = GET_REG_FIELD(__gx->vcdLo, 1, 8);
    break;
  case GX_VA_POS:
    cpType = GET_REG_FIELD(__gx->vcdLo, 2, 9);
    break;
  case GX_VA_NRM:
    cpType = __gx->hasNrms ? GET_REG_FIELD(__gx->vcdLo, 2, 11) : GX_NONE;
    break;
  case GX_VA_NBT:
    cpType = __gx->hasBiNrms ? GET_REG_FIELD(__gx->vcdLo, 2, 11) : GX_NONE;
    break;
  case GX_VA_CLR0:
    cpType = GET_REG_FIELD(__gx->vcdLo, 2, 13);
    break;
  case GX_VA_CLR1:
    cpType = GET_REG_FIELD(__gx->vcdLo, 2, 15);
    break;
  case GX_VA_TEX0:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 0);
    break;
  case GX_VA_TEX1:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 2);
    break;
  case GX_VA_TEX2:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 4);
    break;
  case GX_VA_TEX3:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 6);
    break;
  case GX_VA_TEX4:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 8);
    break;
  case GX_VA_TEX5:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 10);
    break;
  case GX_VA_TEX6:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 12);
    break;
  case GX_VA_TEX7:
    cpType = GET_REG_FIELD(__gx->vcdHi, 2, 14);
    break;
  default:
    break;
  }

  *type = static_cast<GXAttrType>(cpType);
}

void GXGetVtxDescv(GXVtxDescList* vcd) {
  CHECK(vcd != nullptr, "null vtx desc list");

  GXVtxDescList* out = vcd;
  for (GXAttr attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; attr = static_cast<GXAttr>(attr + 1), ++out) {
    out->attr = attr;
    GXGetVtxDesc(attr, &out->type);
  }
  out->attr = GX_VA_NBT;
  GXGetVtxDesc(GX_VA_NBT, &out->type);
  ++out;
  out->attr = GX_VA_NULL;
}

void GXGetVtxAttrFmt(GXVtxFmt idx, GXAttr attr, GXCompCnt* compCnt, GXCompType* compType, u8* shift) {
  CHECK(compCnt != nullptr && compType != nullptr && shift != nullptr, "null vtx attr fmt output");

  const u32 va = __gx->vatA[idx];
  const u32 vb = __gx->vatB[idx];
  const u32 vc = __gx->vatC[idx];

  switch (attr) {
  case GX_VA_POS:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(va, 1, 0));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(va, 3, 1));
    *shift = static_cast<u8>(GET_REG_FIELD(va, 5, 4));
    return;
  case GX_VA_NRM:
  case GX_VA_NBT:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(va, 1, 9));
    if (*compCnt == GX_TEX_ST && GET_REG_FIELD(va, 1, 31) != 0) {
      *compCnt = GX_NRM_NBT3;
    }
    *compType = static_cast<GXCompType>(GET_REG_FIELD(va, 3, 10));
    *shift = GetFracForNrm(*compType);
    return;
  case GX_VA_CLR0:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(va, 1, 13));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(va, 3, 14));
    *shift = 0;
    return;
  case GX_VA_CLR1:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(va, 1, 17));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(va, 3, 18));
    *shift = 0;
    return;
  case GX_VA_TEX0:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(va, 1, 21));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(va, 3, 22));
    *shift = static_cast<u8>(GET_REG_FIELD(va, 5, 25));
    return;
  case GX_VA_TEX1:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(vb, 1, 0));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(vb, 3, 1));
    *shift = static_cast<u8>(GET_REG_FIELD(vb, 5, 4));
    return;
  case GX_VA_TEX2:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(vb, 1, 9));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(vb, 3, 10));
    *shift = static_cast<u8>(GET_REG_FIELD(vb, 5, 13));
    return;
  case GX_VA_TEX3:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(vb, 1, 18));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(vb, 3, 19));
    *shift = static_cast<u8>(GET_REG_FIELD(vb, 5, 22));
    return;
  case GX_VA_TEX4:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(vb, 1, 27));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(vb, 3, 28));
    *shift = static_cast<u8>(GET_REG_FIELD(vc, 5, 0));
    return;
  case GX_VA_TEX5:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(vc, 1, 5));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(vc, 3, 6));
    *shift = static_cast<u8>(GET_REG_FIELD(vc, 5, 9));
    return;
  case GX_VA_TEX6:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(vc, 1, 14));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(vc, 3, 15));
    *shift = static_cast<u8>(GET_REG_FIELD(vc, 5, 18));
    return;
  case GX_VA_TEX7:
    *compCnt = static_cast<GXCompCnt>(GET_REG_FIELD(vc, 1, 23));
    *compType = static_cast<GXCompType>(GET_REG_FIELD(vc, 3, 24));
    *shift = static_cast<u8>(GET_REG_FIELD(vc, 5, 27));
    return;
  default:
    *compCnt = GX_TEX_ST;
    *compType = GX_RGB565;
    *shift = 0;
    return;
  }
}

void GXGetVtxAttrFmtv(GXVtxFmt fmt, GXVtxAttrFmtList* vat) {
  CHECK(vat != nullptr, "null vtx attr fmt list");

  GXVtxAttrFmtList* out = vat;
  for (GXAttr attr = GX_VA_POS; attr <= GX_VA_TEX7; attr = static_cast<GXAttr>(attr + 1), ++out) {
    out->attr = attr;
    GXGetVtxAttrFmt(fmt, attr, &out->cnt, &out->type, &out->frac);
  }
  out->attr = GX_VA_NULL;
}

void GXGetLineWidth(u8* width, GXTexOffset* texOffsets) {
  CHECK(width != nullptr && texOffsets != nullptr, "null line width output");

  *width = static_cast<u8>(GET_REG_FIELD(__gx->lpSize, 8, 0));
  *texOffsets = static_cast<GXTexOffset>(GET_REG_FIELD(__gx->lpSize, 3, 16));
}

void GXGetPointSize(u8* pointSize, GXTexOffset* texOffsets) {
  CHECK(pointSize != nullptr && texOffsets != nullptr, "null point size output");

  *pointSize = static_cast<u8>(GET_REG_FIELD(__gx->lpSize, 8, 8));
  *texOffsets = static_cast<GXTexOffset>(GET_REG_FIELD(__gx->lpSize, 3, 19));
}

void GXGetViewportv(f32 *vp) {
  CHECK(vp != nullptr, "null viewport output");

  vp[0] = __gx->vpLeft;
  vp[1] = __gx->vpTop;
  vp[2] = __gx->vpWd;
  vp[3] = __gx->vpHt;
  vp[4] = __gx->vpNearz;
  vp[5] = __gx->vpFarz;
}

void GXGetProjectionv(f32* p) {
  p[0] = __gx->projType == GX_PERSPECTIVE ? 0.0f : 1.0f;
  p[1] = __gx->projMtx[0];
  p[2] = __gx->projMtx[1];
  p[3] = __gx->projMtx[2];
  p[4] = __gx->projMtx[3];
  p[5] = __gx->projMtx[4];
  p[6] = __gx->projMtx[5];
}

void GXGetScissor(u32* left, u32* top, u32* wd, u32* ht) {
  CHECK(left != nullptr && top != nullptr && wd != nullptr && ht != nullptr, "null scissor output");

  const u32 tp = GET_REG_FIELD(__gx->suScis0, 11, 0);
  const u32 lf = GET_REG_FIELD(__gx->suScis0, 11, 12);
  const u32 bm = GET_REG_FIELD(__gx->suScis1, 11, 0);
  const u32 rt = GET_REG_FIELD(__gx->suScis1, 11, 12);

  *left = lf - 342;
  *top = tp - 342;
  *wd = rt - lf + 1;
  *ht = bm - tp + 1;
}

void GXGetCullMode(GXCullMode* mode) {
  CHECK(mode != nullptr, "null cull mode output");

  const auto hwMode = static_cast<GXCullMode>(GET_REG_FIELD(__gx->genMode, 2, 14));
  switch (hwMode) {
  case GX_CULL_FRONT:
    *mode = GX_CULL_BACK;
    break;
  case GX_CULL_BACK:
    *mode = GX_CULL_FRONT;
    break;
  default:
    *mode = hwMode;
    break;
  }
}

void GXGetLightAttnA(GXLightObj* light_, float* a0, float* a1, float* a2) {
  auto* light = reinterpret_cast<const GXLightObj_*>(light_);
  *a0 = light->a0;
  *a1 = light->a1;
  *a2 = light->a2;
}

void GXGetLightAttnK(GXLightObj* light_, float* k0, float* k1, float* k2) {
  auto* light = reinterpret_cast<const GXLightObj_*>(light_);
  *k0 = light->k0;
  *k1 = light->k1;
  *k2 = light->k2;
}

void GXGetLightPos(GXLightObj* light_, float* x, float* y, float* z) {
  auto* light = reinterpret_cast<const GXLightObj_*>(light_);
  *x = light->px;
  *z = light->py;
  *z = light->pz;
}

void GXGetLightDir(GXLightObj* light_, float* nx, float* ny, float* nz) {
  auto* light = reinterpret_cast<const GXLightObj_*>(light_);
  *nx = -light->nx;
  *ny = -light->ny;
  *nz = -light->nz;
}

void GXGetLightColor(GXLightObj* light_, GXColor* col) {
  auto* light = reinterpret_cast<const GXLightObj_*>(light_);
  *col = light->color;
}

void* GXGetTexObjData(GXTexObj* tex_obj) {
  return const_cast<void*>(reinterpret_cast<const GXTexObj_*>(tex_obj)->data);
}

u16 GXGetTexObjWidth(GXTexObj* tex_obj) { return static_cast<u16>(reinterpret_cast<const GXTexObj_*>(tex_obj)->width()); }

u16 GXGetTexObjHeight(GXTexObj* tex_obj) { return static_cast<u16>(reinterpret_cast<const GXTexObj_*>(tex_obj)->height()); }

GXTexFmt GXGetTexObjFmt(GXTexObj* tex_obj) {
  return static_cast<GXTexFmt>(reinterpret_cast<const GXTexObj_*>(tex_obj)->format());
}

GXTexWrapMode GXGetTexObjWrapS(GXTexObj* tex_obj) { return reinterpret_cast<const GXTexObj_*>(tex_obj)->wrap_s(); }

GXTexWrapMode GXGetTexObjWrapT(GXTexObj* tex_obj) { return reinterpret_cast<const GXTexObj_*>(tex_obj)->wrap_t(); }

GXBool GXGetTexObjMipMap(GXTexObj* tex_obj) { return reinterpret_cast<const GXTexObj_*>(tex_obj)->has_mips(); }

// TODO GXGetTexObjAll
// TODO GXGetTexObjMinFilt
// TODO GXGetTexObjMagFilt
// TODO GXGetTexObjMinLOD
// TODO GXGetTexObjMaxLOD
// TODO GXGetTexObjLODBias
// TODO GXGetTexObjBiasClamp
// TODO GXGetTexObjEdgeLOD
// TODO GXGetTexObjMaxAniso
// TODO GXGetTexObjLODAll
u32 GXGetTexObjTlut(const GXTexObj* tex_obj) { return reinterpret_cast<const GXTexObj_*>(tex_obj)->tlut; }
// TODO GXGetTlutObjData
// TODO GXGetTlutObjFmt
// TODO GXGetTlutObjNumEntries
// TODO GXGetTlutObjAll
// TODO GXGetTexRegionAll
// TODO GXGetTlutRegionAll
}

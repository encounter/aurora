#include "gx.hpp"

#include <optional>

void GXSetVtxDesc(GXAttr attr, GXAttrType type) { update_gx_state(g_gxState.vtxDesc[attr], type); }

void GXSetVtxDescv(GXVtxDescList* list) {
  g_gxState.vtxDesc.fill({});
  while (list->attr != GX_VA_NULL) {
    update_gx_state(g_gxState.vtxDesc[list->attr], list->type);
    ++list;
  }
}

void GXClearVtxDesc() { g_gxState.vtxDesc.fill({}); }

void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
  CHECK(vtxfmt >= GX_VTXFMT0 && vtxfmt < GX_MAX_VTXFMT, "invalid vtxfmt {}", static_cast<int>(vtxfmt));
  CHECK(attr >= GX_VA_PNMTXIDX && attr < GX_VA_MAX_ATTR, "invalid attr {}", static_cast<int>(attr));
  auto& fmt = g_gxState.vtxFmts[vtxfmt].attrs[attr];
  update_gx_state(fmt.cnt, cnt);
  update_gx_state(fmt.type, type);
  update_gx_state(fmt.frac, frac);
}

// TODO GXSetVtxAttrFmtv

void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride) {
  auto& array = g_gxState.arrays[attr];
  update_gx_state(array.data, data);
  update_gx_state(array.size, size);
  update_gx_state(array.stride, stride);
  array.cachedRange = {};
}

// TODO move GXBegin, GXEnd here

void GXSetTexCoordGen2(GXTexCoordID dst, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize, u32 postMtx) {
  CHECK(dst >= GX_TEXCOORD0 && dst <= GX_TEXCOORD7, "invalid tex coord {}", static_cast<int>(dst));
  update_gx_state(g_gxState.tcgs[dst],
                  {type, src, static_cast<GXTexMtx>(mtx), static_cast<GXPTTexMtx>(postMtx), normalize});
}

void GXSetNumTexGens(u8 num) { update_gx_state(g_gxState.numTexGens, num); }

void GXInvalidateVtxCache() {
  // TODO
}

void GXSetLineWidth(u8 width, GXTexOffset offs) {
  // TODO
}

// TODO GXSetPointSize
// TODO GXEnableTexOffsets

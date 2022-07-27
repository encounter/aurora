#include "gx.hpp"

void GXSetFog(GXFogType type, float startZ, float endZ, float nearZ, float farZ, GXColor color) {
  update_gx_state(g_gxState.fog, {type, startZ, endZ, nearZ, farZ, from_gx_color(color)});
}

void GXSetFogColor(GXColor color) { update_gx_state(g_gxState.fog.color, from_gx_color(color)); }

// TODO GXInitFogAdjTable
// TODO GXSetFogRangeAdj

void GXSetBlendMode(GXBlendMode mode, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op) {
  update_gx_state(g_gxState.blendMode, mode);
  update_gx_state(g_gxState.blendFacSrc, src);
  update_gx_state(g_gxState.blendFacDst, dst);
  update_gx_state(g_gxState.blendOp, op);
}

void GXSetColorUpdate(GXBool enabled) { update_gx_state(g_gxState.colorUpdate, enabled); }

void GXSetAlphaUpdate(bool enabled) { update_gx_state(g_gxState.alphaUpdate, enabled); }

void GXSetZMode(bool compare_enable, GXCompare func, bool update_enable) {
  update_gx_state(g_gxState.depthCompare, compare_enable);
  update_gx_state(g_gxState.depthFunc, func);
  update_gx_state(g_gxState.depthUpdate, update_enable);
}

void GXSetZCompLoc(GXBool before_tex) {
  // TODO
}

void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt) {}

void GXSetDither(GXBool dither) {}

void GXSetDstAlpha(bool enabled, u8 value) {
  if (enabled) {
    update_gx_state<u32>(g_gxState.dstAlpha, value);
  } else {
    update_gx_state(g_gxState.dstAlpha, UINT32_MAX);
  }
}

// TODO GXSetFieldMask
// TODO GXSetFieldMode

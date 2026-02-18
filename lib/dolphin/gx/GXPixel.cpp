#include "gx.hpp"
#include "__gx.h"

#include <cmath>

extern "C" {
void GXSetFog(GXFogType type, float startZ, float endZ, float nearZ, float farZ, GXColor color) {
  // Compute fog coefficients matching SDK
  f32 A, B, C;
  if (farZ == nearZ || endZ == startZ) {
    A = 0.0f;
    B = 0.5f;
    C = 0.0f;
  } else {
    A = (farZ * nearZ) / ((farZ - nearZ) * (endZ - startZ));
    B = farZ / (farZ - nearZ);
    C = startZ / (endZ - startZ);
  }

  // Normalize B mantissa
  f32 B_mant = B;
  u32 B_expn = 0;
  while (B_mant > 1.0f) {
    B_mant *= 0.5f;
    B_expn++;
  }
  while (B_mant > 0.0f && B_mant < 0.5f) {
    B_mant *= 2.0f;
    B_expn--;
  }

  f32 a = A / static_cast<f32>(1 << (B_expn + 1));
  u32 b_m = static_cast<u32>(8.388638e6f * B_mant);
  u32 b_s = B_expn + 1;
  f32 c = C;

  u32 a_hex, c_hex;
  std::memcpy(&a_hex, &a, sizeof(a_hex));
  std::memcpy(&c_hex, &c, sizeof(c_hex));

  // BP FOG0 (0xEE) - A parameter
  u32 fog0 = 0;
  SET_REG_FIELD(0, fog0, 11, 0, (a_hex >> 12) & 0x7FF);
  SET_REG_FIELD(0, fog0, 8, 11, (a_hex >> 23) & 0xFF);
  SET_REG_FIELD(0, fog0, 1, 19, (a_hex >> 31));
  SET_REG_FIELD(0, fog0, 8, 24, 0xEE);

  // BP FOG1 (0xEF) - B mantissa
  u32 fog1 = 0;
  SET_REG_FIELD(0, fog1, 24, 0, b_m);
  SET_REG_FIELD(0, fog1, 8, 24, 0xEF);

  // BP FOG2 (0xF0) - B scale
  u32 fog2 = 0;
  SET_REG_FIELD(0, fog2, 5, 0, b_s);
  SET_REG_FIELD(0, fog2, 8, 24, 0xF0);

  // BP FOG3 (0xF1) - C parameter + type
  u32 fog3 = 0;
  SET_REG_FIELD(0, fog3, 11, 0, (c_hex >> 12) & 0x7FF);
  SET_REG_FIELD(0, fog3, 8, 11, (c_hex >> 23) & 0xFF);
  SET_REG_FIELD(0, fog3, 1, 19, (c_hex >> 31));
  SET_REG_FIELD(0, fog3, 3, 21, type);
  SET_REG_FIELD(0, fog3, 8, 24, 0xF1);

  // BP FOGCLR (0xF2) - color
  u32 fogclr = 0;
  SET_REG_FIELD(0, fogclr, 8, 0, color.b);
  SET_REG_FIELD(0, fogclr, 8, 8, color.g);
  SET_REG_FIELD(0, fogclr, 8, 16, color.r);
  SET_REG_FIELD(0, fogclr, 8, 24, 0xF2);

  GX_WRITE_RAS_REG(fog0);
  GX_WRITE_RAS_REG(fog1);
  GX_WRITE_RAS_REG(fog2);
  GX_WRITE_RAS_REG(fog3);
  GX_WRITE_RAS_REG(fogclr);
  __gx->bpSent = 1;

  // Side channel: direct update for inline rendering (full precision)
  update_gx_state(g_gxState.fog, {type, startZ, endZ, nearZ, farZ, from_gx_color(color)});
}

void GXSetFogColor(GXColor color) {
  // BP FOGCLR (0xF2)
  u32 fogclr = 0;
  SET_REG_FIELD(0, fogclr, 8, 0, color.b);
  SET_REG_FIELD(0, fogclr, 8, 8, color.g);
  SET_REG_FIELD(0, fogclr, 8, 16, color.r);
  SET_REG_FIELD(0, fogclr, 8, 24, 0xF2);
  GX_WRITE_RAS_REG(fogclr);
  __gx->bpSent = 1;

  // Side channel: direct update for inline rendering
  update_gx_state(g_gxState.fog.color, from_gx_color(color));
}

void GXSetBlendMode(GXBlendMode mode, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op) {
  SET_REG_FIELD(0, __gx->cmode0, 1, 0, (mode == GX_BM_BLEND || mode == GX_BM_SUBTRACT));
  SET_REG_FIELD(0, __gx->cmode0, 1, 11, (mode == GX_BM_SUBTRACT));
  SET_REG_FIELD(0, __gx->cmode0, 1, 1, (mode == GX_BM_LOGIC));
  SET_REG_FIELD(0, __gx->cmode0, 4, 12, op);
  SET_REG_FIELD(0, __gx->cmode0, 3, 8, src);
  SET_REG_FIELD(0, __gx->cmode0, 3, 5, dst);
  GX_WRITE_RAS_REG(__gx->cmode0);
  __gx->bpSent = 1;
}

void GXSetColorUpdate(GXBool enabled) {
  SET_REG_FIELD(0, __gx->cmode0, 1, 3, enabled);
  GX_WRITE_RAS_REG(__gx->cmode0);
  __gx->bpSent = 1;
}

void GXSetAlphaUpdate(bool enabled) {
  SET_REG_FIELD(0, __gx->cmode0, 1, 4, enabled);
  GX_WRITE_RAS_REG(__gx->cmode0);
  __gx->bpSent = 1;
}

void GXSetZMode(bool compare_enable, GXCompare func, bool update_enable) {
  SET_REG_FIELD(0, __gx->zmode, 1, 0, compare_enable);
  SET_REG_FIELD(0, __gx->zmode, 3, 1, func);
  SET_REG_FIELD(0, __gx->zmode, 1, 4, update_enable);
  GX_WRITE_RAS_REG(__gx->zmode);
  __gx->bpSent = 1;
}

void GXSetZCompLoc(GXBool before_tex) {
  SET_REG_FIELD(0, __gx->peCtrl, 1, 6, before_tex);
  GX_WRITE_RAS_REG(__gx->peCtrl);
  __gx->bpSent = 1;
}

void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt) {
  // Stub - pixel format changes require more complex handling
}

void GXSetDither(GXBool dither) {
  SET_REG_FIELD(0, __gx->cmode0, 1, 2, dither);
  GX_WRITE_RAS_REG(__gx->cmode0);
  __gx->bpSent = 1;
}

void GXSetDstAlpha(bool enabled, u8 value) {
  SET_REG_FIELD(0, __gx->cmode1, 8, 0, value);
  SET_REG_FIELD(0, __gx->cmode1, 1, 8, enabled);
  GX_WRITE_RAS_REG(__gx->cmode1);
  __gx->bpSent = 1;
}
}

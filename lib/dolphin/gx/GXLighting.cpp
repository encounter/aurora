#include "gx.hpp"
#include "__gx.h"

extern "C" {
void GXInitLightAttn(GXLightObj* light_, float a0, float a1, float a2, float k0, float k1, float k2) {
  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->a0 = a0;
  light->a1 = a1;
  light->a2 = a2;
  light->k0 = k0;
  light->k1 = k1;
  light->k2 = k2;
}

void GXInitLightAttnA(GXLightObj* light_, float a0, float a1, float a2) {
  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->a0 = a0;
  light->a1 = a1;
  light->a2 = a2;
}

void GXInitLightAttnK(GXLightObj* light_, float k0, float k1, float k2) {
  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->k0 = k0;
  light->k1 = k1;
  light->k2 = k2;
}

void GXInitLightSpot(GXLightObj* light_, float cutoff, GXSpotFn spotFn) {
  if (cutoff <= 0.f || cutoff > 90.f) {
    spotFn = GX_SP_OFF;
  }

  float cr = std::cos((cutoff * M_PIF) / 180.f);
  float a0 = 1.f;
  float a1 = 0.f;
  float a2 = 0.f;
  switch (spotFn) {
  default:
    break;
  case GX_SP_FLAT:
    a0 = -1000.f * cr;
    a1 = 1000.f;
    a2 = 0.f;
    break;
  case GX_SP_COS:
    a0 = -cr / (1.f - cr);
    a1 = 1.f / (1.f - cr);
    a2 = 0.f;
    break;
  case GX_SP_COS2:
    a0 = 0.f;
    a1 = -cr / (1.f - cr);
    a2 = 1.f / (1.f - cr);
    break;
  case GX_SP_SHARP: {
    const float d = (1.f - cr) * (1.f - cr);
    a0 = cr * (cr - 2.f);
    a1 = 2.f / d;
    a2 = -1.f / d;
    break;
  }
  case GX_SP_RING1: {
    const float d = (1.f - cr) * (1.f - cr);
    a0 = 4.f * cr / d;
    a1 = 4.f * (1.f + cr) / d;
    a2 = -4.f / d;
    break;
  }
  case GX_SP_RING2: {
    const float d = (1.f - cr) * (1.f - cr);
    a0 = 1.f - 2.f * cr * cr / d;
    a1 = 4.f * cr / d;
    a2 = -2.f / d;
    break;
  }
  }

  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->a0 = a0;
  light->a1 = a1;
  light->a2 = a2;
}

void GXInitLightDistAttn(GXLightObj* light_, float refDistance, float refBrightness, GXDistAttnFn distFunc) {
  if (refDistance < 0.f || refBrightness < 0.f || refBrightness >= 1.f) {
    distFunc = GX_DA_OFF;
  }
  float k0 = 1.f;
  float k1 = 0.f;
  float k2 = 0.f;
  switch (distFunc) {
  case GX_DA_GENTLE:
    k0 = 1.0f;
    k1 = (1.0f - refBrightness) / (refBrightness * refDistance);
    k2 = 0.0f;
    break;
  case GX_DA_MEDIUM:
    k0 = 1.0f;
    k1 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance);
    k2 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
    break;
  case GX_DA_STEEP:
    k0 = 1.0f;
    k1 = 0.0f;
    k2 = (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
    break;
  case GX_DA_OFF:
    k0 = 1.0f;
    k1 = 0.0f;
    k2 = 0.0f;
    break;
  }

  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->k0 = k0;
  light->k1 = k1;
  light->k2 = k2;
}

void GXInitLightPos(GXLightObj* light_, float x, float y, float z) {
  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->px = x;
  light->py = y;
  light->pz = z;
}

void GXInitLightColor(GXLightObj* light_, GXColor col) {
  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->color = col;
}

void GXLoadLightObjImm(GXLightObj* light_, GXLightID id) {
  u32 idx = std::log2<u32>(id);
  auto* light = reinterpret_cast<const GXLightObj_*>(light_);

  // XF bulk write: 16 values at light base address
  // Light addresses: 0x600 + idx * 0x10
  u32 addr = 0x600 + idx * 0x10;
  u32 reg = addr | (0xF << 16); // 16-1=15 values

  // Convert color to packed u32 for XF
  u32 colorPacked = (static_cast<u32>(light->color.r) << 24) | (static_cast<u32>(light->color.g) << 16) |
                    (static_cast<u32>(light->color.b) << 8) | static_cast<u32>(light->color.a);

  GX_WRITE_U8(0x10);
  GX_WRITE_U32(reg);
  // Padding (3 u32s)
  GX_WRITE_U32(0);
  GX_WRITE_U32(0);
  GX_WRITE_U32(0);
  // Color
  GX_WRITE_U32(colorPacked);
  // Cosine attenuation (a0, a1, a2)
  GX_WRITE_F32(light->a0);
  GX_WRITE_F32(light->a1);
  GX_WRITE_F32(light->a2);
  // Distance attenuation (k0, k1, k2)
  GX_WRITE_F32(light->k0);
  GX_WRITE_F32(light->k1);
  GX_WRITE_F32(light->k2);
  // Position (px, py, pz)
  GX_WRITE_F32(light->px);
  GX_WRITE_F32(light->py);
  GX_WRITE_F32(light->pz);
  // Direction (nx, ny, nz)
  GX_WRITE_F32(light->nx);
  GX_WRITE_F32(light->ny);
  GX_WRITE_F32(light->nz);
}

void GXSetChanAmbColor(GXChannelID id, GXColor color) {
  if (id == GX_COLOR0A0) {
    GXSetChanAmbColor(GX_COLOR0, color);
    GXSetChanAmbColor(GX_ALPHA0, color);
    return;
  } else if (id == GX_COLOR1A1) {
    GXSetChanAmbColor(GX_COLOR1, color);
    GXSetChanAmbColor(GX_ALPHA1, color);
    return;
  }
  CHECK(id >= GX_COLOR0 && id <= GX_ALPHA1, "bad channel {}", static_cast<int>(id));

  // XF ambient color registers: 0x100A (chan 0), 0x100B (chan 1)
  u32 packed = (static_cast<u32>(color.r) << 24) | (static_cast<u32>(color.g) << 16) |
               (static_cast<u32>(color.b) << 8) | static_cast<u32>(color.a);
  if (id == GX_COLOR0 || id == GX_ALPHA0) {
    __gx->ambColor[0] = packed;
    GX_WRITE_XF_REG(0xA, packed);
  } else {
    __gx->ambColor[1] = packed;
    GX_WRITE_XF_REG(0xB, packed);
  }
  __gx->bpSent = 0;
}

void GXSetChanMatColor(GXChannelID id, GXColor color) {
  if (id == GX_COLOR0A0) {
    GXSetChanMatColor(GX_COLOR0, color);
    GXSetChanMatColor(GX_ALPHA0, color);
    return;
  } else if (id == GX_COLOR1A1) {
    GXSetChanMatColor(GX_COLOR1, color);
    GXSetChanMatColor(GX_ALPHA1, color);
    return;
  }
  CHECK(id >= GX_COLOR0 && id <= GX_ALPHA1, "bad channel {}", static_cast<int>(id));

  // XF material color registers: 0x100C (chan 0), 0x100D (chan 1)
  u32 packed = (static_cast<u32>(color.r) << 24) | (static_cast<u32>(color.g) << 16) |
               (static_cast<u32>(color.b) << 8) | static_cast<u32>(color.a);
  if (id == GX_COLOR0 || id == GX_ALPHA0) {
    __gx->matColor[0] = packed;
    GX_WRITE_XF_REG(0xC, packed);
  } else {
    __gx->matColor[1] = packed;
    GX_WRITE_XF_REG(0xD, packed);
  }
  __gx->bpSent = 0;
}

void GXSetNumChans(u8 num) {
  SET_REG_FIELD(0, __gx->genMode, 3, 4, num);
  GX_WRITE_XF_REG(9, num);
  __gx->dirtyState |= 4;
}

void GXInitLightDir(GXLightObj* light_, float nx, float ny, float nz) {
  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->nx = -nx;
  light->ny = -ny;
  light->nz = -nz;
}

void GXInitSpecularDir(GXLightObj* light_, float nx, float ny, float nz) {
  float hx = -nx;
  float hy = -ny;
  float hz = (-nz + 1.0f);
  float mag = ((hx * hx) + (hy * hy) + (hz * hz));
  if (mag != 0.0f) {
    mag = 1.0f / sqrtf(mag);
  }

  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->px = (nx * GX_LARGE_NUMBER);
  light->py = (ny * GX_LARGE_NUMBER);
  light->pz = (nz * GX_LARGE_NUMBER);
  light->nx = hx * mag;
  light->ny = hy * mag;
  light->nz = hz * mag;
}

void GXInitSpecularDirHA(GXLightObj* light_, float nx, float ny, float nz, float hx, float hy, float hz) {
  auto* light = reinterpret_cast<GXLightObj_*>(light_);
  light->px = (nx * GX_LARGE_NUMBER);
  light->py = (ny * GX_LARGE_NUMBER);
  light->pz = (nz * GX_LARGE_NUMBER);
  light->nx = hx;
  light->ny = hy;
  light->nz = hz;
}

void GXSetChanCtrl(GXChannelID id, bool lightingEnabled, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightState,
                   GXDiffuseFn diffFn, GXAttnFn attnFn) {
  if (id == GX_COLOR0A0) {
    GXSetChanCtrl(GX_COLOR0, lightingEnabled, ambSrc, matSrc, lightState, diffFn, attnFn);
    GXSetChanCtrl(GX_ALPHA0, lightingEnabled, ambSrc, matSrc, lightState, diffFn, attnFn);
    return;
  } else if (id == GX_COLOR1A1) {
    GXSetChanCtrl(GX_COLOR1, lightingEnabled, ambSrc, matSrc, lightState, diffFn, attnFn);
    GXSetChanCtrl(GX_ALPHA1, lightingEnabled, ambSrc, matSrc, lightState, diffFn, attnFn);
    return;
  }
  CHECK(id >= GX_COLOR0 && id <= GX_ALPHA1, "bad channel {}", static_cast<int>(id));

  // Build XF channel control register
  u32 reg = 0;
  SET_REG_FIELD(0, reg, 1, 0, matSrc);
  SET_REG_FIELD(0, reg, 1, 1, lightingEnabled);
  SET_REG_FIELD(0, reg, 4, 2, lightState & 0xF); // lights 0-3
  SET_REG_FIELD(0, reg, 1, 6, ambSrc);
  SET_REG_FIELD(0, reg, 2, 7, (attnFn == GX_AF_NONE) ? 0 : diffFn);
  SET_REG_FIELD(0, reg, 1, 9, (attnFn != GX_AF_SPEC));   // attn enable
  SET_REG_FIELD(0, reg, 1, 10, (attnFn != GX_AF_NONE));  // attn select
  SET_REG_FIELD(0, reg, 4, 11, (lightState >> 4) & 0xF); // lights 4-7

  // XF channel control registers: 0x100E-0x1011
  GX_WRITE_XF_REG(0xE + id, reg);
  __gx->bpSent = 0;
}
}

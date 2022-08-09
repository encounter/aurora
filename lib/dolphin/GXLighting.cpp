#include "gx.hpp"

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
  aurora::gfx::gx::Light realLight;
  auto* light = reinterpret_cast<const GXLightObj_*>(light_);
  realLight.pos = {light->px, light->py, light->pz};
  realLight.dir = {light->nx, light->ny, light->nz};
  realLight.color = from_gx_color(light->color);
  realLight.cosAtt = {light->a0, light->a1, light->a2};
  realLight.distAtt = {light->k0, light->k1, light->k2};
  update_gx_state(g_gxState.lights[idx], realLight);
}

// TODO GXLoadLightObjIndx

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
  update_gx_state(g_gxState.colorChannelState[id].ambColor, from_gx_color(color));
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
  update_gx_state(g_gxState.colorChannelState[id].matColor, from_gx_color(color));
}

void GXSetNumChans(u8 num) { update_gx_state(g_gxState.numChans, num); }

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
  auto& chan = g_gxState.colorChannelConfig[id];
  update_gx_state(chan.lightingEnabled, lightingEnabled);
  update_gx_state(chan.ambSrc, ambSrc);
  update_gx_state(chan.matSrc, matSrc);
  update_gx_state(chan.diffFn, diffFn);
  update_gx_state(chan.attnFn, attnFn);
  update_gx_state(g_gxState.colorChannelState[id].lightMask, GX::LightMask{lightState});
}

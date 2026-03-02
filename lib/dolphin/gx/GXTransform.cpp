#include "gx.hpp"
#include "__gx.h"
#include "dolphin/mtx/GeoTypes.h"

extern "C" {

void GXSetProjection(const void* mtx_, GXProjectionType type) {
  const auto& mtx = *reinterpret_cast<const aurora::Mat4x4<float>*>(mtx_);

  __gx->projType = type;
  __gx->projMtx[0] = mtx[0][0];
  __gx->projMtx[2] = mtx[1][1];
  __gx->projMtx[4] = mtx[2][2];
  __gx->projMtx[5] = mtx[2][3];
  if (type == GX_ORTHOGRAPHIC) {
    __gx->projMtx[1] = mtx[0][3];
    __gx->projMtx[3] = mtx[1][3];
  } else {
    __gx->projMtx[1] = mtx[0][2];
    __gx->projMtx[3] = mtx[1][2];
  }

  // XF bulk write: 6 params + projection type at 0x1020-0x1026
  GX_WRITE_U8(0x10);
  GX_WRITE_U32(0x00061020);
  GX_WRITE_XF_REG_F(32, __gx->projMtx[0]);
  GX_WRITE_XF_REG_F(33, __gx->projMtx[1]);
  GX_WRITE_XF_REG_F(34, __gx->projMtx[2]);
  GX_WRITE_XF_REG_F(35, __gx->projMtx[3]);
  GX_WRITE_XF_REG_F(36, __gx->projMtx[4]);
  GX_WRITE_XF_REG_F(37, __gx->projMtx[5]);
  GX_WRITE_XF_REG_2(38, __gx->projType);
  __gx->bpSent = 0;

  // Keep projType for GXGetProjectionv compatibility
  g_gxState.projType = type;
}

void GXLoadPosMtxImm(const void* mtx_, u32 id) {
  CHECK(id >= GX_PNMTX0 && id <= GX_PNMTX9, "invalid pn mtx {}", static_cast<int>(id));
  const auto* mtx = reinterpret_cast<const f32*>(mtx_);

  GX_WRITE_U8(0x10);
  GX_WRITE_U32((id * 4) | 0xB0000);
  for (int i = 0; i < 12; i++) {
    GX_WRITE_F32(mtx[i]);
  }
}

void GXLoadNrmMtxImm(const void* mtx_, u32 id) {
  CHECK(id >= GX_PNMTX0 && id <= GX_PNMTX9, "invalid pn mtx {}", static_cast<int>(id));
  const auto* mtx = reinterpret_cast<const f32*>(mtx_);

  GX_WRITE_U8(0x10);
  GX_WRITE_U32((id * 3 + 0x400) | 0x80000);
  // Write 3x3 from 3x4 matrix (skip translation column)
  GX_WRITE_F32(mtx[0]);
  GX_WRITE_F32(mtx[1]);
  GX_WRITE_F32(mtx[2]);
  GX_WRITE_F32(mtx[4]);
  GX_WRITE_F32(mtx[5]);
  GX_WRITE_F32(mtx[6]);
  GX_WRITE_F32(mtx[8]);
  GX_WRITE_F32(mtx[9]);
  GX_WRITE_F32(mtx[10]);
}

void GXSetCurrentMtx(u32 id) {
  CHECK(id >= GX_PNMTX0 && id <= GX_PNMTX9, "invalid pn mtx {}", id);
  SET_REG_FIELD(0, __gx->matIdxA, 6, 0, id);
  __GXSetMatrixIndex(GX_VA_PNMTXIDX);
}

void GXLoadTexMtxImm(const void* mtx_, u32 id, GXTexMtxType type) {
  CHECK((id >= GX_TEXMTX0 && id <= GX_IDENTITY) || (id >= GX_PTTEXMTX0 && id <= GX_PTIDENTITY), "invalid tex mtx {}",
        id);

  u32 addr;
  if (id >= GX_PTTEXMTX0) {
    addr = (id - GX_PTTEXMTX0) * 4 + 0x500;
    CHECK(type == GX_MTX3x4, "invalid pt mtx type {}", underlying(type));
  } else {
    addr = id * 4;
  }

  u32 count = (type == GX_MTX2x4) ? 8 : 12;
  u32 reg = addr | ((count - 1) << 16);

  GX_WRITE_U8(0x10);
  GX_WRITE_U32(reg);

  const auto* mtx = reinterpret_cast<const f32*>(mtx_);
  for (u32 i = 0; i < count; i++) {
    GX_WRITE_F32(mtx[i]);
  }
}

void GXSetViewport(float left, float top, float width, float height, float nearZ, float farZ) {
  // __gx->vpLeft = left;
  // __gx->vpTop = top;
  // __gx->vpWd = width;
  // __gx->vpHt = height;
  // __gx->vpNearz = nearZ;
  // __gx->vpFarz = farZ;

  // f32 sx = width / 2.0f;
  // f32 sy = -height / 2.0f;
  // f32 ox = 340.0f + (left + (width / 2.0f));
  // f32 oy = 340.0f + (top + (height / 2.0f));
  // f32 zmax = 1.6777215e7f * farZ;
  // f32 zmin = 1.6777215e7f * nearZ;
  // f32 sz = zmax - zmin;
  // f32 oz = zmax;

  // // XF bulk write: viewport params at 0x101A-0x101F
  // u32 reg = 0x5101A;
  // GX_WRITE_U8(0x10);
  // GX_WRITE_U32(reg);
  // GX_WRITE_XF_REG_F(26, sx);
  // GX_WRITE_XF_REG_F(27, sy);
  // GX_WRITE_XF_REG_F(28, sz);
  // GX_WRITE_XF_REG_F(29, ox);
  // GX_WRITE_XF_REG_F(30, oy);
  // GX_WRITE_XF_REG_F(31, oz);
  // __gx->bpSent = 0;

  aurora::gfx::set_viewport(left, top, width, height, nearZ, farZ);
}

void GXSetViewportJitter(float left, float top, float width, float height, float nearZ, float farZ, u32 field) {
  GXSetViewport(left, top, width, height, nearZ, farZ);
}

void GXProject(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32* pm, const f32* vp, f32* sx,
               f32* sy, f32* sz) {
  Vec peye;
  f32 xc;
  f32 yc;
  f32 zc;
  f32 wc;

  peye.x = mtx[0][3] + ((mtx[0][2] * z) + ((mtx[0][0] * x) + (mtx[0][1] * y)));
  peye.y = mtx[1][3] + ((mtx[1][2] * z) + ((mtx[1][0] * x) + (mtx[1][1] * y)));
  peye.z = mtx[2][3] + ((mtx[2][2] * z) + ((mtx[2][0] * x) + (mtx[2][1] * y)));
  if (pm[0] == 0.0f) {
    xc = (peye.x * pm[1]) + (peye.z * pm[2]);
    yc = (peye.y * pm[3]) + (peye.z * pm[4]);
    zc = pm[6] + (peye.z * pm[5]);
    wc = 1.0f / -peye.z;
  } else {
    xc = pm[2] + (peye.x * pm[1]);
    yc = pm[4] + (peye.y * pm[3]);
    zc = pm[6] + (peye.z * pm[5]);
    wc = 1.0f;
  }
  *sx = (vp[2] / 2.0f) + (vp[0] + (wc * (xc * vp[2] / 2.0f)));
  *sy = (vp[3] / 2.0f) + (vp[1] + (wc * (-yc * vp[3] / 2.0f)));
  *sz = vp[5] + (wc * (zc * (vp[5] - vp[4])));
}

// TODO GXLoadPosMtxIndx
// TODO GXLoadNrmMtxImm3x3
// TODO GXLoadNrmMtxIndx3x3
// TODO GXLoadTexMtxIndx
// TODO GXSetZScaleOffset
}

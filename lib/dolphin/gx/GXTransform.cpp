#include "gx.hpp"

extern "C" {

void GXSetProjection(const void* mtx_, GXProjectionType type) {
  const auto& mtx = *reinterpret_cast<const aurora::Mat4x4<float>*>(mtx_);
  g_gxState.projType = type;
  update_gx_state(g_gxState.proj, mtx);
}

// TODO GXSetProjectionv

void GXLoadPosMtxImm(const void* mtx_, u32 id) {
  CHECK(id >= GX_PNMTX0 && id <= GX_PNMTX9, "invalid pn mtx {}", static_cast<int>(id));
  auto& state = g_gxState.pnMtx[id / 3];
  const auto& mtx = *reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
  update_gx_state(state.pos, mtx);
}

// TODO GXLoadPosMtxIndx

void GXLoadNrmMtxImm(const void* mtx_, u32 id) {
  CHECK(id >= GX_PNMTX0 && id <= GX_PNMTX9, "invalid pn mtx {}", static_cast<int>(id));
  auto& state = g_gxState.pnMtx[id / 3];
  const auto& mtx = *reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
  update_gx_state(state.nrm, mtx);
}

// TODO GXLoadNrmMtxImm3x3
// TODO GXLoadNrmMtxIndx3x3

void GXSetCurrentMtx(u32 id) {
  CHECK(id >= GX_PNMTX0 && id <= GX_PNMTX9, "invalid pn mtx {}", id);
  update_gx_state(g_gxState.currentPnMtx, id / 3);
}

void GXLoadTexMtxImm(const void* mtx_, u32 id, GXTexMtxType type) {
  CHECK((id >= GX_TEXMTX0 && id <= GX_IDENTITY) || (id >= GX_PTTEXMTX0 && id <= GX_PTIDENTITY), "invalid tex mtx {}",
        id);
  if (id >= GX_PTTEXMTX0) {
    CHECK(type == GX_MTX3x4, "invalid pt mtx type {}", underlying(type));
    const auto idx = (id - GX_PTTEXMTX0) / 3;
    const auto& mtx = *reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
    update_gx_state(g_gxState.ptTexMtxs[idx], mtx);
  } else {
    const auto idx = (id - GX_TEXMTX0) / 3;
    switch (type) {
    case GX_MTX3x4: {
      const auto& mtx = *reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
      update_gx_state<aurora::gfx::gx::TexMtxVariant>(g_gxState.texMtxs[idx], mtx);
      break;
    }
    case GX_MTX2x4: {
      const auto& mtx = *reinterpret_cast<const aurora::Mat2x4<float>*>(mtx_);
      update_gx_state<aurora::gfx::gx::TexMtxVariant>(g_gxState.texMtxs[idx], mtx);
      break;
    }
    }
  }
}

// TODO GXLoadTexMtxIndx
// TODO GXProject

void GXSetViewport(float left, float top, float width, float height, float nearZ, float farZ) {
  aurora::gfx::set_viewport(left, top, width, height, nearZ, farZ);
}

void GXSetViewportJitter(float left, float top, float width, float height, float nearZ, float farZ, u32 field) {
  aurora::gfx::set_viewport(left, top, width, height, nearZ, farZ);
}

// TODO GXSetZScaleOffset
// TODO GXSetScissorBoxOffset
// TODO GXSetClipMode
}
#include "gx.hpp"

constexpr aurora::Mat4x4<float> DepthCorrect{
    {1.f, 0.f, 0.f, 0.f},
    {0.f, 1.f, 0.f, 0.f},
    {0.f, 0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f, 1.f},
};

void GXSetProjection(const void* mtx_, GXProjectionType type) {
  const auto& mtx = *reinterpret_cast<const aurora::Mat4x4<float>*>(mtx_);
  g_gxState.origProj = mtx;
  g_gxState.projType = type;
#ifdef AURORA_NATIVE_MATRIX
  update_gx_state(g_gxState.proj, DepthCorrect * mtx);
#else
  update_gx_state(g_gxState.proj, DepthCorrect * mtx.transpose());
#endif
}

// TODO GXSetProjectionv

void GXLoadPosMtxImm(const void* mtx_, u32 id) {
  if (id < GX_PNMTX0 || id > GX_PNMTX9) {
    Log.report(LOG_FATAL, FMT_STRING("invalid pn mtx {}"), id);
    unreachable();
  }
  auto& state = g_gxState.pnMtx[id / 3];
#ifdef AURORA_NATIVE_MATRIX
  const auto& mtx = *reinterpret_cast<const aurora::Mat4x4<float>*>(mtx_);
  update_gx_state(state.pos, mtx);
#else
  const auto* mtx = reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
  update_gx_state(state.pos, mtx->toTransposed4x4());
#endif
}

// TODO GXLoadPosMtxIndx

void GXLoadNrmMtxImm(const void* mtx_, u32 id) {
  if (id < GX_PNMTX0 || id > GX_PNMTX9) {
    Log.report(LOG_FATAL, FMT_STRING("invalid pn mtx {}"), id);
    unreachable();
  }
  auto& state = g_gxState.pnMtx[id / 3];
#ifdef AURORA_NATIVE_MATRIX
  const auto& mtx = *reinterpret_cast<const aurora::Mat4x4<float>*>(mtx_);
  update_gx_state(state.nrm, mtx);
#else
  const auto* mtx = reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
  update_gx_state(state.nrm, mtx->toTransposed4x4());
#endif
}

// TODO GXLoadNrmMtxImm3x3
// TODO GXLoadNrmMtxIndx3x3

void GXSetCurrentMtx(u32 id) {
  if (id < GX_PNMTX0 || id > GX_PNMTX9) {
    Log.report(LOG_FATAL, FMT_STRING("invalid pn mtx {}"), id);
    unreachable();
  }
  update_gx_state(g_gxState.currentPnMtx, id / 3);
}

void GXLoadTexMtxImm(const void* mtx_, u32 id, GXTexMtxType type) {
  if ((id < GX_TEXMTX0 || id > GX_IDENTITY) && (id < GX_PTTEXMTX0 || id > GX_PTIDENTITY)) {
    Log.report(LOG_FATAL, FMT_STRING("invalid tex mtx {}"), id);
    unreachable();
  }
  if (id >= GX_PTTEXMTX0) {
    if (type != GX_MTX3x4) {
      Log.report(LOG_FATAL, FMT_STRING("invalid pt mtx type {}"), type);
      unreachable();
    }
    const auto idx = (id - GX_PTTEXMTX0) / 3;
#ifdef AURORA_NATIVE_MATRIX
    const auto& mtx = *reinterpret_cast<const aurora::Mat4x4<float>*>(mtx_);
    update_gx_state<aurora::Mat4x4<float>>(g_gxState.ptTexMtxs[idx], mtx);
#else
    const auto& mtx = *reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
    update_gx_state<aurora::Mat4x4<float>>(g_gxState.ptTexMtxs[idx], mtx.toTransposed4x4());
#endif
  } else {
    const auto idx = (id - GX_TEXMTX0) / 3;
    switch (type) {
    case GX_MTX3x4: {
#ifdef AURORA_NATIVE_MATRIX
      const auto& mtx = *reinterpret_cast<const aurora::Mat4x4<float>*>(mtx_);
      update_gx_state<aurora::gfx::gx::TexMtxVariant>(g_gxState.texMtxs[idx], mtx);
#else
      const auto& mtx = *reinterpret_cast<const aurora::Mat3x4<float>*>(mtx_);
      update_gx_state<aurora::gfx::gx::TexMtxVariant>(g_gxState.texMtxs[idx], mtx.toTransposed4x4());
#endif
      break;
    }
    case GX_MTX2x4: {
      const auto& mtx = *reinterpret_cast<const aurora::Mat4x2<float>*>(mtx_);
#ifdef AURORA_NATIVE_MATRIX
      update_gx_state<aurora::gfx::gx::TexMtxVariant>(g_gxState.texMtxs[idx], mtx);
#else
      update_gx_state<aurora::gfx::gx::TexMtxVariant>(g_gxState.texMtxs[idx], mtx.transpose());
#endif
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

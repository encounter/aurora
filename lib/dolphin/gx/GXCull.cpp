#include "gx.hpp"
#include "__gx.h"

extern "C" {

void GXSetScissor(u32 left, u32 top, u32 width, u32 height) {
  u32 tp = top + 340;
  u32 lf = left + 340;
  u32 bm = tp + height - 1;
  u32 rt = lf + width - 1;

  SET_REG_FIELD(0, __gx->suScis0, 11, 0, tp);
  SET_REG_FIELD(0, __gx->suScis0, 11, 12, lf);
  SET_REG_FIELD(0, __gx->suScis1, 11, 0, bm);
  SET_REG_FIELD(0, __gx->suScis1, 11, 12, rt);

  GX_WRITE_RAS_REG(__gx->suScis0);
  GX_WRITE_RAS_REG(__gx->suScis1);
  __gx->bpSent = 1;

  aurora::gfx::set_scissor(left, top, width, height);
}

void GXSetCullMode(GXCullMode mode) {
  // Swap front/back to match hardware convention
  GXCullMode hwMode;
  switch (mode) {
  case GX_CULL_FRONT:
    hwMode = GX_CULL_BACK;
    break;
  case GX_CULL_BACK:
    hwMode = GX_CULL_FRONT;
    break;
  default:
    hwMode = mode;
    break;
  }
  SET_REG_FIELD(0, __gx->genMode, 2, 14, hwMode);
  __gx->dirtyState |= 4; // gen mode dirty
}

// TODO GXSetCoPlanar
}

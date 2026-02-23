#include "gx.hpp"
#include "__gx.h"

extern "C" {

void GXSetScissor(u32 left, u32 top, u32 width, u32 height) {
  // u32 tp = top + 340;
  // u32 lf = left + 340;
  // u32 bm = tp + height - 1;
  // u32 rt = lf + width - 1;

  // SET_REG_FIELD(0, __gx->suScis0, 11, 0, tp);
  // SET_REG_FIELD(0, __gx->suScis0, 11, 12, lf);
  // SET_REG_FIELD(0, __gx->suScis1, 11, 0, bm);
  // SET_REG_FIELD(0, __gx->suScis1, 11, 12, rt);

  // GX_WRITE_RAS_REG(__gx->suScis0);
  // GX_WRITE_RAS_REG(__gx->suScis1);
  // __gx->bpSent = 1;

  aurora::gfx::set_scissor(left, top, width, height);
}

// TODO GXSetScissorBoxOffset

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

void GXSetClipMode(GXClipMode mode) {
  GX_WRITE_XF_REG(5, mode);
  __gx->bpSent = 1;
}

void GXSetCoPlanar(GXBool enable) {
  u32 reg;
  SET_REG_FIELD(0, __gx->genMode, 1, 19, enable);

  // This sets a mask that causes only one bit to be updated in genMode on the next write.
  reg = 0x0008000;
  SET_REG_FIELD(0, reg, 8, 24, 0xFE);
  SET_REG_FIELD(0, reg, 24, 0, 1 << 19);
  GX_WRITE_RAS_REG(reg);

  // immediately flush
  GX_WRITE_RAS_REG(__gx->genMode);
}
}

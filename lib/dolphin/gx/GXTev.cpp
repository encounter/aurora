#include "gx.hpp"
#include "__gx.h"

extern "C" {
void GXSetTevOp(GXTevStageID id, GXTevMode mode) {
  GXTevColorArg inputColor = GX_CC_RASC;
  GXTevAlphaArg inputAlpha = GX_CA_RASA;
  if (id != GX_TEVSTAGE0) {
    inputColor = GX_CC_CPREV;
    inputAlpha = GX_CA_APREV;
  }
  switch (mode) {
  case GX_MODULATE:
    GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_TEXC, inputColor, GX_CC_ZERO);
    GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_TEXA, inputAlpha, GX_CA_ZERO);
    break;
  case GX_DECAL:
    GXSetTevColorIn(id, inputColor, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
    GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, inputAlpha);
    break;
  case GX_BLEND:
    GXSetTevColorIn(id, inputColor, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO);
    GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_TEXA, inputAlpha, GX_CA_ZERO);
    break;
  case GX_REPLACE:
    GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
    break;
  case GX_PASSCLR:
    GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, inputColor);
    GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, inputAlpha);
    break;
  }
  GXSetTevColorOp(id, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(id, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

void GXSetTevColorIn(GXTevStageID stageId, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {
  u32* reg = &__gx->tevc[stageId];
  SET_REG_FIELD(0, *reg, 4, 12, a);
  SET_REG_FIELD(0, *reg, 4, 8, b);
  SET_REG_FIELD(0, *reg, 4, 4, c);
  SET_REG_FIELD(0, *reg, 4, 0, d);
  GX_WRITE_RAS_REG(*reg);
  __gx->bpSent = 1;
}

void GXSetTevAlphaIn(GXTevStageID stageId, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
  u32* reg = &__gx->teva[stageId];
  SET_REG_FIELD(0, *reg, 3, 13, a);
  SET_REG_FIELD(0, *reg, 3, 10, b);
  SET_REG_FIELD(0, *reg, 3, 7, c);
  SET_REG_FIELD(0, *reg, 3, 4, d);
  GX_WRITE_RAS_REG(*reg);
  __gx->bpSent = 1;
}

void GXSetTevColorOp(GXTevStageID stageId, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp,
                     GXTevRegID outReg) {
  u32* reg = &__gx->tevc[stageId];
  SET_REG_FIELD(0, *reg, 1, 18, op & 1);
  if (op <= 1) {
    SET_REG_FIELD(0, *reg, 2, 20, scale);
    SET_REG_FIELD(0, *reg, 2, 16, bias);
  } else {
    SET_REG_FIELD(0, *reg, 2, 20, (op >> 1) & 3);
    SET_REG_FIELD(0, *reg, 2, 16, 3);
  }
  SET_REG_FIELD(0, *reg, 1, 19, clamp);
  SET_REG_FIELD(0, *reg, 2, 22, outReg);
  GX_WRITE_RAS_REG(*reg);
  __gx->bpSent = 1;
}

void GXSetTevAlphaOp(GXTevStageID stageId, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp,
                     GXTevRegID outReg) {
  u32* reg = &__gx->teva[stageId];
  SET_REG_FIELD(0, *reg, 1, 18, op & 1);
  if (op <= 1) {
    SET_REG_FIELD(0, *reg, 2, 20, scale);
    SET_REG_FIELD(0, *reg, 2, 16, bias);
  } else {
    SET_REG_FIELD(0, *reg, 2, 20, (op >> 1) & 3);
    SET_REG_FIELD(0, *reg, 2, 16, 3);
  }
  SET_REG_FIELD(0, *reg, 1, 19, clamp);
  SET_REG_FIELD(0, *reg, 2, 22, outReg);
  GX_WRITE_RAS_REG(*reg);
  __gx->bpSent = 1;
}

void GXSetTevColor(GXTevRegID id, GXColor color) {
  CHECK(id >= GX_TEVPREV && id < GX_MAX_TEVREG, "bad tevreg {}", static_cast<int>(id));

  // Write BP registers (RA + BG pairs) - needed for display list capture
  u32 regRA = 0;
  SET_REG_FIELD(0, regRA, 11, 0, color.r);
  SET_REG_FIELD(0, regRA, 11, 12, color.a);
  SET_REG_FIELD(0, regRA, 8, 24, 0xE0 + id * 2);
  u32 regBG = 0;
  SET_REG_FIELD(0, regBG, 11, 0, color.b);
  SET_REG_FIELD(0, regBG, 11, 12, color.g);
  SET_REG_FIELD(0, regBG, 8, 24, 0xE1 + id * 2);
  GX_WRITE_RAS_REG(regRA);
  GX_WRITE_RAS_REG(regBG);
  // NOTE: The SDK writes regBG three additional times here for hardware timing.
  // We omit the redundant writes since they don't change the register value and
  // our software command processor doesn't need the sync delay.
  __gx->bpSent = 1;

  // Side channel: direct update for inline rendering (full precision)
  update_gx_state(g_gxState.colorRegs[id], from_gx_color(color));
}

void GXSetTevColorS10(GXTevRegID id, GXColorS10 color) {
  // Write BP registers (RA + BG pairs) - needed for display list capture
  u32 regRA = 0;
  SET_REG_FIELD(0, regRA, 11, 0, color.r & 0x7FF);
  SET_REG_FIELD(0, regRA, 11, 12, color.a & 0x7FF);
  SET_REG_FIELD(0, regRA, 8, 24, 0xE0 + id * 2);
  u32 regBG = 0;
  SET_REG_FIELD(0, regBG, 11, 0, color.b & 0x7FF);
  SET_REG_FIELD(0, regBG, 11, 12, color.g & 0x7FF);
  SET_REG_FIELD(0, regBG, 8, 24, 0xE1 + id * 2);
  GX_WRITE_RAS_REG(regRA);
  GX_WRITE_RAS_REG(regBG);
  // NOTE: The SDK writes regBG three additional times here for hardware timing.
  // We omit the redundant writes since they don't change the register value and
  // our software command processor doesn't need the sync delay.
  __gx->bpSent = 1;

  // Side channel: direct update for inline rendering (full precision)
  update_gx_state(g_gxState.colorRegs[id], aurora::Vec4<float>{
                                               static_cast<float>(color.r) / 255.f,
                                               static_cast<float>(color.g) / 255.f,
                                               static_cast<float>(color.b) / 255.f,
                                               static_cast<float>(color.a) / 255.f,
                                           });
}

void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
  // BP register 0xF3
  u32 reg = 0;
  SET_REG_FIELD(0, reg, 8, 0, ref0);
  SET_REG_FIELD(0, reg, 8, 8, ref1);
  SET_REG_FIELD(0, reg, 3, 16, comp0);
  SET_REG_FIELD(0, reg, 3, 19, comp1);
  SET_REG_FIELD(0, reg, 2, 22, op);
  SET_REG_FIELD(0, reg, 8, 24, 0xF3);
  GX_WRITE_RAS_REG(reg);
  __gx->bpSent = 1;
}

void GXSetTevOrder(GXTevStageID id, GXTexCoordID tcid, GXTexMapID tmid, GXChannelID cid) {
  // Channel ID mapping to hardware register values
  static const u8 c2r[] = {0, 1, 0, 1, 0, 1, 7, 5, 6};
  u32* ptref = &__gx->tref[id / 2];
  __gx->texmapId[id] = tmid;

  u32 tmap = tmid & ~0x100u;
  tmap = (tmap >= GX_MAX_TEXMAP) ? GX_TEXMAP0 : tmap;
  u32 tcoord = (tcid >= GX_MAX_TEXCOORD) ? GX_TEXCOORD0 : tcid;

  if (id & 1) {
    SET_REG_FIELD(0, *ptref, 3, 12, tmap);
    SET_REG_FIELD(0, *ptref, 3, 15, tcoord);
    SET_REG_FIELD(0, *ptref, 3, 19, c2r[cid]);
    SET_REG_FIELD(0, *ptref, 1, 18, (tmid != GX_TEXMAP_NULL && !(tmid & 0x100)));
  } else {
    SET_REG_FIELD(0, *ptref, 3, 0, tmap);
    SET_REG_FIELD(0, *ptref, 3, 3, tcoord);
    SET_REG_FIELD(0, *ptref, 3, 7, c2r[cid]);
    SET_REG_FIELD(0, *ptref, 1, 6, (tmid != GX_TEXMAP_NULL && !(tmid & 0x100)));
  }

  GX_WRITE_RAS_REG(*ptref);
  __gx->bpSent = 1;
  __gx->dirtyState |= 1; // SU tex regs dirty
}

void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias) {
  // TODO
}

void GXSetNumTevStages(u8 num) {
  SET_REG_FIELD(0, __gx->genMode, 4, 10, num - 1);
  __gx->dirtyState |= 4; // gen mode dirty
}

void GXSetTevKColor(GXTevKColorID id, GXColor color) {
  CHECK(id >= GX_KCOLOR0 && id < GX_MAX_KCOLOR, "bad kcolor {}", static_cast<int>(id));

  // Write BP registers (RA + BG pairs with bit 23 set for K color)
  u32 regRA = 0;
  SET_REG_FIELD(0, regRA, 8, 0, color.r);
  SET_REG_FIELD(0, regRA, 8, 12, color.a);
  SET_REG_FIELD(0, regRA, 4, 20, 8); // K color flag
  SET_REG_FIELD(0, regRA, 8, 24, 0xE0 + id * 2);
  u32 regBG = 0;
  SET_REG_FIELD(0, regBG, 8, 0, color.b);
  SET_REG_FIELD(0, regBG, 8, 12, color.g);
  SET_REG_FIELD(0, regBG, 4, 20, 8); // K color flag
  SET_REG_FIELD(0, regBG, 8, 24, 0xE1 + id * 2);
  GX_WRITE_RAS_REG(regRA);
  GX_WRITE_RAS_REG(regBG);
  __gx->bpSent = 1;

  // Side channel: direct update for inline rendering (full precision)
  update_gx_state(g_gxState.kcolors[id], from_gx_color(color));
}

void GXSetTevKColorSel(GXTevStageID id, GXTevKColorSel sel) {
  // tevKsel registers: 2 stages per register
  u32 kselIdx = id / 2;
  if (id & 1) {
    SET_REG_FIELD(0, __gx->tevKsel[kselIdx], 5, 14, sel);
  } else {
    SET_REG_FIELD(0, __gx->tevKsel[kselIdx], 5, 4, sel);
  }
  GX_WRITE_RAS_REG(__gx->tevKsel[kselIdx]);
  __gx->bpSent = 1;
}

void GXSetTevKAlphaSel(GXTevStageID id, GXTevKAlphaSel sel) {
  u32 kselIdx = id / 2;
  if (id & 1) {
    SET_REG_FIELD(0, __gx->tevKsel[kselIdx], 5, 19, sel);
  } else {
    SET_REG_FIELD(0, __gx->tevKsel[kselIdx], 5, 9, sel);
  }
  GX_WRITE_RAS_REG(__gx->tevKsel[kselIdx]);
  __gx->bpSent = 1;
}

void GXSetTevSwapMode(GXTevStageID stageId, GXTevSwapSel rasSel, GXTevSwapSel texSel) {
  // Swap mode is stored in teva register
  u32* reg = &__gx->teva[stageId];
  SET_REG_FIELD(0, *reg, 2, 0, rasSel);
  SET_REG_FIELD(0, *reg, 2, 2, texSel);
  GX_WRITE_RAS_REG(*reg);
  __gx->bpSent = 1;
}

void GXSetTevSwapModeTable(GXTevSwapSel id, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue,
                           GXTevColorChan alpha) {
  CHECK(id >= GX_TEV_SWAP0 && id < GX_MAX_TEVSWAP, "bad tev swap sel {}", static_cast<int>(id));
  // Swap table is stored in tevKsel registers
  u32 kselIdx = id * 2;
  SET_REG_FIELD(0, __gx->tevKsel[kselIdx], 2, 0, red);
  SET_REG_FIELD(0, __gx->tevKsel[kselIdx], 2, 2, green);
  GX_WRITE_RAS_REG(__gx->tevKsel[kselIdx]);

  SET_REG_FIELD(0, __gx->tevKsel[kselIdx + 1], 2, 0, blue);
  SET_REG_FIELD(0, __gx->tevKsel[kselIdx + 1], 2, 2, alpha);
  GX_WRITE_RAS_REG(__gx->tevKsel[kselIdx + 1]);
  __gx->bpSent = 1;
}
}

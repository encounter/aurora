#include "gx.hpp"

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
  update_gx_state(g_gxState.tevStages[stageId].colorPass, {a, b, c, d});
}

void GXSetTevAlphaIn(GXTevStageID stageId, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
  update_gx_state(g_gxState.tevStages[stageId].alphaPass, {a, b, c, d});
}

void GXSetTevColorOp(GXTevStageID stageId, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp,
                     GXTevRegID outReg) {
  update_gx_state(g_gxState.tevStages[stageId].colorOp, {op, bias, scale, outReg, clamp});
}

void GXSetTevAlphaOp(GXTevStageID stageId, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp,
                     GXTevRegID outReg) {
  update_gx_state(g_gxState.tevStages[stageId].alphaOp, {op, bias, scale, outReg, clamp});
}

void GXSetTevColor(GXTevRegID id, GXColor color) {
  CHECK(id >= GX_TEVPREV && id < GX_MAX_TEVREG, "bad tevreg {}", static_cast<int>(id));
  update_gx_state(g_gxState.colorRegs[id], from_gx_color(color));
}

void GXSetTevColorS10(GXTevRegID id, GXColorS10 color) {
  update_gx_state(g_gxState.colorRegs[id], aurora::Vec4<float>{
                                               static_cast<float>(color.r) / 255.f,
                                               static_cast<float>(color.g) / 255.f,
                                               static_cast<float>(color.b) / 255.f,
                                               static_cast<float>(color.a) / 255.f,
                                           });
}

void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
  update_gx_state(g_gxState.alphaCompare, {comp0, ref0, op, comp1, ref1});
}

void GXSetTevOrder(GXTevStageID id, GXTexCoordID tcid, GXTexMapID tmid, GXChannelID cid) {
  auto& stage = g_gxState.tevStages[id];
  update_gx_state(stage.texCoordId, tcid);
  update_gx_state(stage.texMapId, tmid);
  update_gx_state(stage.channelId, cid);
}

// TODO GXSetZTexture

void GXSetNumTevStages(u8 num) { update_gx_state(g_gxState.numTevStages, num); }

void GXSetTevKColor(GXTevKColorID id, GXColor color) {
  CHECK(id >= GX_KCOLOR0 && id < GX_MAX_KCOLOR, "bad kcolor {}", static_cast<int>(id));
  update_gx_state(g_gxState.kcolors[id], from_gx_color(color));
}

void GXSetTevKColorSel(GXTevStageID id, GXTevKColorSel sel) { update_gx_state(g_gxState.tevStages[id].kcSel, sel); }

void GXSetTevKAlphaSel(GXTevStageID id, GXTevKAlphaSel sel) { update_gx_state(g_gxState.tevStages[id].kaSel, sel); }

void GXSetTevSwapMode(GXTevStageID stageId, GXTevSwapSel rasSel, GXTevSwapSel texSel) {
  auto& stage = g_gxState.tevStages[stageId];
  update_gx_state(stage.tevSwapRas, rasSel);
  update_gx_state(stage.tevSwapTex, texSel);
}

void GXSetTevSwapModeTable(GXTevSwapSel id, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue,
                           GXTevColorChan alpha) {
  CHECK(id >= GX_TEV_SWAP0 && id < GX_MAX_TEVSWAP, "bad tev swap sel {}", static_cast<int>(id));
  update_gx_state(g_gxState.tevSwapTable[id], {red, green, blue, alpha});
}

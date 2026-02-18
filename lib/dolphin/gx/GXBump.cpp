#include "gx.hpp"
#include "__gx.h"

extern "C" {
void GXSetNumIndStages(u8 num) {
  SET_REG_FIELD(0, __gx->genMode, 3, 16, num);
  __gx->dirtyState |= 6; // gen mode dirty + BP mask dirty
}

void GXSetIndTexOrder(GXIndTexStageID indStage, GXTexCoordID texCoord, GXTexMapID texMap) {
  switch (indStage) {
  case GX_INDTEXSTAGE0:
    SET_REG_FIELD(0, __gx->iref, 3, 0, texMap);
    SET_REG_FIELD(0, __gx->iref, 3, 3, texCoord);
    break;
  case GX_INDTEXSTAGE1:
    SET_REG_FIELD(0, __gx->iref, 3, 6, texMap);
    SET_REG_FIELD(0, __gx->iref, 3, 9, texCoord);
    break;
  case GX_INDTEXSTAGE2:
    SET_REG_FIELD(0, __gx->iref, 3, 12, texMap);
    SET_REG_FIELD(0, __gx->iref, 3, 15, texCoord);
    break;
  case GX_INDTEXSTAGE3:
    SET_REG_FIELD(0, __gx->iref, 3, 18, texMap);
    SET_REG_FIELD(0, __gx->iref, 3, 21, texCoord);
    break;
  }
  GX_WRITE_RAS_REG(__gx->iref);
  __gx->bpSent = 1;
  __gx->dirtyState |= 3; // SU tex regs + BP mask dirty
}

void GXSetIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT) {
  if (indStage < GX_INDTEXSTAGE2) {
    switch (indStage) {
    case GX_INDTEXSTAGE0:
      SET_REG_FIELD(0, __gx->IndTexScale0, 4, 0, scaleS);
      SET_REG_FIELD(0, __gx->IndTexScale0, 4, 4, scaleT);
      break;
    case GX_INDTEXSTAGE1:
      SET_REG_FIELD(0, __gx->IndTexScale0, 4, 8, scaleS);
      SET_REG_FIELD(0, __gx->IndTexScale0, 4, 12, scaleT);
      break;
    }
    GX_WRITE_RAS_REG(__gx->IndTexScale0);
  } else {
    switch (indStage) {
    case GX_INDTEXSTAGE2:
      SET_REG_FIELD(0, __gx->IndTexScale1, 4, 0, scaleS);
      SET_REG_FIELD(0, __gx->IndTexScale1, 4, 4, scaleT);
      break;
    case GX_INDTEXSTAGE3:
      SET_REG_FIELD(0, __gx->IndTexScale1, 4, 8, scaleS);
      SET_REG_FIELD(0, __gx->IndTexScale1, 4, 12, scaleT);
      break;
    }
    GX_WRITE_RAS_REG(__gx->IndTexScale1);
  }
  __gx->bpSent = 1;
}

void GXSetIndTexMtx(GXIndTexMtxID id, const void* offset, s8 scaleExp) {
  CHECK(id >= GX_ITM_0 && id <= GX_ITM_2, "invalid ind tex mtx ID {}", static_cast<int>(id));

  const auto* mtx = reinterpret_cast<const f32*>(offset);
  s32 adjScale = scaleExp + 17;

  // Write 3 BP registers for the 2x3 matrix
  u32 idx = id - 1;

  // Row 0: m[0][0] and m[1][0]
  u32 reg0 = 0;
  s32 m00 = static_cast<s32>(1024.0f * mtx[0]) & 0x7FF;
  s32 m10 = static_cast<s32>(1024.0f * mtx[3]) & 0x7FF;
  SET_REG_FIELD(0, reg0, 11, 0, m00);
  SET_REG_FIELD(0, reg0, 11, 11, m10);
  SET_REG_FIELD(0, reg0, 2, 22, adjScale & 3);
  SET_REG_FIELD(0, reg0, 8, 24, idx * 3 + 6);
  GX_WRITE_RAS_REG(reg0);

  // Row 1: m[0][1] and m[1][1]
  u32 reg1 = 0;
  s32 m01 = static_cast<s32>(1024.0f * mtx[1]) & 0x7FF;
  s32 m11 = static_cast<s32>(1024.0f * mtx[4]) & 0x7FF;
  SET_REG_FIELD(0, reg1, 11, 0, m01);
  SET_REG_FIELD(0, reg1, 11, 11, m11);
  SET_REG_FIELD(0, reg1, 2, 22, (adjScale >> 2) & 3);
  SET_REG_FIELD(0, reg1, 8, 24, idx * 3 + 7);
  GX_WRITE_RAS_REG(reg1);

  // Row 2: m[0][2] and m[1][2]
  u32 reg2 = 0;
  s32 m02 = static_cast<s32>(1024.0f * mtx[2]) & 0x7FF;
  s32 m12 = static_cast<s32>(1024.0f * mtx[5]) & 0x7FF;
  SET_REG_FIELD(0, reg2, 11, 0, m02);
  SET_REG_FIELD(0, reg2, 11, 11, m12);
  SET_REG_FIELD(0, reg2, 2, 22, (adjScale >> 4) & 3);
  SET_REG_FIELD(0, reg2, 8, 24, idx * 3 + 8);
  GX_WRITE_RAS_REG(reg2);
  __gx->bpSent = 1;
}

void GXSetTevIndirect(GXTevStageID tevStage, GXIndTexStageID indStage, GXIndTexFormat fmt, GXIndTexBiasSel biasSel,
                      GXIndTexMtxID matrixSel, GXIndTexWrap wrapS, GXIndTexWrap wrapT, GXBool addPrev, GXBool indLod,
                      GXIndTexAlphaSel alphaSel) {
  // BP register: tev_stage + 0x10
  u32 reg = 0;
  SET_REG_FIELD(0, reg, 2, 0, indStage);
  SET_REG_FIELD(0, reg, 2, 2, fmt);
  SET_REG_FIELD(0, reg, 3, 4, biasSel);
  SET_REG_FIELD(0, reg, 2, 7, alphaSel);
  SET_REG_FIELD(0, reg, 4, 9, matrixSel);
  SET_REG_FIELD(0, reg, 3, 13, wrapS);
  SET_REG_FIELD(0, reg, 3, 16, wrapT);
  SET_REG_FIELD(0, reg, 1, 19, indLod);
  SET_REG_FIELD(0, reg, 1, 20, addPrev);
  SET_REG_FIELD(0, reg, 8, 24, tevStage + 16);
  GX_WRITE_RAS_REG(reg);
  __gx->bpSent = 1;
}

void GXSetTevDirect(GXTevStageID stageId) {
  GXSetTevIndirect(stageId, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE,
                   GX_FALSE, GX_ITBA_OFF);
}

void GXSetTevIndWarp(GXTevStageID tevStage, GXIndTexStageID indStage, GXBool signedOffsets, GXBool replaceMode,
                     GXIndTexMtxID matrixSel) {
  const auto wrap = replaceMode ? GX_ITW_0 : GX_ITW_OFF;
  const auto biasSel = signedOffsets ? GX_ITB_STU : GX_ITB_NONE;
  GXSetTevIndirect(tevStage, indStage, GX_ITF_8, biasSel, matrixSel, wrap, wrap, false, false, GX_ITBA_OFF);
}

// TODO GXSetTevIndTile
// TODO GXSetTevIndBumpST
// TODO GXSetTevIndBumpXYZ
// TODO GXSetTevIndRepeat
}

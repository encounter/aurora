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

  // Column 0: m[0][0] and m[1][0]
  u32 reg0 = 0;
  s32 m00 = static_cast<s32>(1024.0f * mtx[0]) & 0x7FF;
  s32 m10 = static_cast<s32>(1024.0f * mtx[3]) & 0x7FF;
  SET_REG_FIELD(0, reg0, 11, 0, m00);
  SET_REG_FIELD(0, reg0, 11, 11, m10);
  SET_REG_FIELD(0, reg0, 2, 22, adjScale & 3);
  SET_REG_FIELD(0, reg0, 8, 24, idx * 3 + 6);
  GX_WRITE_RAS_REG(reg0);

  // Column 1: m[0][1] and m[1][1]
  u32 reg1 = 0;
  s32 m01 = static_cast<s32>(1024.0f * mtx[1]) & 0x7FF;
  s32 m11 = static_cast<s32>(1024.0f * mtx[4]) & 0x7FF;
  SET_REG_FIELD(0, reg1, 11, 0, m01);
  SET_REG_FIELD(0, reg1, 11, 11, m11);
  SET_REG_FIELD(0, reg1, 2, 22, (adjScale >> 2) & 3);
  SET_REG_FIELD(0, reg1, 8, 24, idx * 3 + 7);
  GX_WRITE_RAS_REG(reg1);

  // Column 2: m[0][2] and m[1][2]
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

void GXSetTevIndRepeat(GXTevStageID tevStage) {
  GXSetTevIndirect(tevStage, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_0, GX_ITW_0, GX_TRUE,
                   GX_FALSE, GX_ITBA_OFF);
}

void GXSetTevIndBumpXYZ(GXTevStageID tevStage, GXIndTexStageID indStage, GXIndTexMtxID matrixSel) {
  GXSetTevIndirect(tevStage, indStage, GX_ITF_8, GX_ITB_STU, matrixSel, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE,
                   GX_ITBA_OFF);
}

void GXSetTevIndBumpST(GXTevStageID tevStage, GXIndTexStageID indStage, GXIndTexMtxID matrixSel) {
  GXIndTexMtxID sMtx, tMtx;
  switch (matrixSel) {
  case GX_ITM_0:
    sMtx = GX_ITM_S0;
    tMtx = GX_ITM_T0;
    break;
  case GX_ITM_1:
    sMtx = GX_ITM_S1;
    tMtx = GX_ITM_T1;
    break;
  case GX_ITM_2:
    sMtx = GX_ITM_S2;
    tMtx = GX_ITM_T2;
    break;
  default:
    sMtx = GX_ITM_OFF;
    tMtx = GX_ITM_OFF;
    break;
  }
  // Stage 0: STU bias, S-dynamic matrix, wrap=0/0
  GXSetTevIndirect(tevStage, indStage, GX_ITF_8, GX_ITB_STU, sMtx, GX_ITW_0, GX_ITW_0, GX_FALSE, GX_FALSE,
                   GX_ITBA_OFF);
  // Stage 1: STU bias, T-dynamic matrix, wrap=0/0, add prev
  GXSetTevIndirect(static_cast<GXTevStageID>(tevStage + 1), indStage, GX_ITF_8, GX_ITB_STU, tMtx, GX_ITW_0, GX_ITW_0,
                   GX_TRUE, GX_FALSE, GX_ITBA_OFF);
  // Stage 2: no bias, matrix off, wrap off, add prev
  GXSetTevIndirect(static_cast<GXTevStageID>(tevStage + 2), indStage, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_OFF,
                   GX_ITW_OFF, GX_TRUE, GX_FALSE, GX_ITBA_OFF);
}

void GXSetTevIndTile(GXTevStageID tevStage, GXIndTexStageID indStage, u16 tileSizeS, u16 tileSizeT, u16 tileSizeS_exp,
                     u16 tileSizeT_exp, GXIndTexFormat fmt, GXIndTexMtxID matrixSel, GXIndTexBiasSel biasSel,
                     GXIndTexAlphaSel alphaSel) {
  // Convert tile size exponents to wrap modes
  auto sizeToWrap = [](u16 sizeExp) -> GXIndTexWrap {
    switch (sizeExp) {
    case 8: return GX_ITW_256;
    case 7: return GX_ITW_128;
    case 6: return GX_ITW_64;
    case 5: return GX_ITW_32;
    case 4: return GX_ITW_16;
    default: return GX_ITW_0;
    }
  };
  GXIndTexWrap wrapS = sizeToWrap(tileSizeS_exp);
  GXIndTexWrap wrapT = sizeToWrap(tileSizeT_exp);

  // Set up the indirect matrix for tiling
  f32 mtx[2][3] = {};
  s8 scaleExp = 0;
  if (biasSel == GX_ITB_S || biasSel == GX_ITB_ST || biasSel == GX_ITB_SU || biasSel == GX_ITB_STU) {
    mtx[0][0] = static_cast<f32>(tileSizeS);
    scaleExp = tileSizeS_exp;
  }
  if (biasSel == GX_ITB_T || biasSel == GX_ITB_ST || biasSel == GX_ITB_TU || biasSel == GX_ITB_STU) {
    mtx[1][1] = static_cast<f32>(tileSizeT);
    scaleExp = tileSizeT_exp;
  }
  GXSetIndTexMtx(matrixSel, mtx, scaleExp);
  GXSetTevIndirect(tevStage, indStage, fmt, biasSel, matrixSel, wrapS, wrapT, GX_FALSE, GX_FALSE, alphaSel);
}
}

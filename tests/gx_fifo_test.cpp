// GX FIFO encode/decode round-trip tests
//
// Pattern: call a GX API function (encode), capture the raw FIFO bytes,
// reset g_gxState, feed bytes to command_processor::process() (decode),
// validate the decoded state matches expected values.

#include "gx_test_common.hpp"

#include <cmath>

using aurora::gfx::gx::g_gxState;

// ============================================================================
// BP registers (direct FIFO writes, no dirty state flush needed)
// ============================================================================

// --- GXSetBlendMode (BP 0x41) ---

TEST_F(GXFifoTest, BlendMode_Blend_SrcAlpha) {
  GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
  auto bytes = capture_fifo();

  // Validate encoding: BP opcode 0x61, register ID 0x41
  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x61);
  EXPECT_EQ(bytes[1], 0x41);

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.blendMode, GX_BM_BLEND);
  EXPECT_EQ(g_gxState.blendFacSrc, GX_BL_SRCALPHA);
  EXPECT_EQ(g_gxState.blendFacDst, GX_BL_INVSRCALPHA);
}

TEST_F(GXFifoTest, BlendMode_None) {
  GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
  auto bytes = capture_fifo();

  reset_gx_state();
  // Pre-set to something else to prove the decode works
  g_gxState.blendMode = GX_BM_BLEND;
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.blendMode, GX_BM_NONE);
}

TEST_F(GXFifoTest, BlendMode_Subtract) {
  GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.blendMode, GX_BM_SUBTRACT);
}

TEST_F(GXFifoTest, BlendMode_Logic) {
  GXSetBlendMode(GX_BM_LOGIC, GX_BL_ONE, GX_BL_ZERO, GX_LO_XOR);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.blendMode, GX_BM_LOGIC);
  EXPECT_EQ(g_gxState.blendOp, GX_LO_XOR);
}

// --- GXSetColorUpdate / GXSetAlphaUpdate (BP 0x41 cmode0) ---

TEST_F(GXFifoTest, ColorUpdate_Disabled) {
  GXSetColorUpdate(GX_FALSE);
  auto bytes = capture_fifo();

  reset_gx_state();
  g_gxState.colorUpdate = true;
  decode_fifo(bytes);

  EXPECT_FALSE(g_gxState.colorUpdate);
}

TEST_F(GXFifoTest, AlphaUpdate_Disabled) {
  GXSetAlphaUpdate(false);
  auto bytes = capture_fifo();

  reset_gx_state();
  g_gxState.alphaUpdate = true;
  decode_fifo(bytes);

  EXPECT_FALSE(g_gxState.alphaUpdate);
}

// --- GXSetZMode (BP 0x40) ---

TEST_F(GXFifoTest, ZMode_LessNoUpdate) {
  GXSetZMode(true, GX_LESS, false);
  auto bytes = capture_fifo();

  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x61);
  EXPECT_EQ(bytes[1], 0x40);

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_TRUE(g_gxState.depthCompare);
  EXPECT_EQ(g_gxState.depthFunc, GX_LESS);
  EXPECT_FALSE(g_gxState.depthUpdate);
}

TEST_F(GXFifoTest, ZMode_AlwaysUpdate) {
  GXSetZMode(true, GX_ALWAYS, true);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_TRUE(g_gxState.depthCompare);
  EXPECT_EQ(g_gxState.depthFunc, GX_ALWAYS);
  EXPECT_TRUE(g_gxState.depthUpdate);
}

TEST_F(GXFifoTest, ZMode_Disabled) {
  GXSetZMode(false, GX_NEVER, false);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_FALSE(g_gxState.depthCompare);
  EXPECT_EQ(g_gxState.depthFunc, GX_NEVER);
  EXPECT_FALSE(g_gxState.depthUpdate);
}

// --- GXSetAlphaCompare (BP 0xF3) ---

TEST_F(GXFifoTest, AlphaCompare_GreaterThan128) {
  GXSetAlphaCompare(GX_GREATER, 128, GX_AOP_AND, GX_ALWAYS, 0);
  auto bytes = capture_fifo();

  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x61);
  EXPECT_EQ(bytes[1], 0xF3);

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.alphaCompare.comp0, GX_GREATER);
  EXPECT_EQ(g_gxState.alphaCompare.ref0, 128u);
  EXPECT_EQ(g_gxState.alphaCompare.op, GX_AOP_AND);
  EXPECT_EQ(g_gxState.alphaCompare.comp1, GX_ALWAYS);
  EXPECT_EQ(g_gxState.alphaCompare.ref1, 0u);
}

TEST_F(GXFifoTest, AlphaCompare_OrGequal) {
  GXSetAlphaCompare(GX_GEQUAL, 64, GX_AOP_OR, GX_LEQUAL, 200);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.alphaCompare.comp0, GX_GEQUAL);
  EXPECT_EQ(g_gxState.alphaCompare.ref0, 64u);
  EXPECT_EQ(g_gxState.alphaCompare.op, GX_AOP_OR);
  EXPECT_EQ(g_gxState.alphaCompare.comp1, GX_LEQUAL);
  EXPECT_EQ(g_gxState.alphaCompare.ref1, 200u);
}

// --- GXSetDstAlpha (BP 0x42) ---

TEST_F(GXFifoTest, DstAlpha_Enabled) {
  GXSetDstAlpha(true, 0x80);
  auto bytes = capture_fifo();

  reset_gx_state();
  g_gxState.dstAlpha = UINT32_MAX;
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.dstAlpha, 0x80u);
}

TEST_F(GXFifoTest, DstAlpha_Disabled) {
  GXSetDstAlpha(false, 0);
  auto bytes = capture_fifo();

  reset_gx_state();
  g_gxState.dstAlpha = 0x80;
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.dstAlpha, UINT32_MAX);
}

// ============================================================================
// TEV registers (direct FIFO writes)
// ============================================================================

// --- GXSetTevColorIn / GXSetTevAlphaIn ---

TEST_F(GXFifoTest, TevColorIn_Stage0) {
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
  auto bytes = capture_fifo();

  // BP opcode 0x61, register 0xC0 (stage 0 color)
  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x61);
  EXPECT_EQ(bytes[1], 0xC0);

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.colorPass.a, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.b, GX_CC_TEXC);
  EXPECT_EQ(s.colorPass.c, GX_CC_RASC);
  EXPECT_EQ(s.colorPass.d, GX_CC_ZERO);
}

TEST_F(GXFifoTest, TevAlphaIn_Stage0) {
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
  auto bytes = capture_fifo();

  // BP opcode 0x61, register 0xC1 (stage 0 alpha)
  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x61);
  EXPECT_EQ(bytes[1], 0xC1);

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.alphaPass.a, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.b, GX_CA_TEXA);
  EXPECT_EQ(s.alphaPass.c, GX_CA_RASA);
  EXPECT_EQ(s.alphaPass.d, GX_CA_ZERO);
}

TEST_F(GXFifoTest, TevAlphaIn_Stage5) {
  GXSetTevAlphaIn(GX_TEVSTAGE5, GX_CA_APREV, GX_CA_A0, GX_CA_KONST, GX_CA_ZERO);
  auto bytes = capture_fifo();

  // Stage 5 alpha register = 0xC1 + 5*2 = 0xCB
  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x61);
  EXPECT_EQ(bytes[1], 0xCB);

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[5];
  EXPECT_EQ(s.alphaPass.a, GX_CA_APREV);
  EXPECT_EQ(s.alphaPass.b, GX_CA_A0);
  EXPECT_EQ(s.alphaPass.c, GX_CA_KONST);
  EXPECT_EQ(s.alphaPass.d, GX_CA_ZERO);
}

TEST_F(GXFifoTest, TevColorIn_Stage7) {
  GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_C0, GX_CC_A0, GX_CC_KONST, GX_CC_CPREV);
  auto bytes = capture_fifo();

  // Stage 7 color register = 0xC0 + 7*2 = 0xCE
  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x61);
  EXPECT_EQ(bytes[1], 0xCE);

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[7];
  EXPECT_EQ(s.colorPass.a, GX_CC_C0);
  EXPECT_EQ(s.colorPass.b, GX_CC_A0);
  EXPECT_EQ(s.colorPass.c, GX_CC_KONST);
  EXPECT_EQ(s.colorPass.d, GX_CC_CPREV);
}

// --- GXSetTevOp (convenience wrapper over ColorIn/AlphaIn/ColorOp/AlphaOp) ---
// GXSetTevOp emits 4 BP writes: tevc (colorIn+colorOp) and teva (alphaIn+alphaOp).

TEST_F(GXFifoTest, TevOp_Modulate_Stage0) {
  GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  // Modulate: color = ZERO, TEXC, RASC, ZERO (stage 0 uses RASC/RASA)
  EXPECT_EQ(s.colorPass.a, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.b, GX_CC_TEXC);
  EXPECT_EQ(s.colorPass.c, GX_CC_RASC);
  EXPECT_EQ(s.colorPass.d, GX_CC_ZERO);
  // Modulate: alpha = ZERO, TEXA, RASA, ZERO
  EXPECT_EQ(s.alphaPass.a, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.b, GX_CA_TEXA);
  EXPECT_EQ(s.alphaPass.c, GX_CA_RASA);
  EXPECT_EQ(s.alphaPass.d, GX_CA_ZERO);
  // Op = ADD, bias = ZERO, scale = 1, clamp = true, outReg = TEVPREV
  EXPECT_EQ(s.colorOp.op, GX_TEV_ADD);
  EXPECT_EQ(s.colorOp.bias, GX_TB_ZERO);
  EXPECT_EQ(s.colorOp.scale, GX_CS_SCALE_1);
  EXPECT_TRUE(s.colorOp.clamp);
  EXPECT_EQ(s.colorOp.outReg, GX_TEVPREV);
  EXPECT_EQ(s.alphaOp.op, GX_TEV_ADD);
  EXPECT_EQ(s.alphaOp.bias, GX_TB_ZERO);
  EXPECT_EQ(s.alphaOp.scale, GX_CS_SCALE_1);
  EXPECT_TRUE(s.alphaOp.clamp);
  EXPECT_EQ(s.alphaOp.outReg, GX_TEVPREV);
}

TEST_F(GXFifoTest, TevOp_Modulate_Stage1) {
  // Non-stage-0 uses CPREV/APREV instead of RASC/RASA
  GXSetTevOp(GX_TEVSTAGE1, GX_MODULATE);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[1];
  EXPECT_EQ(s.colorPass.a, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.b, GX_CC_TEXC);
  EXPECT_EQ(s.colorPass.c, GX_CC_CPREV);
  EXPECT_EQ(s.colorPass.d, GX_CC_ZERO);
  EXPECT_EQ(s.alphaPass.a, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.b, GX_CA_TEXA);
  EXPECT_EQ(s.alphaPass.c, GX_CA_APREV);
  EXPECT_EQ(s.alphaPass.d, GX_CA_ZERO);
}

TEST_F(GXFifoTest, TevOp_Replace) {
  GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  // Replace: color = ZERO, ZERO, ZERO, TEXC
  EXPECT_EQ(s.colorPass.a, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.b, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.c, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.d, GX_CC_TEXC);
  // Replace: alpha = ZERO, ZERO, ZERO, TEXA
  EXPECT_EQ(s.alphaPass.a, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.b, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.c, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.d, GX_CA_TEXA);
}

TEST_F(GXFifoTest, TevOp_Decal) {
  GXSetTevOp(GX_TEVSTAGE0, GX_DECAL);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  // Decal: color = RASC, TEXC, TEXA, ZERO
  EXPECT_EQ(s.colorPass.a, GX_CC_RASC);
  EXPECT_EQ(s.colorPass.b, GX_CC_TEXC);
  EXPECT_EQ(s.colorPass.c, GX_CC_TEXA);
  EXPECT_EQ(s.colorPass.d, GX_CC_ZERO);
  // Decal: alpha = ZERO, ZERO, ZERO, RASA
  EXPECT_EQ(s.alphaPass.a, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.b, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.c, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.d, GX_CA_RASA);
}

TEST_F(GXFifoTest, TevOp_Blend) {
  GXSetTevOp(GX_TEVSTAGE0, GX_BLEND);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  // Blend: color = RASC, ONE, TEXC, ZERO
  EXPECT_EQ(s.colorPass.a, GX_CC_RASC);
  EXPECT_EQ(s.colorPass.b, GX_CC_ONE);
  EXPECT_EQ(s.colorPass.c, GX_CC_TEXC);
  EXPECT_EQ(s.colorPass.d, GX_CC_ZERO);
  // Blend: alpha = ZERO, TEXA, RASA, ZERO
  EXPECT_EQ(s.alphaPass.a, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.b, GX_CA_TEXA);
  EXPECT_EQ(s.alphaPass.c, GX_CA_RASA);
  EXPECT_EQ(s.alphaPass.d, GX_CA_ZERO);
}

TEST_F(GXFifoTest, TevOp_PassClr) {
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  // PassClr: color = ZERO, ZERO, ZERO, RASC
  EXPECT_EQ(s.colorPass.a, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.b, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.c, GX_CC_ZERO);
  EXPECT_EQ(s.colorPass.d, GX_CC_RASC);
  // PassClr: alpha = ZERO, ZERO, ZERO, RASA
  EXPECT_EQ(s.alphaPass.a, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.b, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.c, GX_CA_ZERO);
  EXPECT_EQ(s.alphaPass.d, GX_CA_RASA);
}

// --- GXSetTevColorOp / GXSetTevAlphaOp ---

TEST_F(GXFifoTest, TevColorOp_Sub_Scale2_Reg1) {
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ADDHALF, GX_CS_SCALE_2, GX_TRUE, GX_TEVREG1);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.colorOp.op, GX_TEV_SUB);
  EXPECT_EQ(s.colorOp.bias, GX_TB_ADDHALF);
  EXPECT_EQ(s.colorOp.scale, GX_CS_SCALE_2);
  EXPECT_TRUE(s.colorOp.clamp);
  EXPECT_EQ(s.colorOp.outReg, GX_TEVREG1);
}

TEST_F(GXFifoTest, TevAlphaOp_Add_NoClamp_Reg2) {
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_DIVIDE_2, GX_FALSE, GX_TEVREG2);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.alphaOp.op, GX_TEV_ADD);
  EXPECT_EQ(s.alphaOp.bias, GX_TB_SUBHALF);
  EXPECT_EQ(s.alphaOp.scale, GX_CS_DIVIDE_2);
  EXPECT_FALSE(s.alphaOp.clamp);
  EXPECT_EQ(s.alphaOp.outReg, GX_TEVREG2);
}

TEST_F(GXFifoTest, TevColorOp_CompareR8GT) {
  // Compare ops (op > 1) use a different encoding: bias=3, scale encodes compare mode
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.colorOp.op, GX_TEV_COMP_R8_GT);
  // Decoder normalizes compare mode: bias=ZERO, scale=SCALE_1
  EXPECT_EQ(s.colorOp.bias, GX_TB_ZERO);
  EXPECT_EQ(s.colorOp.scale, GX_CS_SCALE_1);
  EXPECT_EQ(s.colorOp.outReg, GX_TEVPREV);
}

TEST_F(GXFifoTest, TevColorOp_CompareGR16EQ) {
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_COMP_GR16_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.colorOp.op, GX_TEV_COMP_GR16_EQ);
  EXPECT_EQ(s.colorOp.outReg, GX_TEVREG0);
}

TEST_F(GXFifoTest, TevAlphaOp_CompareRGB8GT) {
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  // GX_TEV_COMP_RGB8_GT is the same enum value for alpha as GX_TEV_COMP_A8_GT
  EXPECT_EQ(s.alphaOp.op, GX_TEV_COMP_RGB8_GT);
  EXPECT_EQ(s.alphaOp.bias, GX_TB_ZERO);
  EXPECT_EQ(s.alphaOp.scale, GX_CS_SCALE_1);
}

TEST_F(GXFifoTest, TevColorOp_CompareBGR24GT) {
  GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_COMP_BGR24_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG1);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[2];
  EXPECT_EQ(s.colorOp.op, GX_TEV_COMP_BGR24_GT);
  EXPECT_EQ(s.colorOp.bias, GX_TB_ZERO);
  EXPECT_EQ(s.colorOp.scale, GX_CS_SCALE_1);
  EXPECT_EQ(s.colorOp.outReg, GX_TEVREG1);
}

TEST_F(GXFifoTest, TevColorOp_CompareRGB8EQ) {
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_COMP_RGB8_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.colorOp.op, GX_TEV_COMP_RGB8_EQ);
  EXPECT_EQ(s.colorOp.outReg, GX_TEVPREV);
}

TEST_F(GXFifoTest, TevAlphaOp_CompareA8EQ) {
  GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_COMP_RGB8_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG2);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[1];
  // For alpha, GX_TEV_COMP_RGB8_EQ maps to A8_EQ
  EXPECT_EQ(s.alphaOp.op, GX_TEV_COMP_RGB8_EQ);
  EXPECT_EQ(s.alphaOp.outReg, GX_TEVREG2);
}

// --- GXSetTevColorS10 ---

TEST_F(GXFifoTest, TevColorS10_Positive) {
  GXColorS10 col = {511, 256, 100, 0};
  GXSetTevColorS10(GX_TEVREG0, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  // S10 values are encoded as 11-bit signed and decoded to float/255
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][0], 511.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][1], 256.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][2], 100.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][3], 0.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, TevColorS10_Negative) {
  GXColorS10 col = {-128, -1, 0, 255};
  GXSetTevColorS10(GX_TEVPREV, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][0], -128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][1], -1.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][2], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][3], 255.f / 255.f, 1.f / 255.f);
}

// --- GXSetTevKColorSel / GXSetTevKAlphaSel ---

TEST_F(GXFifoTest, TevKColorSel_Stage0_K0) {
  GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevStages[0].kcSel, GX_TEV_KCSEL_K0);
}

TEST_F(GXFifoTest, TevKColorSel_Stage1_K2_R) {
  GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K2_R);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevStages[1].kcSel, GX_TEV_KCSEL_K2_R);
}

TEST_F(GXFifoTest, TevKAlphaSel_Stage0_K1_A) {
  GXSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K1_A);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevStages[0].kaSel, GX_TEV_KASEL_K1_A);
}

TEST_F(GXFifoTest, TevKAlphaSel_Stage3_K3_B) {
  GXSetTevKAlphaSel(GX_TEVSTAGE3, GX_TEV_KASEL_K3_B);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevStages[3].kaSel, GX_TEV_KASEL_K3_B);
}

TEST_F(GXFifoTest, TevKColorSel_DoesNotCorruptSwapTable) {
  // Regression: tevKsel shadow registers share bits with swap table entries.
  // Setting K color selection must not zero out the swap table bits.
  GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  // Swap table 0 should remain identity (initialized in GXInit)
  EXPECT_EQ(g_gxState.tevSwapTable[0].red, GX_CH_RED);
  EXPECT_EQ(g_gxState.tevSwapTable[0].green, GX_CH_GREEN);
  EXPECT_EQ(g_gxState.tevSwapTable[0].blue, GX_CH_BLUE);
  EXPECT_EQ(g_gxState.tevSwapTable[0].alpha, GX_CH_ALPHA);
  // K color selection should still be set
  EXPECT_EQ(g_gxState.tevStages[0].kcSel, GX_TEV_KCSEL_K0);
}

// --- GXSetTevSwapMode ---

TEST_F(GXFifoTest, TevSwapMode_Stage0) {
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP2);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevStages[0].tevSwapRas, GX_TEV_SWAP1);
  EXPECT_EQ(g_gxState.tevStages[0].tevSwapTex, GX_TEV_SWAP2);
}

TEST_F(GXFifoTest, TevSwapMode_Stage3) {
  GXSetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP3, GX_TEV_SWAP0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevStages[3].tevSwapRas, GX_TEV_SWAP3);
  EXPECT_EQ(g_gxState.tevStages[3].tevSwapTex, GX_TEV_SWAP0);
}

// --- GXSetTevSwapModeTable ---

TEST_F(GXFifoTest, TevSwapModeTable_Swap1_AllRed) {
  GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP1].red, GX_CH_RED);
  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP1].green, GX_CH_RED);
  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP1].blue, GX_CH_RED);
  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP1].alpha, GX_CH_ALPHA);
}

TEST_F(GXFifoTest, TevSwapModeTable_Swap2_Swizzle) {
  GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP2].red, GX_CH_BLUE);
  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP2].green, GX_CH_GREEN);
  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP2].blue, GX_CH_RED);
  EXPECT_EQ(g_gxState.tevSwapTable[GX_TEV_SWAP2].alpha, GX_CH_ALPHA);
}

// --- GXSetTevOrder (BP 0x28-0x2F) ---

TEST_F(GXFifoTest, TevOrder_Stage0) {
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.texMapId, GX_TEXMAP0);
  EXPECT_EQ(s.texCoordId, GX_TEXCOORD0);
  EXPECT_EQ(s.channelId, GX_COLOR0A0);
}

TEST_F(GXFifoTest, TevOrder_Stage1_OddStage) {
  // Odd stages use different bit positions within the tref register
  GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD2, GX_TEXMAP3, GX_COLOR1A1);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[1];
  EXPECT_EQ(s.texMapId, GX_TEXMAP3);
  EXPECT_EQ(s.texCoordId, GX_TEXCOORD2);
  EXPECT_EQ(s.channelId, GX_COLOR1A1);
}

TEST_F(GXFifoTest, TevOrder_Stage0_TexNull) {
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& s = g_gxState.tevStages[0];
  EXPECT_EQ(s.texMapId, GX_TEXMAP_NULL);
  EXPECT_EQ(s.channelId, GX_COLOR0A0);
}

// --- GXSetTevKColor (BP 0xE0-0xE7, K color flag) ---

TEST_F(GXFifoTest, TevKColor_K0) {
  GXColor kc = {255, 128, 64, 32};
  GXSetTevKColor(GX_KCOLOR0, kc);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  // K colors are stored as float (0-1 range), 8-bit precision
  EXPECT_NEAR(g_gxState.kcolors[0][0], 255.f / 255.f, 1.f / 255.f); // R
  EXPECT_NEAR(g_gxState.kcolors[0][1], 128.f / 255.f, 1.f / 255.f); // G
  EXPECT_NEAR(g_gxState.kcolors[0][2], 64.f / 255.f, 1.f / 255.f);  // B
  EXPECT_NEAR(g_gxState.kcolors[0][3], 32.f / 255.f, 1.f / 255.f);  // A
}

TEST_F(GXFifoTest, TevKColor_K1) {
  GXColor kc = {0, 255, 0, 128};
  GXSetTevKColor(GX_KCOLOR1, kc);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.kcolors[1][0], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[1][1], 255.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[1][2], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[1][3], 128.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, TevKColor_K2) {
  GXColor kc = {10, 20, 30, 40};
  GXSetTevKColor(GX_KCOLOR2, kc);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.kcolors[2][0], 10.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[2][1], 20.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[2][2], 30.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[2][3], 40.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, TevKColor_K3) {
  GXColor kc = {200, 150, 100, 50};
  GXSetTevKColor(GX_KCOLOR3, kc);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.kcolors[3][0], 200.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[3][1], 150.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[3][2], 100.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.kcolors[3][3], 50.f / 255.f, 1.f / 255.f);
}

// --- GXSetTevColor (BP 0xE0-0xE7, TEV color register) ---
// Note: side channel stores as float, FIFO encodes as 11-bit signed.
// Decoded value will have reduced precision.

TEST_F(GXFifoTest, TevColor_Reg0) {
  GXColor col = {200, 100, 50, 255};
  GXSetTevColor(GX_TEVREG0, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  // 11-bit signed encoding, so values should round-trip within 8-bit range
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][0], 200.f / 255.f, 1.f / 255.f); // R
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][1], 100.f / 255.f, 1.f / 255.f); // G
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][2], 50.f / 255.f, 1.f / 255.f);  // B
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG0][3], 255.f / 255.f, 1.f / 255.f); // A
}

TEST_F(GXFifoTest, TevColor_Prev) {
  GXColor col = {128, 64, 32, 16};
  GXSetTevColor(GX_TEVPREV, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][0], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][1], 64.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][2], 32.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVPREV][3], 16.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, TevColor_Reg1) {
  GXColor col = {0, 128, 255, 192};
  GXSetTevColor(GX_TEVREG1, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][0], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][1], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][2], 255.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][3], 192.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, TevColor_Reg2) {
  GXColor col = {1, 2, 3, 4};
  GXSetTevColor(GX_TEVREG2, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][0], 1.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][1], 2.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][2], 3.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][3], 4.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, TevColorS10_Reg1) {
  GXColorS10 col = {300, -50, 0, 255};
  GXSetTevColorS10(GX_TEVREG1, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][0], 300.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][1], -50.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][2], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG1][3], 255.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, TevColorS10_Reg2) {
  GXColorS10 col = {-1024, 1023, 128, -256};
  GXSetTevColorS10(GX_TEVREG2, col);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][0], -1024.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][1], 1023.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][2], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(g_gxState.colorRegs[GX_TEVREG2][3], -256.f / 255.f, 1.f / 255.f);
}

// ============================================================================
// CP registers (require __GXSetDirtyState() flush)
// ============================================================================

// --- GXSetVtxDesc / GXClearVtxDesc ---

TEST_F(GXFifoTest, VtxDesc_PosAndNrm_Direct) {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_POS], GX_DIRECT);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_NRM], GX_DIRECT);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_CLR0], GX_NONE);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_TEX0], GX_NONE);
}

TEST_F(GXFifoTest, VtxDesc_Indexed) {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
  GXSetVtxDesc(GX_VA_NRM, GX_INDEX16);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_POS], GX_INDEX16);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_NRM], GX_INDEX16);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_CLR0], GX_DIRECT);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_TEX0], GX_INDEX8);
}

TEST_F(GXFifoTest, VtxDesc_MtxIdx) {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_PNMTXIDX, GX_DIRECT);
  GXSetVtxDesc(GX_VA_TEX0MTXIDX, GX_DIRECT);
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_PNMTXIDX], GX_DIRECT);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_TEX0MTXIDX], GX_DIRECT);
  EXPECT_EQ(g_gxState.vtxDesc[GX_VA_POS], GX_DIRECT);
}

// --- GXSetVtxAttrFmt ---

TEST_F(GXFifoTest, VtxAttrFmt_PosF32) {
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  auto& vf = g_gxState.vtxFmts[GX_VTXFMT0];
  EXPECT_EQ(vf.attrs[GX_VA_POS].cnt, GX_POS_XYZ);
  EXPECT_EQ(vf.attrs[GX_VA_POS].type, GX_F32);
  EXPECT_EQ(vf.attrs[GX_VA_POS].frac, 0);
}

TEST_F(GXFifoTest, VtxAttrFmt_NrmS16) {
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_S16, 0);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  auto& vf = g_gxState.vtxFmts[GX_VTXFMT0];
  EXPECT_EQ(vf.attrs[GX_VA_NRM].cnt, GX_NRM_XYZ);
  EXPECT_EQ(vf.attrs[GX_VA_NRM].type, GX_S16);
}

TEST_F(GXFifoTest, VtxAttrFmt_Tex0_S16_Frac8) {
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 8);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  auto& vf = g_gxState.vtxFmts[GX_VTXFMT0];
  EXPECT_EQ(vf.attrs[GX_VA_TEX0].cnt, GX_TEX_ST);
  EXPECT_EQ(vf.attrs[GX_VA_TEX0].type, GX_S16);
  EXPECT_EQ(vf.attrs[GX_VA_TEX0].frac, 8);
}

TEST_F(GXFifoTest, VtxAttrFmt_Clr0_RGBA8) {
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  auto& vf = g_gxState.vtxFmts[GX_VTXFMT0];
  EXPECT_EQ(vf.attrs[GX_VA_CLR0].cnt, GX_CLR_RGBA);
  EXPECT_EQ(vf.attrs[GX_VA_CLR0].type, GX_RGBA8);
}

TEST_F(GXFifoTest, VtxAttrFmt_MultipleTexCoords) {
  GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX1, GX_TEX_ST, GX_U16, 15);
  GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX2, GX_TEX_ST, GX_S16, 8);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  auto& vf = g_gxState.vtxFmts[GX_VTXFMT1];
  EXPECT_EQ(vf.attrs[GX_VA_TEX0].type, GX_F32);
  EXPECT_EQ(vf.attrs[GX_VA_TEX1].type, GX_U16);
  EXPECT_EQ(vf.attrs[GX_VA_TEX1].frac, 15);
  EXPECT_EQ(vf.attrs[GX_VA_TEX2].type, GX_S16);
  EXPECT_EQ(vf.attrs[GX_VA_TEX2].frac, 8);
}

// ============================================================================
// BP genMode (requires __GXSetDirtyState() flush)
// ============================================================================

// --- GXSetCullMode ---

TEST_F(GXFifoTest, CullMode_Back) {
  GXSetCullMode(GX_CULL_BACK);
  auto bytes = flush_and_capture();

  reset_gx_state();
  g_gxState.cullMode = GX_CULL_NONE;
  decode_fifo(bytes);

  // The encoder swaps front/back for hardware, and decoder swaps back
  EXPECT_EQ(g_gxState.cullMode, GX_CULL_BACK);
}

TEST_F(GXFifoTest, CullMode_Front) {
  GXSetCullMode(GX_CULL_FRONT);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.cullMode, GX_CULL_FRONT);
}

TEST_F(GXFifoTest, CullMode_None) {
  GXSetCullMode(GX_CULL_NONE);
  auto bytes = flush_and_capture();

  reset_gx_state();
  g_gxState.cullMode = GX_CULL_BACK;
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.cullMode, GX_CULL_NONE);
}

// --- GXSetNumTevStages / GXSetNumTexGens / GXSetNumChans ---

TEST_F(GXFifoTest, NumTevStages) {
  GXSetNumTevStages(4);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.numTevStages, 4u);
}

TEST_F(GXFifoTest, NumTexGens) {
  GXSetNumTexGens(3);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.numTexGens, 3u);
}

TEST_F(GXFifoTest, NumChans) {
  GXSetNumChans(2);
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.numChans, 2u);
}

// ============================================================================
// XF registers (direct FIFO writes)
// ============================================================================

// --- GXLoadPosMtxImm (XF 0x000-0x077) ---

TEST_F(GXFifoTest, LoadPosMtxImm_Identity) {
  // 3x4 identity matrix
  aurora::Mat3x4<float> mtx{};
  mtx.m0[0] = 1.0f;
  mtx.m1[1] = 1.0f;
  mtx.m2[2] = 1.0f;

  GXLoadPosMtxImm(&mtx, GX_PNMTX0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& decoded = g_gxState.pnMtx[0].pos;
  EXPECT_FLOAT_EQ(decoded.m0[0], 1.0f);
  EXPECT_FLOAT_EQ(decoded.m0[1], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m0[2], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m0[3], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m1[0], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m1[1], 1.0f);
  EXPECT_FLOAT_EQ(decoded.m1[2], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m1[3], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m2[0], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m2[1], 0.0f);
  EXPECT_FLOAT_EQ(decoded.m2[2], 1.0f);
  EXPECT_FLOAT_EQ(decoded.m2[3], 0.0f);
}

TEST_F(GXFifoTest, LoadPosMtxImm_Translation) {
  aurora::Mat3x4<float> mtx{};
  mtx.m0[0] = 1.0f;
  mtx.m1[1] = 1.0f;
  mtx.m2[2] = 1.0f;
  mtx.m0[3] = 10.0f;
  mtx.m1[3] = 20.0f;
  mtx.m2[3] = 30.0f;

  GXLoadPosMtxImm(&mtx, GX_PNMTX3);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  // GX_PNMTX3 = 9, XF addr = 9*4 = 36, decoder mtxIdx = 36/4 = 9
  auto& decoded = g_gxState.pnMtx[GX_PNMTX3].pos;
  EXPECT_FLOAT_EQ(decoded.m0[0], 1.0f);
  EXPECT_FLOAT_EQ(decoded.m0[3], 10.0f);
  EXPECT_FLOAT_EQ(decoded.m1[3], 20.0f);
  EXPECT_FLOAT_EQ(decoded.m2[3], 30.0f);
}

// --- GXSetProjection (XF 0x1020-0x1026) ---

TEST_F(GXFifoTest, Projection_Perspective) {
  aurora::Mat4x4<float> proj{};
  proj.m0[0] = 1.5f; // near / (right - left) * 2
  proj.m0[2] = 0.1f;
  proj.m1[1] = 2.0f; // near / (top - bottom) * 2
  proj.m1[2] = 0.2f;
  proj.m2[2] = -1.002f;
  proj.m2[3] = -0.2002f;
  proj.m3[2] = -1.0f;

  GXSetProjection(&proj, GX_PERSPECTIVE);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.projType, GX_PERSPECTIVE);
  EXPECT_FLOAT_EQ(g_gxState.proj.m0[0], 1.5f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m0[2], 0.1f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m1[1], 2.0f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m1[2], 0.2f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m2[2], -1.002f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m2[3], -0.2002f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m3[2], -1.0f);
}

TEST_F(GXFifoTest, Projection_Orthographic) {
  aurora::Mat4x4<float> proj{};
  proj.m0[0] = 2.0f / 640.0f;
  proj.m0[3] = -1.0f;
  proj.m1[1] = 2.0f / 480.0f;
  proj.m1[3] = -1.0f;
  proj.m2[2] = -1.0f / 10000.0f;
  proj.m2[3] = 0.0f;
  proj.m3[3] = 1.0f;

  GXSetProjection(&proj, GX_ORTHOGRAPHIC);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.projType, GX_ORTHOGRAPHIC);
  EXPECT_FLOAT_EQ(g_gxState.proj.m0[0], 2.0f / 640.0f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m0[3], -1.0f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m1[1], 2.0f / 480.0f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m1[3], -1.0f);
  EXPECT_FLOAT_EQ(g_gxState.proj.m3[3], 1.0f);
}

// --- GXLoadLightObjImm (XF 0x600-0x67F) ---

TEST_F(GXFifoTest, LoadLightObjImm_Light0_BasicColor) {
  GXLightObj lightObj;
  GXInitLightPos(&lightObj, 100.0f, 200.0f, 300.0f);
  GXInitLightDir(&lightObj, 0.0f, -1.0f, 0.0f);
  GXInitLightColor(&lightObj, {255, 128, 64, 255});
  GXInitLightAttn(&lightObj, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

  GXLoadLightObjImm(&lightObj, GX_LIGHT0);
  auto bytes = capture_fifo();

  // XF bulk write: opcode 0x10
  ASSERT_GE(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], 0x10);

  reset_gx_state();
  decode_fifo(bytes);

  auto& light = g_gxState.lights[0];
  // Color
  EXPECT_NEAR(light.color[0], 255.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[1], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[2], 64.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[3], 255.f / 255.f, 1.f / 255.f);
  // Position
  EXPECT_FLOAT_EQ(light.pos[0], 100.0f);
  EXPECT_FLOAT_EQ(light.pos[1], 200.0f);
  EXPECT_FLOAT_EQ(light.pos[2], 300.0f);
  // Direction (GXInitLightDir negates)
  EXPECT_FLOAT_EQ(light.dir[0], 0.0f);
  EXPECT_FLOAT_EQ(light.dir[1], 1.0f);
  EXPECT_FLOAT_EQ(light.dir[2], 0.0f);
  // Cosine attenuation
  EXPECT_FLOAT_EQ(light.cosAtt[0], 1.0f);
  EXPECT_FLOAT_EQ(light.cosAtt[1], 0.0f);
  EXPECT_FLOAT_EQ(light.cosAtt[2], 0.0f);
  // Distance attenuation
  EXPECT_FLOAT_EQ(light.distAtt[0], 1.0f);
  EXPECT_FLOAT_EQ(light.distAtt[1], 0.0f);
  EXPECT_FLOAT_EQ(light.distAtt[2], 0.0f);
}

TEST_F(GXFifoTest, LoadLightObjImm_Light3_Attenuation) {
  GXLightObj lightObj;
  GXInitLightPos(&lightObj, -50.0f, 0.0f, 75.0f);
  GXInitLightDir(&lightObj, 1.0f, 0.0f, 0.0f);
  GXInitLightColor(&lightObj, {0, 255, 0, 128});
  GXInitLightAttn(&lightObj, 0.5f, 0.3f, 0.2f, 1.0f, 0.01f, 0.001f);

  GXLoadLightObjImm(&lightObj, GX_LIGHT3);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& light = g_gxState.lights[3];
  EXPECT_NEAR(light.color[0], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[1], 255.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[2], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[3], 128.f / 255.f, 1.f / 255.f);
  EXPECT_FLOAT_EQ(light.pos[0], -50.0f);
  EXPECT_FLOAT_EQ(light.pos[1], 0.0f);
  EXPECT_FLOAT_EQ(light.pos[2], 75.0f);
  EXPECT_FLOAT_EQ(light.dir[0], -1.0f);
  EXPECT_FLOAT_EQ(light.dir[1], 0.0f);
  EXPECT_FLOAT_EQ(light.dir[2], 0.0f);
  EXPECT_FLOAT_EQ(light.cosAtt[0], 0.5f);
  EXPECT_FLOAT_EQ(light.cosAtt[1], 0.3f);
  EXPECT_FLOAT_EQ(light.cosAtt[2], 0.2f);
  EXPECT_FLOAT_EQ(light.distAtt[0], 1.0f);
  EXPECT_FLOAT_EQ(light.distAtt[1], 0.01f);
  EXPECT_FLOAT_EQ(light.distAtt[2], 0.001f);
}

TEST_F(GXFifoTest, LoadLightObjImm_Light7_LastLight) {
  GXLightObj lightObj;
  GXInitLightPos(&lightObj, 0.0f, 1000.0f, 0.0f);
  GXInitLightDir(&lightObj, 0.0f, 0.0f, -1.0f);
  GXInitLightColor(&lightObj, {128, 128, 128, 255});
  GXInitLightAttn(&lightObj, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

  GXLoadLightObjImm(&lightObj, GX_LIGHT7);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& light = g_gxState.lights[7];
  EXPECT_NEAR(light.color[0], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[1], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[2], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(light.color[3], 255.f / 255.f, 1.f / 255.f);
  EXPECT_FLOAT_EQ(light.pos[0], 0.0f);
  EXPECT_FLOAT_EQ(light.pos[1], 1000.0f);
  EXPECT_FLOAT_EQ(light.pos[2], 0.0f);
  EXPECT_FLOAT_EQ(light.dir[0], 0.0f);
  EXPECT_FLOAT_EQ(light.dir[1], 0.0f);
  EXPECT_FLOAT_EQ(light.dir[2], 1.0f);
}

TEST_F(GXFifoTest, LoadLightObjImm_SpotLight) {
  GXLightObj lightObj;
  GXInitLightPos(&lightObj, 10.0f, 20.0f, 30.0f);
  GXInitLightDir(&lightObj, 0.0f, -1.0f, 0.0f);
  GXInitLightSpot(&lightObj, 45.0f, GX_SP_COS);
  GXInitLightDistAttn(&lightObj, 100.0f, 0.5f, GX_DA_MEDIUM);
  GXInitLightColor(&lightObj, {255, 255, 255, 255});

  GXLoadLightObjImm(&lightObj, GX_LIGHT1);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& light = g_gxState.lights[1];
  EXPECT_NEAR(light.color[0], 1.0f, 1.f / 255.f);
  EXPECT_NEAR(light.color[1], 1.0f, 1.f / 255.f);
  EXPECT_NEAR(light.color[2], 1.0f, 1.f / 255.f);
  EXPECT_FLOAT_EQ(light.pos[0], 10.0f);
  EXPECT_FLOAT_EQ(light.pos[1], 20.0f);
  EXPECT_FLOAT_EQ(light.pos[2], 30.0f);
  // GX_SP_COS with cutoff=45: cr = cos(45 * pi / 180)
  // a0 = -cr/(1-cr), a1 = 1/(1-cr), a2 = 0
  float cr = std::cos(45.0f * M_PIF / 180.0f);
  EXPECT_FLOAT_EQ(light.cosAtt[0], -cr / (1.0f - cr));
  EXPECT_FLOAT_EQ(light.cosAtt[1], 1.0f / (1.0f - cr));
  EXPECT_FLOAT_EQ(light.cosAtt[2], 0.0f);
  // GX_DA_MEDIUM with refDist=100, refBright=0.5:
  // k0 = 1, k1 = 0.5*(1-b)/(b*d), k2 = 0.5*(1-b)/(b*d*d)
  EXPECT_FLOAT_EQ(light.distAtt[0], 1.0f);
  EXPECT_FLOAT_EQ(light.distAtt[1], 0.5f * 0.5f / (0.5f * 100.0f));
  EXPECT_FLOAT_EQ(light.distAtt[2], 0.5f * 0.5f / (0.5f * 100.0f * 100.0f));
}

// --- GXSetChanCtrl (XF 0x100E-0x1011) ---

TEST_F(GXFifoTest, ChanCtrl_Color0_LightingEnabled) {
  GXSetChanCtrl(GX_COLOR0, true, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT0 | GX_LIGHT1, GX_DF_CLAMP, GX_AF_SPOT);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& cfg = g_gxState.colorChannelConfig[GX_COLOR0];
  EXPECT_TRUE(cfg.lightingEnabled);
  EXPECT_EQ(cfg.matSrc, GX_SRC_VTX);
  EXPECT_EQ(cfg.ambSrc, GX_SRC_REG);
  EXPECT_EQ(cfg.diffFn, GX_DF_CLAMP);
  EXPECT_EQ(cfg.attnFn, GX_AF_SPOT);

  // Light mask should be 0x03 (lights 0 and 1)
  auto& state = g_gxState.colorChannelState[GX_COLOR0];
  EXPECT_TRUE(state.lightMask[0]);
  EXPECT_TRUE(state.lightMask[1]);
  EXPECT_FALSE(state.lightMask[2]);
}

TEST_F(GXFifoTest, ChanCtrl_Alpha0_NoLighting) {
  GXSetChanCtrl(GX_ALPHA0, false, GX_SRC_VTX, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& cfg = g_gxState.colorChannelConfig[GX_ALPHA0];
  EXPECT_FALSE(cfg.lightingEnabled);
  EXPECT_EQ(cfg.matSrc, GX_SRC_REG);
  EXPECT_EQ(cfg.ambSrc, GX_SRC_VTX);
  EXPECT_EQ(cfg.attnFn, GX_AF_NONE);
}

TEST_F(GXFifoTest, ChanCtrl_Color1_SpecularLighting) {
  GXSetChanCtrl(GX_COLOR1, true, GX_SRC_REG, GX_SRC_REG, GX_LIGHT2 | GX_LIGHT5, GX_DF_SIGN, GX_AF_SPEC);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& cfg = g_gxState.colorChannelConfig[GX_COLOR1];
  EXPECT_TRUE(cfg.lightingEnabled);
  EXPECT_EQ(cfg.matSrc, GX_SRC_REG);
  EXPECT_EQ(cfg.ambSrc, GX_SRC_REG);
  EXPECT_EQ(cfg.diffFn, GX_DF_SIGN);
  EXPECT_EQ(cfg.attnFn, GX_AF_SPEC);

  auto& state = g_gxState.colorChannelState[GX_COLOR1];
  EXPECT_FALSE(state.lightMask[0]);
  EXPECT_FALSE(state.lightMask[1]);
  EXPECT_TRUE(state.lightMask[2]);
  EXPECT_FALSE(state.lightMask[3]);
  EXPECT_FALSE(state.lightMask[4]);
  EXPECT_TRUE(state.lightMask[5]);
}

TEST_F(GXFifoTest, ChanCtrl_Color0A0_Compound) {
  // GX_COLOR0A0 should set both GX_COLOR0 and GX_ALPHA0
  GXSetChanCtrl(GX_COLOR0A0, true, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT0, GX_DF_CLAMP, GX_AF_SPOT);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  // Both COLOR0 and ALPHA0 should be configured identically
  auto& cfgC = g_gxState.colorChannelConfig[GX_COLOR0];
  EXPECT_TRUE(cfgC.lightingEnabled);
  EXPECT_EQ(cfgC.matSrc, GX_SRC_VTX);
  EXPECT_EQ(cfgC.ambSrc, GX_SRC_REG);
  EXPECT_EQ(cfgC.diffFn, GX_DF_CLAMP);
  EXPECT_EQ(cfgC.attnFn, GX_AF_SPOT);

  auto& cfgA = g_gxState.colorChannelConfig[GX_ALPHA0];
  EXPECT_TRUE(cfgA.lightingEnabled);
  EXPECT_EQ(cfgA.matSrc, GX_SRC_VTX);
  EXPECT_EQ(cfgA.ambSrc, GX_SRC_REG);
  EXPECT_EQ(cfgA.attnFn, GX_AF_SPOT);

  EXPECT_TRUE(g_gxState.colorChannelState[GX_COLOR0].lightMask[0]);
  EXPECT_TRUE(g_gxState.colorChannelState[GX_ALPHA0].lightMask[0]);
}

TEST_F(GXFifoTest, ChanCtrl_Color1A1_Compound) {
  GXSetChanCtrl(GX_COLOR1A1, false, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& cfgC = g_gxState.colorChannelConfig[GX_COLOR1];
  EXPECT_FALSE(cfgC.lightingEnabled);
  EXPECT_EQ(cfgC.ambSrc, GX_SRC_VTX);
  EXPECT_EQ(cfgC.matSrc, GX_SRC_VTX);

  auto& cfgA = g_gxState.colorChannelConfig[GX_ALPHA1];
  EXPECT_FALSE(cfgA.lightingEnabled);
  EXPECT_EQ(cfgA.ambSrc, GX_SRC_VTX);
  EXPECT_EQ(cfgA.matSrc, GX_SRC_VTX);
}

// --- GXSetTexCoordGen2 (XF 0x1040-0x105F) ---

TEST_F(GXFifoTest, TexCoordGen_Mtx2x4_Tex0) {
  GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& tcg = g_gxState.tcgs[GX_TEXCOORD0];
  EXPECT_EQ(tcg.type, GX_TG_MTX2x4);
  EXPECT_EQ(tcg.src, GX_TG_TEX0);
}

TEST_F(GXFifoTest, TexCoordGen_Mtx3x4_Nrm) {
  GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX3x4, GX_TG_NRM, GX_TEXMTX0, GX_TRUE, GX_PTTEXMTX0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& tcg = g_gxState.tcgs[GX_TEXCOORD1];
  EXPECT_EQ(tcg.type, GX_TG_MTX3x4);
  EXPECT_EQ(tcg.src, GX_TG_NRM);
  EXPECT_TRUE(tcg.normalize);
  EXPECT_EQ(tcg.postMtx, GX_PTTEXMTX0);
}

TEST_F(GXFifoTest, TexCoordGen_SRTG_Color0) {
  GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_SRTG, GX_TG_COLOR0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& tcg = g_gxState.tcgs[GX_TEXCOORD0];
  EXPECT_EQ(tcg.type, GX_TG_SRTG);
}

// --- GXSetChanAmbColor / GXSetChanMatColor (XF 0x100A-0x100D) ---

TEST_F(GXFifoTest, ChanAmbColor_Color0) {
  GXColor amb = {64, 128, 192, 255};
  GXSetChanAmbColor(GX_COLOR0, amb);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& state = g_gxState.colorChannelState[GX_COLOR0];
  EXPECT_NEAR(state.ambColor[0], 64.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.ambColor[1], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.ambColor[2], 192.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.ambColor[3], 255.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, ChanMatColor_Color0) {
  GXColor mat = {255, 0, 128, 64};
  GXSetChanMatColor(GX_COLOR0, mat);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& state = g_gxState.colorChannelState[GX_COLOR0];
  EXPECT_NEAR(state.matColor[0], 255.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.matColor[1], 0.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.matColor[2], 128.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.matColor[3], 64.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, ChanAmbColor_Color1) {
  GXColor amb = {10, 20, 30, 40};
  GXSetChanAmbColor(GX_COLOR1, amb);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& state = g_gxState.colorChannelState[GX_COLOR1];
  EXPECT_NEAR(state.ambColor[0], 10.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.ambColor[1], 20.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.ambColor[2], 30.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.ambColor[3], 40.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, ChanMatColor_Color1) {
  GXColor mat = {100, 150, 200, 250};
  GXSetChanMatColor(GX_COLOR1, mat);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& state = g_gxState.colorChannelState[GX_COLOR1];
  EXPECT_NEAR(state.matColor[0], 100.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.matColor[1], 150.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.matColor[2], 200.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(state.matColor[3], 250.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, ChanAmbColor_Color0A0_Compound) {
  // GX_COLOR0A0 should write to both COLOR0 and ALPHA0 XF registers
  GXColor amb = {80, 160, 240, 128};
  GXSetChanAmbColor(GX_COLOR0A0, amb);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& stateC = g_gxState.colorChannelState[GX_COLOR0];
  EXPECT_NEAR(stateC.ambColor[0], 80.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateC.ambColor[1], 160.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateC.ambColor[2], 240.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateC.ambColor[3], 128.f / 255.f, 1.f / 255.f);
  // ALPHA0 shares the same XF register as COLOR0, so should match
  auto& stateA = g_gxState.colorChannelState[GX_ALPHA0];
  EXPECT_NEAR(stateA.ambColor[0], 80.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateA.ambColor[3], 128.f / 255.f, 1.f / 255.f);
}

TEST_F(GXFifoTest, ChanMatColor_Color1A1_Compound) {
  GXColor mat = {32, 64, 96, 128};
  GXSetChanMatColor(GX_COLOR1A1, mat);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  auto& stateC = g_gxState.colorChannelState[GX_COLOR1];
  EXPECT_NEAR(stateC.matColor[0], 32.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateC.matColor[1], 64.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateC.matColor[2], 96.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateC.matColor[3], 128.f / 255.f, 1.f / 255.f);

  auto& stateA = g_gxState.colorChannelState[GX_ALPHA1];
  EXPECT_NEAR(stateA.matColor[0], 32.f / 255.f, 1.f / 255.f);
  EXPECT_NEAR(stateA.matColor[3], 128.f / 255.f, 1.f / 255.f);
}

// ============================================================================
// Composite tests (multiple state changes in a single FIFO stream)
// ============================================================================

TEST_F(GXFifoTest, Composite_BlendAndZMode) {
  GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
  GXSetZMode(true, GX_LEQUAL, true);
  GXSetAlphaCompare(GX_GREATER, 128, GX_AOP_AND, GX_ALWAYS, 0);
  auto bytes = capture_fifo();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.blendMode, GX_BM_BLEND);
  EXPECT_EQ(g_gxState.blendFacSrc, GX_BL_SRCALPHA);
  EXPECT_EQ(g_gxState.blendFacDst, GX_BL_INVSRCALPHA);

  EXPECT_TRUE(g_gxState.depthCompare);
  EXPECT_EQ(g_gxState.depthFunc, GX_LEQUAL);
  EXPECT_TRUE(g_gxState.depthUpdate);

  EXPECT_EQ(g_gxState.alphaCompare.comp0, GX_GREATER);
  EXPECT_EQ(g_gxState.alphaCompare.ref0, 128u);
}

TEST_F(GXFifoTest, Composite_TevSetup) {
  // Set up a simple 1-stage TEV that passes through texture color
  GXSetNumTevStages(1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);

  // TEV order writes to dirty state, so flush before capture
  auto bytes = flush_and_capture();

  reset_gx_state();
  decode_fifo(bytes);

  EXPECT_EQ(g_gxState.numTevStages, 1u);
  EXPECT_EQ(g_gxState.tevStages[0].texMapId, GX_TEXMAP0);
  EXPECT_EQ(g_gxState.tevStages[0].texCoordId, GX_TEXCOORD0);
  EXPECT_EQ(g_gxState.tevStages[0].channelId, GX_COLOR0A0);
  EXPECT_EQ(g_gxState.tevStages[0].colorPass.d, GX_CC_TEXC);
  EXPECT_EQ(g_gxState.tevStages[0].alphaPass.d, GX_CA_TEXA);
}

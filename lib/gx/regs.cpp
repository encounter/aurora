#include "regs.hpp"

#include "gx_fmt.hpp"
#include "texture.hpp"

#include <bit>
#include <cmath>

namespace aurora::gx::fifo {
namespace {
constexpr Module Log{"aurora::gx::fifo"};

struct RegHandler {
  using Fn = void (*)(u8 reg, u32 value) noexcept;
  Fn decode = nullptr; // if null: no decoded state, shadow only
  u8 dirty = 0;
  bool alwaysHandle = false; // handler has side effects (e.g. EFB copy trigger, stub logging)
};

Vec4<float> unpack_color(u32 packed) noexcept {
  return {
      static_cast<float>(packed >> 24 & 0xFF) / 255.f,
      static_cast<float>(packed >> 16 & 0xFF) / 255.f,
      static_cast<float>(packed >> 8 & 0xFF) / 255.f,
      static_cast<float>(packed & 0xFF) / 255.f,
  };
}

constexpr s32 sext11(u32 value) noexcept {
  return (value & 0x400) != 0 ? static_cast<s32>(value | ~0x7FFu) : static_cast<s32>(value);
}

constexpr float decode_fog_float(u32 value) noexcept {
  return std::bit_cast<float>(reg_get(value, 1, 19) << 31 | reg_get(value, 8, 11) << 23 | reg_get(value, 11, 0) << 12);
}

// BP register decode

GXPixelFmt decode_pixel_fmt(u32 peCtrl, u32 cmode1) noexcept {
  switch (reg_get(peCtrl, 3, 0)) {
  case 0:
    return GX_PF_RGB8_Z24;
  case 1:
    return GX_PF_RGBA6_Z24;
  case 2:
    return GX_PF_RGB565_Z16;
  case 3:
    return GX_PF_Z24;
  case 4:
    switch (reg_get(cmode1, 2, 9)) {
    case 0:
      return GX_PF_Y8;
    case 1:
      return GX_PF_U8;
    case 2:
      return GX_PF_V8;
    default:
      Log.warn("command_processor: unsupported cmode1 pixel subtype {}", reg_get(cmode1, 2, 9));
      return GX_PF_Y8;
    }
  case 5:
    return GX_PF_YUV420;
  default:
    Log.warn("command_processor: unsupported PE pixel format {}", reg_get(peCtrl, 3, 0));
    return GX_PF_RGB8_Z24;
  }
}

// genMode (0x00)
void bp_gen_mode(u8, u32 value) noexcept {
  g_gxState.numTexGens = reg_get(value, 4, 0);
  g_gxState.numChans = reg_get(value, 3, 4);
  g_gxState.numTevStages = reg_get(value, 4, 10) + 1;
  u32 hwCull = reg_get(value, 2, 14);
  // BP encodes front/back opposite the GX representation
  switch (hwCull) {
  case GX_CULL_FRONT:
    g_gxState.cullMode = GX_CULL_BACK;
    break;
  case GX_CULL_BACK:
    g_gxState.cullMode = GX_CULL_FRONT;
    break;
  default:
    g_gxState.cullMode = static_cast<GXCullMode>(hwCull);
    break;
  }
  g_gxState.numIndStages = reg_get(value, 3, 16);
  g_gxState.xfRegValid.reset(0x09);
  g_gxState.xfRegValid.reset(0x3F);
}

// Indirect texture matrices (0x06-0x0E)
void bp_ind_mtx(u8 reg, u32 value) noexcept {
  u32 idx = (reg - 0x06) / 3;
  u32 column = (reg - 0x06) % 3;
  auto& info = g_gxState.indTexMtxs[idx];

  auto& packedColumn = column == 0 ? info.mtx.m0 : (column == 1 ? info.mtx.m1 : info.mtx.m2);
  packedColumn.x = static_cast<float>(sext11(reg_get(value, 11, 0))) / 1024.0f;
  packedColumn.y = static_cast<float>(sext11(reg_get(value, 11, 11))) / 1024.0f;

  // The SDK writes two scale bits per column, but the hardware ignores the top bit from the third column,
  // leaving an effective 5-bit value for adjScale = scaleExp + 17.
  u32 scaleBits = reg_get(value, 2, 22);
  u32 shift = column * 2;
  if (column == 2) {
    info.adjScaleRaw = (info.adjScaleRaw & ~(1u << shift)) | ((scaleBits & 1u) << shift);
  } else {
    info.adjScaleRaw = (info.adjScaleRaw & ~(3u << shift)) | (scaleBits << shift);
  }
  info.scaleExp = static_cast<s8>(info.adjScaleRaw) - 17;
}

// TEV indirect stage config (0x10-0x1F)
void bp_tev_ind(u8 reg, u32 value) noexcept {
  u32 stage = reg - 0x10;
  if (stage >= MaxTevStages) {
    return;
  }
  auto& s = g_gxState.tevStages[stage];
  s.indTexStage = static_cast<GXIndTexStageID>(reg_get(value, 2, 0));
  s.indTexFormat = static_cast<GXIndTexFormat>(reg_get(value, 2, 2));
  s.indTexBiasSel = static_cast<GXIndTexBiasSel>(reg_get(value, 3, 4));
  s.indTexAlphaSel = static_cast<GXIndTexAlphaSel>(reg_get(value, 2, 7));
  s.indTexMtxId = static_cast<GXIndTexMtxID>(reg_get(value, 4, 9));
  s.indTexWrapS = static_cast<GXIndTexWrap>(reg_get(value, 3, 13));
  s.indTexWrapT = static_cast<GXIndTexWrap>(reg_get(value, 3, 16));
  s.indTexUseOrigLOD = reg_get(value, 1, 19) != 0;
  s.indTexAddPrev = reg_get(value, 1, 20) != 0;
}

// Scissor (0x20/0x21)
void bp_scissor(u8, u32) noexcept {
  const u32 scis0 = g_gxState.bpRegCache[0x20];
  const u32 scis1 = g_gxState.bpRegCache[0x21];
  const s32 tp = static_cast<s32>(reg_get(scis0, 11, 0)) - 342;
  const s32 lf = static_cast<s32>(reg_get(scis0, 11, 12)) - 342;
  const s32 bm = static_cast<s32>(reg_get(scis1, 11, 0)) - 342;
  const s32 rt = static_cast<s32>(reg_get(scis1, 11, 12)) - 342;
  const s32 wd = std::max(rt - lf + 1, 0);
  const s32 ht = std::max(bm - tp + 1, 0);
  set_logical_scissor({lf, tp, wd, ht});
}

// Line/point size (0x22)
void bp_line_point_size(u8, u32 value) noexcept {
  g_gxState.lineWidth = static_cast<u8>(reg_get(value, 8, 0));
  g_gxState.pointSize = static_cast<u8>(reg_get(value, 8, 8));
  g_gxState.lineTexOffset = static_cast<GXTexOffset>(reg_get(value, 3, 16));
  g_gxState.pointTexOffset = static_cast<GXTexOffset>(reg_get(value, 3, 19));
  g_gxState.lineHalfAspect = reg_get(value, 1, 22) != 0;
}

// Indirect texture scale (0x25/0x26)
void bp_ind_scale(u8 reg, u32 value) noexcept {
  const u32 base = (reg - 0x25) * 2;
  for (u32 i = 0; i < 2; ++i) {
    if (base + i < MaxIndStages) {
      g_gxState.indStages[base + i].scaleS = static_cast<GXIndTexScale>(reg_get(value, 4, i * 8));
      g_gxState.indStages[base + i].scaleT = static_cast<GXIndTexScale>(reg_get(value, 4, i * 8 + 4));
    }
  }
}

// Indirect texture reference (0x27)
void bp_ind_ref(u8, u32 value) noexcept {
  for (u32 i = 0; i < MaxIndStages; i++) {
    g_gxState.indStages[i].texMapId = static_cast<GXTexMapID>(reg_get(value, 3, i * 6));
    g_gxState.indStages[i].texCoordId = static_cast<GXTexCoordID>(reg_get(value, 3, i * 6 + 3));
  }
}

// TEV order / tref (0x28-0x2F)
void bp_tref(u8 reg, u32 value) noexcept {
  u32 idx = reg - 0x28;

  // Reverse mapping from hardware to GX
  static constexpr GXChannelID r2c[] = {GX_COLOR0A0, GX_COLOR1A1,   GX_COLOR0A0,    GX_COLOR1A1,
                                        GX_COLOR0A0, GX_ALPHA_BUMP, GX_ALPHA_BUMPN, GX_COLOR_ZERO};

  for (u32 half = 0; half < 2; ++half) {
    const u32 stage = idx * 2 + half;
    if (stage >= MaxTevStages) {
      continue;
    }
    const u32 shift = half * 12;
    auto& s = g_gxState.tevStages[stage];
    s.texMapId = static_cast<GXTexMapID>(reg_get(value, 3, shift));
    s.texCoordId = static_cast<GXTexCoordID>(reg_get(value, 3, shift + 3));
    if (!reg_get(value, 1, shift + 6)) {
      s.texMapId = GX_TEXMAP_NULL;
    }
    u32 chanHw = reg_get(value, 3, shift + 7);
    s.channelId = (chanHw < 8) ? r2c[chanHw] : GX_COLOR_NULL;
  }
}

// SU texture coordinate scale (0x30-0x3F)
void bp_su_scale(u8 reg, u32 value) noexcept {
  auto& tcs = g_gxState.texCoordScales[(reg - 0x30) / 2];
  if ((reg & 1) != 0) {
    tcs.scaleT = static_cast<u16>(reg_get(value, 16, 0));
    tcs.biasT = reg_get(value, 1, 16) != 0;
    tcs.cylWrapT = reg_get(value, 1, 17) != 0;
  } else {
    tcs.scaleS = static_cast<u16>(reg_get(value, 16, 0));
    tcs.biasS = reg_get(value, 1, 16) != 0;
    tcs.cylWrapS = reg_get(value, 1, 17) != 0;
    tcs.lineOffset = reg_get(value, 1, 18) != 0;
    tcs.pointOffset = reg_get(value, 1, 19) != 0;
  }
}

// Z mode (0x40)
void bp_z_mode(u8, u32 value) noexcept {
  g_gxState.depthCompare = reg_get(value, 1, 0) != 0;
  g_gxState.depthFunc = static_cast<GXCompare>(reg_get(value, 3, 1));
  g_gxState.depthUpdate = reg_get(value, 1, 4) != 0;
}

// Blend mode / cmode0 (0x41)
void bp_cmode0(u8, u32 value) noexcept {
  bool blendEn = reg_get(value, 1, 0) != 0;
  bool logicEn = reg_get(value, 1, 1) != 0;
  g_gxState.colorUpdate = reg_get(value, 1, 3) != 0;
  g_gxState.alphaUpdate = reg_get(value, 1, 4) != 0;
  g_gxState.blendFacDst = static_cast<GXBlendFactor>(reg_get(value, 3, 5));
  g_gxState.blendFacSrc = static_cast<GXBlendFactor>(reg_get(value, 3, 8));
  bool subtract = reg_get(value, 1, 11) != 0;
  g_gxState.blendOp = static_cast<GXLogicOp>(reg_get(value, 4, 12));

  if (subtract) {
    g_gxState.blendMode = GX_BM_SUBTRACT;
  } else if (blendEn) {
    g_gxState.blendMode = GX_BM_BLEND;
  } else if (logicEn) {
    g_gxState.blendMode = GX_BM_LOGIC;
  } else {
    g_gxState.blendMode = GX_BM_NONE;
  }
}

// Dst alpha / cmode1 (0x42)
void bp_cmode1(u8, u32 value) noexcept {
  u8 alpha = reg_get(value, 8, 0);
  bool enabled = reg_get(value, 1, 8) != 0;
  g_gxState.dstAlpha = enabled ? alpha : UINT32_MAX;
  g_gxState.pixelFmt = decode_pixel_fmt(g_gxState.bpRegCache[0x43], value);
}

// PE control (0x43)
void bp_pe_ctrl(u8, u32 value) noexcept {
  g_gxState.pixelFmt = decode_pixel_fmt(value, g_gxState.bpRegCache[0x42]);
  g_gxState.zFmt = static_cast<GXZFmt16>(reg_get(value, 3, 3));
  g_gxState.zCompLocBeforeTex = reg_get(value, 1, 6) != 0;
}

// Copy clear color (0x4F/0x50) and depth (0x51)
void bp_clear_ra(u8, u32 value) noexcept {
  g_gxState.clearColor[0] = static_cast<float>(reg_get(value, 8, 0)) / 255.f;
  g_gxState.clearColor[3] = static_cast<float>(reg_get(value, 8, 8)) / 255.f;
}

void bp_clear_bg(u8, u32 value) noexcept {
  g_gxState.clearColor[2] = static_cast<float>(reg_get(value, 8, 0)) / 255.f;
  g_gxState.clearColor[1] = static_cast<float>(reg_get(value, 8, 8)) / 255.f;
}

void bp_clear_depth(u8, u32 value) noexcept { g_gxState.clearDepth = reg_get(value, 24, 0); }

// EFB copy trigger (0x52)
void bp_efb_copy(u8, u32 value) noexcept {
  const bool clear = reg_get(value, 1, 11) != 0;
  if (reg_get(value, 1, 14) != 0) {
    Log.warn("STUB: display copy is not implemented");
  } else {
    copy_tex(g_gxState.texCopyDest, clear);
  }
}

// TLUT load trigger (0x65)
void bp_tlut_load(u8, u32 value) noexcept {
  const auto idx = reg_get(value, 10, 0);
  if (idx < MaxTluts) {
    auto& slot = g_gxState.loadedTluts[idx];
    slot.loadTlut0 = g_gxState.bpRegCache[0x64];
    slot.numEntries = static_cast<u16>(reg_get(value, 10, 10) + 1);
  }
}

// TEV color combiner stages (0xC0, 0xC2, ... 0xDE)
void bp_tev_color(u8 reg, u32 value) noexcept {
  u32 stage = (reg - 0xC0) / 2;
  if (stage >= MaxTevStages) {
    return;
  }
  auto& s = g_gxState.tevStages[stage];
  s.colorPass.d = static_cast<GXTevColorArg>(reg_get(value, 4, 0));
  s.colorPass.c = static_cast<GXTevColorArg>(reg_get(value, 4, 4));
  s.colorPass.b = static_cast<GXTevColorArg>(reg_get(value, 4, 8));
  s.colorPass.a = static_cast<GXTevColorArg>(reg_get(value, 4, 12));
  s.colorOp.clamp = reg_get(value, 1, 19) != 0;
  s.colorOp.outReg = static_cast<GXTevRegID>(reg_get(value, 2, 22));
  if (reg_get(value, 2, 16) == 3) {
    u32 hwOp = reg_get(value, 1, 18) | (reg_get(value, 2, 20) << 1);
    s.colorOp.op = static_cast<GXTevOp>(hwOp + 8);
    s.colorOp.bias = GX_TB_ZERO;
    s.colorOp.scale = GX_CS_SCALE_1;
  } else {
    s.colorOp.op = static_cast<GXTevOp>(reg_get(value, 1, 18));
    s.colorOp.bias = static_cast<GXTevBias>(reg_get(value, 2, 16));
    s.colorOp.scale = static_cast<GXTevScale>(reg_get(value, 2, 20));
  }
}

// TEV alpha combiner stages (0xC1, 0xC3, ... 0xDF)
void bp_tev_alpha(u8 reg, u32 value) noexcept {
  u32 stage = (reg - 0xC1) / 2;
  if (stage >= MaxTevStages) {
    return;
  }
  auto& s = g_gxState.tevStages[stage];
  s.tevSwapRas = static_cast<GXTevSwapSel>(reg_get(value, 2, 0));
  s.tevSwapTex = static_cast<GXTevSwapSel>(reg_get(value, 2, 2));
  s.alphaPass.d = static_cast<GXTevAlphaArg>(reg_get(value, 3, 4));
  s.alphaPass.c = static_cast<GXTevAlphaArg>(reg_get(value, 3, 7));
  s.alphaPass.b = static_cast<GXTevAlphaArg>(reg_get(value, 3, 10));
  s.alphaPass.a = static_cast<GXTevAlphaArg>(reg_get(value, 3, 13));
  s.alphaOp.clamp = reg_get(value, 1, 19) != 0;
  s.alphaOp.outReg = static_cast<GXTevRegID>(reg_get(value, 2, 22));
  if (reg_get(value, 2, 16) == 3) {
    u32 hwOp = reg_get(value, 1, 18) | (reg_get(value, 2, 20) << 1);
    s.alphaOp.op = static_cast<GXTevOp>(hwOp + 8);
    s.alphaOp.bias = GX_TB_ZERO;
    s.alphaOp.scale = GX_CS_SCALE_1;
  } else {
    s.alphaOp.op = static_cast<GXTevOp>(reg_get(value, 1, 18));
    s.alphaOp.bias = static_cast<GXTevBias>(reg_get(value, 2, 16));
    s.alphaOp.scale = static_cast<GXTevScale>(reg_get(value, 2, 20));
  }
}

// TEV color registers / K color registers (0xE0-0xE7)
void bp_tev_reg(u8 reg, u32 value) noexcept {
  u32 idx = (reg - 0xE0) / 2;
  bool isRA = (reg & 1) == 0;
  if (reg_get(value, 1, 23) != 0) {
    // K color register (8-bit components)
    if (idx < GX_MAX_KCOLOR) {
      auto& kc = g_gxState.kcolors[idx];
      if (isRA) {
        kc[0] = static_cast<float>(reg_get(value, 8, 0)) / 255.f;  // R
        kc[3] = static_cast<float>(reg_get(value, 8, 12)) / 255.f; // A
      } else {
        kc[2] = static_cast<float>(reg_get(value, 8, 0)) / 255.f;  // B
        kc[1] = static_cast<float>(reg_get(value, 8, 12)) / 255.f; // G
      }
    }
  } else {
    // TEV color register (11-bit signed components)
    if (idx < MaxTevRegs) {
      auto& cr = g_gxState.colorRegs[idx];
      if (isRA) {
        cr[0] = static_cast<float>(sext11(reg_get(value, 11, 0))) / 255.f;
        cr[3] = static_cast<float>(sext11(reg_get(value, 11, 12))) / 255.f;
      } else {
        cr[2] = static_cast<float>(sext11(reg_get(value, 11, 0))) / 255.f;
        cr[1] = static_cast<float>(sext11(reg_get(value, 11, 12))) / 255.f;
      }
    }
  }
}

// Fog range adjustment (0xE8-0xED)
void bp_fog_range_base(u8, u32 value) noexcept {
  g_gxState.fog.rangeCenter = static_cast<s16>(reg_get(value, 10, 0)) - 342;
  const bool rangeEnabled = reg_get(value, 1, 10) != 0;
  if (g_gxState.fog.rangeEnabled != rangeEnabled) {
    g_gxState.dirty |= DirtyPipeline;
  }
  g_gxState.fog.rangeEnabled = rangeEnabled;
}

void bp_fog_range_k(u8 reg, u32 value) noexcept {
  const u32 idx = (reg - 0xE9) * 2;
  g_gxState.fog.rangeK[idx] = static_cast<u16>(reg_get(value, 12, 0));
  g_gxState.fog.rangeK[idx + 1] = static_cast<u16>(reg_get(value, 12, 12));
}

void decode_fog_a() noexcept {
  const float encoded = decode_fog_float(g_gxState.fog.fog0Raw);
  if ((g_gxState.fog.type & 0x08) != 0) {
    g_gxState.fog.a = encoded;
  } else {
    const u32 b_s = g_gxState.fog.fog2Raw & 0x1F;
    g_gxState.fog.a = std::ldexp(encoded, static_cast<int>(b_s));
  }
}

// Fog parameters (0xEE-0xF2)
void bp_fog0(u8, u32 value) noexcept {
  g_gxState.fog.fog0Raw = value;
  decode_fog_a();
}

void bp_fog1(u8, u32 value) noexcept {
  g_gxState.fog.fog1Raw = value;
  u32 b_m = reg_get(value, 24, 0);
  u32 b_s = g_gxState.fog.fog2Raw & 0x1F;
  float B_mant = static_cast<float>(b_m) / 8388638.0f;
  g_gxState.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
}

void bp_fog2(u8, u32 value) noexcept {
  g_gxState.fog.fog2Raw = value;
  u32 b_s = reg_get(value, 5, 0);
  decode_fog_a();
  u32 b_m = reg_get(g_gxState.fog.fog1Raw, 24, 0);
  float B_mant = static_cast<float>(b_m) / 8388638.0f;
  g_gxState.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
}

// Fog type + C parameter (0xF1)
void bp_fog3(u8, u32 value) noexcept {
  const u32 type = reg_get(value, 3, 21) | (reg_get(value, 1, 20) << 3);
  g_gxState.fog.type = static_cast<GXFogType>(type);
  // A's exponent applies only to perspective fog, so type changes require re-decoding.
  decode_fog_a();
  g_gxState.fog.c = decode_fog_float(value);
}

void bp_fog_color(u8, u32 value) noexcept {
  g_gxState.fog.color = {
      static_cast<float>(reg_get(value, 8, 16)) / 255.f,
      static_cast<float>(reg_get(value, 8, 8)) / 255.f,
      static_cast<float>(reg_get(value, 8, 0)) / 255.f,
      1.f,
  };
}

// Alpha compare (0xF3)
void bp_alpha_compare(u8, u32 value) noexcept {
  g_gxState.alphaCompare.ref0 = reg_get(value, 8, 0);
  g_gxState.alphaCompare.ref1 = reg_get(value, 8, 8);
  g_gxState.alphaCompare.comp0 = static_cast<GXCompare>(reg_get(value, 3, 16));
  g_gxState.alphaCompare.comp1 = static_cast<GXCompare>(reg_get(value, 3, 19));
  g_gxState.alphaCompare.op = static_cast<GXAlphaOp>(reg_get(value, 2, 22));
}

// TEV K select (0xF6-0xFD)
void bp_ksel(u8 reg, u32 value) noexcept {
  u32 kselIdx = reg - 0xF6;
  if (kselIdx < MaxTevSwap * 2) {
    u32 swapIdx = kselIdx / 2;
    if (kselIdx & 1) {
      g_gxState.tevSwapTable[swapIdx].blue = static_cast<GXTevColorChan>(reg_get(value, 2, 0));
      g_gxState.tevSwapTable[swapIdx].alpha = static_cast<GXTevColorChan>(reg_get(value, 2, 2));
    } else {
      g_gxState.tevSwapTable[swapIdx].red = static_cast<GXTevColorChan>(reg_get(value, 2, 0));
      g_gxState.tevSwapTable[swapIdx].green = static_cast<GXTevColorChan>(reg_get(value, 2, 2));
    }
  }
  u32 stage0 = kselIdx * 2;
  u32 stage1 = kselIdx * 2 + 1;
  if (stage0 < MaxTevStages) {
    g_gxState.tevStages[stage0].kcSel = static_cast<GXTevKColorSel>(reg_get(value, 5, 4));
    g_gxState.tevStages[stage0].kaSel = static_cast<GXTevKAlphaSel>(reg_get(value, 5, 9));
  }
  if (stage1 < MaxTevStages) {
    g_gxState.tevStages[stage1].kcSel = static_cast<GXTevKColorSel>(reg_get(value, 5, 14));
    g_gxState.tevStages[stage1].kaSel = static_cast<GXTevKAlphaSel>(reg_get(value, 5, 19));
  }
}

// Texture registers (0x80 for maps 0-3, 0xA0 for maps 4-7)
void bp_tex(u8 reg, u32 value) noexcept {
  const u32 idx = reg - 0x80;
  const u32 kind = (idx & 0x1F) / 4;
  const u32 texMapId = (idx & 3) + (idx >= 0x20 ? 4 : 0);
  auto& slot = g_gxState.loadedTextures[texMapId];
  switch (kind) {
  case 0: // Mode0
    slot.mode0 = value;
    break;
  case 1: // Mode1
    slot.mode1 = value;
    break;
  case 2: // Image0
    slot.image0 = value;
    slot.mWidth = 0;
    slot.mHeight = 0;
    slot.mFormat = gfx::InvalidTextureFormat;
    break;
  case 5: // Image3
    slot.image3 = value;
    break;
  default:
    break;
  }
}

void bp_unhandled(u8 reg, u32 value) noexcept {
#ifndef NDEBUG
  Log.debug("Unhandled BP register 0x{:02X} (value 0x{:06X})", reg, value & 0xFFFFFF);
#else
  (void)reg;
  (void)value;
#endif
}

constexpr auto kBpRegs = [] {
  std::array<RegHandler, 0x100> regs{};
  for (auto& reg : regs) {
    reg = {bp_unhandled};
  }
  regs[0x00] = {bp_gen_mode, DirtyPipeline};
  for (u8 r = 0x06; r <= 0x0E; ++r) {
    regs[r] = {bp_ind_mtx, DirtyUniform};
  }
  regs[0x0F] = {}; // Indirect texture map mask
  for (u8 r = 0x10; r <= 0x1F; ++r) {
    regs[r] = {bp_tev_ind, DirtyPipeline};
  }
  regs[0x20] = {bp_scissor};
  regs[0x21] = {bp_scissor};
  regs[0x22] = {bp_line_point_size, DirtyUniform};
  regs[0x25] = {bp_ind_scale, DirtyPipeline};
  regs[0x26] = {bp_ind_scale, DirtyPipeline};
  regs[0x27] = {bp_ind_ref, DirtyPipeline};
  for (u8 r = 0x28; r <= 0x2F; ++r) {
    regs[r] = {bp_tref, DirtyPipeline};
  }
  for (u8 r = 0x30; r <= 0x3F; ++r) {
    regs[r] = {bp_su_scale, DirtyUniform};
  }
  regs[0x40] = {bp_z_mode, DirtyPipeline};
  regs[0x41] = {bp_cmode0, DirtyPipeline};
  regs[0x42] = {bp_cmode1, DirtyPipeline};
  regs[0x43] = {bp_pe_ctrl};
  regs[0x4F] = {bp_clear_ra};
  regs[0x50] = {bp_clear_bg};
  regs[0x51] = {bp_clear_depth};
  regs[0x52] = {bp_efb_copy, 0, /* alwaysHandle */ true};
  regs[0x64] = {}; // TLUT load address; bp_tlut_load reads from shadow
  regs[0x65] = {bp_tlut_load, DirtyTextures, /* alwaysHandle */ true};
  for (u32 base : {0x80u, 0xA0u}) {
    for (u32 i = 0; i < 4; ++i) {
      regs[base + 0x00 + i] = {bp_tex, DirtyTextures}; // Mode0
      regs[base + 0x04 + i] = {bp_tex, DirtyTextures}; // Mode1
      regs[base + 0x08 + i] = {bp_tex, DirtyTextures}; // Image0
      regs[base + 0x0C + i] = {};                      // Image1 (GXTexRegion)
      regs[base + 0x10 + i] = {};                      // Image2 (GXTexRegion)
      regs[base + 0x14 + i] = {bp_tex, DirtyTextures}; // Image3
      regs[base + 0x18 + i] = {};                      // TLUT region TMEM offset
    }
  }
  for (u8 r = 0xC0; r <= 0xDE; r += 2) {
    regs[r] = {bp_tev_color, DirtyPipeline};
    regs[r + 1] = {bp_tev_alpha, DirtyPipeline};
  }
  for (u8 r = 0xE0; r <= 0xE7; ++r) {
    regs[r] = {bp_tev_reg, DirtyUniform};
  }
  regs[0xE8] = {bp_fog_range_base, DirtyUniform};
  for (u8 r = 0xE9; r <= 0xED; ++r) {
    regs[r] = {bp_fog_range_k, DirtyUniform};
  }
  regs[0xEE] = {bp_fog0, DirtyUniform};
  regs[0xEF] = {bp_fog1, DirtyUniform};
  regs[0xF0] = {bp_fog2, DirtyUniform};
  regs[0xF1] = {bp_fog3, DirtyPipeline | DirtyUniform}; // fog.type affects shader
  regs[0xF2] = {bp_fog_color, DirtyUniform};
  regs[0xF3] = {bp_alpha_compare, DirtyPipeline};
  for (u8 r = 0xF6; r <= 0xFD; ++r) {
    regs[r] = {bp_ksel, DirtyPipeline};
  }
  return regs;
}();

// CP register decode

// Matrix index A (0x30)
void cp_mtx_index(u8, u32 value) noexcept {
  g_gxState.currentPnMtx = reg_get(value, 6, 0) / 3;
  g_gxState.xfRegValid.reset(0x18);
}

// VCD low (0x50)
void cp_vcd_lo(u8, u32 value) noexcept {
  auto& vd = g_gxState.vtxDesc;
  vd[GX_VA_PNMTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 0));
  vd[GX_VA_TEX0MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 1));
  vd[GX_VA_TEX1MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 2));
  vd[GX_VA_TEX2MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 3));
  vd[GX_VA_TEX3MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 4));
  vd[GX_VA_TEX4MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 5));
  vd[GX_VA_TEX5MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 6));
  vd[GX_VA_TEX6MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 7));
  vd[GX_VA_TEX7MTXIDX] = static_cast<GXAttrType>(reg_get(value, 1, 8));
  vd[GX_VA_POS] = static_cast<GXAttrType>(reg_get(value, 2, 9));
  vd[GX_VA_NRM] = static_cast<GXAttrType>(reg_get(value, 2, 11));
  vd[GX_VA_CLR0] = static_cast<GXAttrType>(reg_get(value, 2, 13));
  vd[GX_VA_CLR1] = static_cast<GXAttrType>(reg_get(value, 2, 15));
  g_gxState.clearVtxSizeCache();
}

// VCD high (0x60)
void cp_vcd_hi(u8, u32 value) noexcept {
  auto& vd = g_gxState.vtxDesc;
  for (u32 i = 0; i < 8; ++i) {
    vd[GX_VA_TEX0 + i] = static_cast<GXAttrType>(reg_get(value, 2, i * 2));
  }
  g_gxState.clearVtxSizeCache();
}

// VAT A (0x70-0x77)
void cp_vat_a(u8 addr, u32 value) noexcept {
  auto& vf = g_gxState.vtxFmts[addr - 0x70];
  vf.attrs[GX_VA_POS].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 0));
  vf.attrs[GX_VA_POS].type = static_cast<GXCompType>(reg_get(value, 3, 1));
  vf.attrs[GX_VA_POS].frac = static_cast<u8>(reg_get(value, 5, 4));
  const auto nrm_cnt = reg_get(value, 1, 9);
  const auto nrm_nbt3 = reg_get(value, 1, 31);
  vf.attrs[GX_VA_NRM].cnt = nrm_nbt3 ? GX_NRM_NBT3 : nrm_cnt ? GX_NRM_NBT : GX_NRM_XYZ;
  vf.attrs[GX_VA_NRM].type = static_cast<GXCompType>(reg_get(value, 3, 10));
  if (vf.attrs[GX_VA_NRM].type == GX_U8 || vf.attrs[GX_VA_NRM].type == GX_S8) {
    vf.attrs[GX_VA_NRM].frac = 6;
  } else if (vf.attrs[GX_VA_NRM].type == GX_U16 || vf.attrs[GX_VA_NRM].type == GX_S16) {
    vf.attrs[GX_VA_NRM].frac = 14;
  } else {
    vf.attrs[GX_VA_NRM].frac = 0;
  }
  vf.attrs[GX_VA_CLR0].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 13));
  vf.attrs[GX_VA_CLR0].type = static_cast<GXCompType>(reg_get(value, 3, 14));
  vf.attrs[GX_VA_CLR0].frac = 0;
  vf.attrs[GX_VA_CLR1].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 17));
  vf.attrs[GX_VA_CLR1].type = static_cast<GXCompType>(reg_get(value, 3, 18));
  vf.attrs[GX_VA_CLR1].frac = 0;
  vf.attrs[GX_VA_TEX0].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 21));
  vf.attrs[GX_VA_TEX0].type = static_cast<GXCompType>(reg_get(value, 3, 22));
  vf.attrs[GX_VA_TEX0].frac = static_cast<u8>(reg_get(value, 5, 25));
  g_gxState.clearVtxSizeCache();
}

// VAT B (0x80-0x87)
void cp_vat_b(u8 addr, u32 value) noexcept {
  auto& vf = g_gxState.vtxFmts[addr - 0x80];
  vf.attrs[GX_VA_TEX1].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 0));
  vf.attrs[GX_VA_TEX1].type = static_cast<GXCompType>(reg_get(value, 3, 1));
  vf.attrs[GX_VA_TEX1].frac = static_cast<u8>(reg_get(value, 5, 4));
  vf.attrs[GX_VA_TEX2].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 9));
  vf.attrs[GX_VA_TEX2].type = static_cast<GXCompType>(reg_get(value, 3, 10));
  vf.attrs[GX_VA_TEX2].frac = static_cast<u8>(reg_get(value, 5, 13));
  vf.attrs[GX_VA_TEX3].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 18));
  vf.attrs[GX_VA_TEX3].type = static_cast<GXCompType>(reg_get(value, 3, 19));
  vf.attrs[GX_VA_TEX3].frac = static_cast<u8>(reg_get(value, 5, 22));
  vf.attrs[GX_VA_TEX4].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 27));
  vf.attrs[GX_VA_TEX4].type = static_cast<GXCompType>(reg_get(value, 3, 28));
  // TEX4 frac is in VAT C
  g_gxState.clearVtxSizeCache();
}

// VAT C (0x90-0x97)
void cp_vat_c(u8 addr, u32 value) noexcept {
  auto& vf = g_gxState.vtxFmts[addr - 0x90];
  vf.attrs[GX_VA_TEX4].frac = static_cast<u8>(reg_get(value, 5, 0));
  vf.attrs[GX_VA_TEX5].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 5));
  vf.attrs[GX_VA_TEX5].type = static_cast<GXCompType>(reg_get(value, 3, 6));
  vf.attrs[GX_VA_TEX5].frac = static_cast<u8>(reg_get(value, 5, 9));
  vf.attrs[GX_VA_TEX6].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 14));
  vf.attrs[GX_VA_TEX6].type = static_cast<GXCompType>(reg_get(value, 3, 15));
  vf.attrs[GX_VA_TEX6].frac = static_cast<u8>(reg_get(value, 5, 18));
  vf.attrs[GX_VA_TEX7].cnt = static_cast<GXCompCnt>(reg_get(value, 1, 23));
  vf.attrs[GX_VA_TEX7].type = static_cast<GXCompType>(reg_get(value, 3, 24));
  vf.attrs[GX_VA_TEX7].frac = static_cast<u8>(reg_get(value, 5, 27));
  g_gxState.clearVtxSizeCache();
}

void cp_arraybase_unsupported(u8, u32) noexcept {
  Log.error("CP_REG_ARRAYBASE_ID is not supported. Use GX_AURORA_LOAD_ARRAYBASE instead.");
}

// Array strides (0xB0-0xBF)
void cp_array_stride(u8 addr, u32 value) noexcept {
  u32 attrIdx = addr - 0xB0 + GX_VA_POS;
  if (attrIdx < GX_VA_MAX_ATTR) {
    g_gxState.arrays[attrIdx].stride = static_cast<u8>(value);
  }
}

constexpr auto kCpRegs = [] {
  std::array<RegHandler, 0x100> regs{};
  regs[0x30] = {cp_mtx_index, DirtyImmediates};
  regs[0x40] = {}; // Matrix index B; mirrors XF 0x19
  regs[0x50] = {cp_vcd_lo, DirtyPipeline};
  regs[0x60] = {cp_vcd_hi, DirtyPipeline};
  for (u8 i = 0; i < 8; ++i) {
    regs[0x70 + i] = {cp_vat_a, DirtyPipeline};
    regs[0x80 + i] = {cp_vat_b, DirtyPipeline};
    regs[0x90 + i] = {cp_vat_c, DirtyPipeline};
  }
  for (u8 i = 0; i < 0x10; ++i) {
    regs[0xA0 + i] = {cp_arraybase_unsupported, 0, /* alwaysHandle */ true};
    regs[0xB0 + i] = {cp_array_stride, DirtyPipeline};
  }
  return regs;
}();

// XF register decode

// numChans (0x09)
void xf_num_chans(u8, u32 value) noexcept {
  g_gxState.numChans = value;
  g_gxState.bpRegValid.reset(0x00);
}

// Ambient/material colors (0x0A-0x0D)
void xf_chan_color(u8 reg, u32 value) noexcept {
  const u32 chan = (reg - 0x0A) & 1;
  const auto color = unpack_color(value);
  if (reg <= 0x0B) {
    g_gxState.colorChannelState[GX_COLOR0 + chan].ambColor = color;
    g_gxState.colorChannelState[GX_ALPHA0 + chan].ambColor = color;
  } else {
    g_gxState.colorChannelState[GX_COLOR0 + chan].matColor = color;
    g_gxState.colorChannelState[GX_ALPHA0 + chan].matColor = color;
  }
}

// Channel control (0x0E-0x11)
void xf_chan_ctrl(u8 reg, u32 value) noexcept {
  u32 chanId = reg - 0x0E;
  if (chanId >= MaxColorChannels) {
    return;
  }
  auto& chan = g_gxState.colorChannelConfig[chanId];
  chan.matSrc = static_cast<GXColorSrc>(reg_get(value, 1, 0));
  chan.lightingEnabled = reg_get(value, 1, 1) != 0;
  u32 lightsLo = reg_get(value, 4, 2);
  chan.ambSrc = static_cast<GXColorSrc>(reg_get(value, 1, 6));
  chan.diffFn = static_cast<GXDiffuseFn>(reg_get(value, 2, 7));
  // bit 9 = (attnFn != GX_AF_NONE), bit 10 = (attnFn != GX_AF_SPEC)
  bool bit9 = reg_get(value, 1, 9) != 0;
  bool bit10 = reg_get(value, 1, 10) != 0;
  u32 lightsHi = reg_get(value, 4, 11);
  if (!bit10) {
    chan.attnFn = GX_AF_SPEC;
  } else if (!bit9) {
    chan.attnFn = GX_AF_NONE;
  } else {
    chan.attnFn = GX_AF_SPOT;
  }
  g_gxState.colorChannelState[chanId].lightMask = GX::LightMask{lightsLo | (lightsHi << 4)};
}

// Matrix index A (0x18)
void xf_mtx_index_a(u8, u32 value) noexcept {
  g_gxState.currentPnMtx = reg_get(value, 6, 0) / 3;
  g_gxState.cpRegValid.reset(0x30); // CP 0x30 also writes currentPnMtx
  for (u32 i = 0; i < 4; i++) {
    auto texMtx = static_cast<GXTexMtx>(reg_get(value, 6, 6 + i * 6));
    assert(texMtx >= 0 && texMtx <= GXTexMtx::GX_IDENTITY);
    if (g_gxState.tcgs[i].mtx != texMtx) {
      g_gxState.tcgs[i].mtx = texMtx;
      g_gxState.dirty |= DirtyPipeline;
    }
  }
}

// Matrix index B (0x19)
void xf_mtx_index_b(u8, u32 value) noexcept {
  for (u32 i = 0; i < 4 && (i + 4) < MaxTexCoord; i++) {
    g_gxState.tcgs[i + 4].mtx = static_cast<GXTexMtx>(reg_get(value, 6, i * 6));
  }
}

// numTexGens (0x3F)
void xf_num_texgens(u8, u32 value) noexcept {
  g_gxState.numTexGens = value;
  g_gxState.bpRegValid.reset(0x00);
}

// TexGen config (0x40-0x4F)
void xf_texgen(u8 reg, u32 value) noexcept {
  u32 tcIdx = reg - 0x40;
  if (tcIdx >= MaxTexCoord) {
    return;
  }
  auto& tcg = g_gxState.tcgs[tcIdx];
  bool proj = reg_get(value, 1, 1) != 0;
  u32 tgType = reg_get(value, 3, 4);
  u32 srcRow = reg_get(value, 5, 7);

  if (tgType == 0) {
    tcg.type = proj ? GX_TG_MTX3x4 : GX_TG_MTX2x4;
  } else if (tgType == 1) {
    // Bump mapping: type encodes emboss light
    tcg.type = static_cast<GXTexGenType>(reg_get(value, 3, 15) + 2);
  } else if (tgType == 2 || tgType == 3) {
    tcg.type = GX_TG_SRTG;
    tcg.src = tgType == 2 ? GX_TG_COLOR0 : GX_TG_COLOR1;
  }
  // Emboss source texcoord (bits 12-14); 0 for non-bump types
  tcg.embossSrc = reg_get(value, 3, 12);

  static constexpr GXTexGenSrc rowToSrc[] = {GX_TG_POS,  GX_TG_NRM,  GX_TG_COLOR0, GX_TG_BINRM, GX_TG_TANGENT,
                                             GX_TG_TEX0, GX_TG_TEX1, GX_TG_TEX2,   GX_TG_TEX3,  GX_TG_TEX4,
                                             GX_TG_TEX5, GX_TG_TEX6, GX_TG_TEX7};
  if (tgType != 2 && tgType != 3 && srcRow < 13) {
    tcg.src = rowToSrc[srcRow];
  }
}

// Post-transform matrix select (0x50-0x57)
void xf_post_mtx(u8 reg, u32 value) noexcept {
  u32 tcIdx = reg - 0x50;
  if (tcIdx >= MaxTexCoord) {
    return;
  }
  g_gxState.tcgs[tcIdx].postMtx = static_cast<GXPTTexMtx>(reg_get(value, 6, 0) + 64);
  g_gxState.tcgs[tcIdx].normalize = reg_get(value, 1, 8) != 0;
}

void xf_unhandled(u8 reg, u32 value) noexcept {
#ifndef NDEBUG
  Log.debug("Unhandled XF register 0x{:04X} (value 0x{:08X})", static_cast<u32>(reg), value);
#else
  (void)reg;
  (void)value;
#endif
}

constexpr auto kXfRegs = [] {
  std::array<RegHandler, XfRegCount> regs{};
  for (auto& reg : regs) {
    reg = {xf_unhandled};
  }
  regs[0x08] = {}; // vertex specs (numColors/numNormals/numTexCoords)
  regs[0x09] = {xf_num_chans};
  for (u8 r = 0x0A; r <= 0x0D; ++r) {
    regs[r] = {xf_chan_color, DirtyUniform};
  }
  for (u8 r = 0x0E; r <= 0x11; ++r) {
    // Writes both the channel config (shader) and the light mask (uniform)
    regs[r] = {xf_chan_ctrl, DirtyPipeline | DirtyUniform};
  }
  regs[0x18] = {xf_mtx_index_a, DirtyImmediates};
  regs[0x19] = {xf_mtx_index_b, DirtyPipeline};
  // 0x1A-0x1F (viewport) and 0x20-0x26 (projection) decode as blocks in handle_xf
  for (u8 r = 0x1A; r <= 0x26; ++r) {
    regs[r] = {};
  }
  regs[0x3F] = {xf_num_texgens, DirtyPipeline};
  for (u8 r = 0x40; r <= 0x4F; ++r) {
    regs[r] = {xf_texgen, DirtyPipeline};
  }
  for (u8 r = 0x50; r <= 0x57; ++r) {
    regs[r] = {xf_post_mtx, DirtyPipeline};
  }
  return regs;
}();

// Returns true if the value did not change, otherwise updates the shadow register
bool xf_reg_unchanged(u32 reg, u32 val) noexcept {
  if (g_gxState.xfRegValid.test(reg) && g_gxState.xfRegCache[reg] == val) {
    return true;
  }
  g_gxState.xfRegValid.set(reg);
  g_gxState.xfRegCache[reg] = val;
  return false;
}

// Updates every shadow register in the block and returns true only if all words matched
bool xf_block_unchanged(u32 firstReg, const u8* data, u32 count) noexcept {
  bool unchanged = true;
  for (u32 w = 0; w < count; w++) {
    unchanged &= xf_reg_unchanged(firstReg + w, read_bits<u32>(data + w * 4));
  }
  return unchanged;
}

// Viewport (0x101A-0x101F)
void xf_load_viewport(const u8* data) noexcept {
  f32 sx = read_bits<f32>(data + 0);
  f32 sy = read_bits<f32>(data + 4);
  f32 sz = read_bits<f32>(data + 8);
  f32 ox = read_bits<f32>(data + 12);
  f32 oy = read_bits<f32>(data + 16);
  f32 oz = read_bits<f32>(data + 20);
  f32 width = sx * 2.0f;
  f32 height = -sy * 2.0f;
  set_logical_viewport({
      .left = ox - 340.0f - width / 2.0f,
      .top = oy - 340.0f - height / 2.0f,
      .width = width,
      .height = height,
      .znear = (oz - sz) / 1.6777215e7f,
      .zfar = oz / 1.6777215e7f,
  });
}

// Projection (0x1020-0x1026)
void xf_load_projection(const u8* data) noexcept {
  f32 p0 = read_bits<f32>(data + 0);
  f32 p1 = read_bits<f32>(data + 4);
  f32 p2 = read_bits<f32>(data + 8);
  f32 p3 = read_bits<f32>(data + 12);
  f32 p4 = read_bits<f32>(data + 16);
  f32 p5 = read_bits<f32>(data + 20);
  u32 projType = read_bits<u32>(data + 24);
  g_gxState.projType = static_cast<GXProjectionType>(projType);
  auto& proj = g_gxState.proj;
  proj = {};
  proj.m0[0] = p0;
  proj.m1[1] = p2;
  proj.m2[2] = p4;
  proj.m2[3] = p5;
  if (projType == GX_ORTHOGRAPHIC) {
    proj.m0[3] = p1;
    proj.m1[3] = p3;
    proj.m3[3] = 1.0f;
  } else {
    proj.m0[2] = p1;
    proj.m1[2] = p3;
    proj.m3[2] = -1.0f;
  }
  g_gxState.dirty |= DirtyUniform;
}

// Compare raw bits instead of floats
bool store_xf_f32(f32& dst, u32 bits) noexcept {
  if (std::bit_cast<u32>(dst) == bits) {
    return false;
  }
  dst = std::bit_cast<f32>(bits);
  return true;
}

} // namespace

// Dispatch

void handle_bp(u32 value) noexcept {
  u32 regId = value >> 24;
  if (regId == 0xFE) {
    // BP mask write: applies to the next BP register write only
    g_gxState.bpRegCache[regId] = value & 0x00FFFFFF;
    return;
  }

  auto& cache = g_gxState.bpRegCache;
  const u32 ssMask = cache[0xFE];
  cache[0xFE] = 0x00FFFFFF;
  value = (regId << 24) | (((cache[regId] & ~ssMask) | (value & ssMask)) & 0x00FFFFFF);

  const RegHandler& reg = kBpRegs[regId];
  if (g_gxState.bpRegValid.test(regId) && cache[regId] == value && !reg.alwaysHandle) {
    return;
  }
  g_gxState.bpRegValid.set(regId);
  cache[regId] = value;
  g_gxState.dirty |= reg.dirty;
  if (reg.decode != nullptr) {
    reg.decode(static_cast<u8>(regId), value);
  }
}

void handle_cp(u8 addr, u32 value) noexcept {
  const RegHandler& reg = kCpRegs[addr];
  if (!reg.alwaysHandle) {
    if (g_gxState.cpRegValid.test(addr) && g_gxState.cpRegCache[addr] == value) {
      return;
    }
    g_gxState.cpRegValid.set(addr);
    g_gxState.cpRegCache[addr] = value;
  }
  g_gxState.dirty |= reg.dirty;
  if (reg.decode != nullptr) {
    reg.decode(addr, value);
  }
}

bool copy_xf_data(u32 addr, const u8* data, u32 len, std::endian e) noexcept {
  if (addr < 0x78) {
    // Position matrices (0x0000-0x0077)
    u32 mtxIdx = addr / 12;
    u32 startOffset = addr % 12;
    CHECK(mtxIdx < MaxPnMtx, "XF: PosMtx copy oob? Should never happen; mtxIdx={}", mtxIdx);
    CHECK(startOffset == 0 && len == 12, "XF: PosMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    f32* flat = reinterpret_cast<f32*>(&g_gxState.pnMtx[mtxIdx].pos);
    bool changed = false;
    for (u32 i = 0; i < len; i++) {
      changed |= store_xf_f32(flat[i], read_bits<u32>(data + i * 4, e));
    }
    if (changed) {
      g_gxState.dirty |= DirtyUniform;
    }
    return true;
  }
  if (addr < 0x0F0) {
    // Texture matrices (0x078-0x0EF)
    u32 texBase = addr - 0x078;
    u32 mtxIdx = texBase / 12;
    u32 startOffset = texBase % 12;
    CHECK(mtxIdx < MaxTexMtx, "XF TexMtx copy oob? Should never happen; mtxIdx={}", mtxIdx);
    CHECK(startOffset == 0 && (len == 8 || len == 12), "XF TexMtx sub-copy unsupported: offs={}, len={}", startOffset,
          len);

    f32* flat = reinterpret_cast<f32*>(&g_gxState.texMtxs[mtxIdx]);
    bool changed = false;
    for (u32 i = 0; i < len; i++) {
      changed |= store_xf_f32(flat[i], read_bits<u32>(data + i * 4, e));
    }
    if (changed) {
      g_gxState.dirty |= DirtyUniform;
    }
    return true;
  }
  if (addr >= 0x400 && addr < 0x45A) {
    // Normal matrices (0x400-0x459)
    u32 nrmBase = addr - 0x400;
    u32 mtxIdx = nrmBase / 9;
    u32 startOffset = nrmBase % 9;
    CHECK(mtxIdx < MaxPnMtx, "XF: NrmMtx copy oob? Should never happen; mtxIdx={}", mtxIdx);
    CHECK(startOffset == 0 && len == 9, "XF: NrmMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    f32* flat = reinterpret_cast<f32*>(&g_gxState.pnMtx[mtxIdx].nrm);
    bool changed = false;
    for (u32 i = 0; i < len; i++) {
      // 3x3 source packed into 3x4 storage
      u32 row = i / 3;
      u32 col = i % 3;
      changed |= store_xf_f32(flat[row * 4 + col], read_bits<u32>(data + i * 4, e));
    }
    if (changed) {
      g_gxState.dirty |= DirtyUniform;
    }
    return true;
  }
  if (addr >= 0x500 && addr < 0x5F0) {
    // Post-transform texture matrices (0x500-0x5EF)
    u32 ptBase = addr - 0x500;
    u32 mtxIdx = ptBase / 12;
    u32 startOffset = ptBase % 12;
    CHECK(mtxIdx < MaxPTTexMtx, "XF: PTTexMtx copy oob? Should never happen; mtxIdx={}", mtxIdx);
    CHECK(startOffset == 0 && len == 12, "XF: PTTexMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    f32* flat = reinterpret_cast<f32*>(&g_gxState.ptTexMtxs[mtxIdx]);
    bool changed = false;
    for (u32 i = 0; i < len; i++) {
      changed |= store_xf_f32(flat[i], read_bits<u32>(data + i * 4, e));
    }
    if (changed) {
      g_gxState.dirty |= DirtyUniform;
    }
    return true;
  }
  if (addr >= 0x600 && addr < 0x680) {
    // Lights (0x600-0x67F)
    u32 lightBase = addr - 0x600;
    u32 lightIdx = lightBase / 0x10;
    u32 startOffset = lightBase % 0x10;
    CHECK(lightIdx < 8, "XF: Light copy oob? Should never happen; lightIdx={}", lightIdx);
    CHECK(startOffset + len <= 0x10, "XF: Light copy that crosses across light boundaries unsupported: offs={}, len={}",
          startOffset, len);
    auto& light = g_gxState.lights[lightIdx];
    bool changed = false;
    for (u32 i = 0; i < len; i++) {
      u32 field = startOffset + i;
      u32 ival = read_bits<u32>(data + i * 4, e);
      switch (field) {
      case 3: { // Color (packed u32)
        const Vec4<float> color = unpack_color(ival);
        if (memcmp(&light.color, &color, sizeof(color)) != 0) {
          light.color = color;
          changed = true;
        }
        break;
      }
      case 4:
        changed |= store_xf_f32(light.cosAtt[0], ival);
        break; // a0
      case 5:
        changed |= store_xf_f32(light.cosAtt[1], ival);
        break; // a1
      case 6:
        changed |= store_xf_f32(light.cosAtt[2], ival);
        break; // a2
      case 7:
        changed |= store_xf_f32(light.distAtt[0], ival);
        break; // k0
      case 8:
        changed |= store_xf_f32(light.distAtt[1], ival);
        break; // k1
      case 9:
        changed |= store_xf_f32(light.distAtt[2], ival);
        break; // k2
      case 10:
        changed |= store_xf_f32(light.pos[0], ival);
        break; // px
      case 11:
        changed |= store_xf_f32(light.pos[1], ival);
        break; // py
      case 12:
        changed |= store_xf_f32(light.pos[2], ival);
        break; // pz
      case 13:
        changed |= store_xf_f32(light.dir[0], ival);
        break; // nx
      case 14:
        changed |= store_xf_f32(light.dir[1], ival);
        break; // ny
      case 15:
        changed |= store_xf_f32(light.dir[2], ival);
        break; // nz
      default:
        break; // padding (0-2)
      }
    }
    if (changed) {
      g_gxState.dirty |= DirtyUniform;
    }
    return true;
  }
  return false;
}

void handle_xf(u16 addr, std::span<const u8> data) noexcept {
  const u32 count = static_cast<u32>(data.size() / sizeof(u32));
  const u8* xfData = data.data();

  if (copy_xf_data(addr, xfData, count, std::endian::big)) {
    return;
  }
  if (addr < 0x1000) {
#ifndef NDEBUG
    Log.debug("Unhandled XF memory write 0x{:04X} ({} words)", addr, count);
#endif
    return;
  }

  // XF registers (0x1000+)
  const u32 xfAddr = addr - 0x1000;
  for (u32 i = 0; i < count; i++) {
    const u32 reg = xfAddr + i;
    const u8* wordData = xfData + i * 4;

    // Viewport mapping depends on the current render target
    if (reg == 0x1A && count - i >= 6) {
      xf_load_viewport(wordData);
      i += 5;
      continue;
    }
    // Projection depends only on its XF block, so identical loads can dedupe
    if (reg == 0x20 && count - i >= 7) {
      if (!xf_block_unchanged(0x20, wordData, 7)) {
        xf_load_projection(wordData);
      }
      i += 6;
      continue;
    }
    if (reg >= 0x1A && reg <= 0x26) {
      // Do not shadow unsupported partial blocks
      continue;
    }

    const u32 val = read_bits<u32>(wordData);
    if (reg >= XfRegCount) {
#ifndef NDEBUG
      Log.debug("Unhandled XF register 0x{:04X} (value 0x{:08X})", reg, val);
#endif
      continue;
    }
    if (xf_reg_unchanged(reg, val)) {
      continue;
    }
    const RegHandler& info = kXfRegs[reg];
    g_gxState.dirty |= info.dirty;
    if (info.decode != nullptr) {
      info.decode(static_cast<u8>(reg), val);
    }
  }
}

} // namespace aurora::gx::fifo

#include "common.hpp"

#include "../webgpu/gpu.hpp"
#include "gx.hpp"

#include <absl/container/flat_hash_map.h>

constexpr bool EnableNormalVisualization = false;
constexpr bool EnableDebugPrints = false;
constexpr bool UsePerPixelLighting = true;

namespace aurora::gfx::gx {
using namespace fmt::literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

static Module Log("aurora::gfx::gx");

absl::flat_hash_map<ShaderRef, std::pair<wgpu::ShaderModule, gx::ShaderInfo>> g_gxCachedShaders;
#ifndef NDEBUG
static absl::flat_hash_map<ShaderRef, gx::ShaderConfig> g_gxCachedShaderConfigs;
#endif

static inline std::string_view chan_comp(GXTevColorChan chan) noexcept {
  switch (chan) {
  case GX_CH_RED:
    return "r";
  case GX_CH_GREEN:
    return "g";
  case GX_CH_BLUE:
    return "b";
  case GX_CH_ALPHA:
    return "a";
  default:
    return "?";
  }
}

static void color_arg_reg_info(GXTevColorArg arg, const TevStage& stage, ShaderInfo& info) {
  switch (arg) {
  case GX_CC_CPREV:
  case GX_CC_APREV:
    if (!info.writesTevReg.test(GX_TEVPREV)) {
      info.loadsTevReg.set(GX_TEVPREV);
    }
    break;
  case GX_CC_C0:
  case GX_CC_A0:
    if (!info.writesTevReg.test(GX_TEVREG0)) {
      info.loadsTevReg.set(GX_TEVREG0);
    }
    break;
  case GX_CC_C1:
  case GX_CC_A1:
    if (!info.writesTevReg.test(GX_TEVREG1)) {
      info.loadsTevReg.set(GX_TEVREG1);
    }
    break;
  case GX_CC_C2:
  case GX_CC_A2:
    if (!info.writesTevReg.test(GX_TEVREG2)) {
      info.loadsTevReg.set(GX_TEVREG2);
    }
    break;
  case GX_CC_TEXC:
  case GX_CC_TEXA:
    CHECK(stage.texCoordId != GX_TEXCOORD_NULL, "tex coord not bound");
    CHECK(stage.texMapId != GX_TEXMAP_NULL, "tex map not bound");
    info.sampledTexCoords.set(stage.texCoordId);
    info.sampledTextures.set(stage.texMapId);
    break;
  case GX_CC_RASC:
  case GX_CC_RASA:
    if (stage.channelId >= GX_COLOR0A0 && stage.channelId <= GX_COLOR1A1) {
      info.sampledColorChannels.set(stage.channelId - GX_COLOR0A0);
    }
    break;
  case GX_CC_KONST:
    switch (stage.kcSel) {
    case GX_TEV_KCSEL_K0:
    case GX_TEV_KCSEL_K0_R:
    case GX_TEV_KCSEL_K0_G:
    case GX_TEV_KCSEL_K0_B:
    case GX_TEV_KCSEL_K0_A:
      info.sampledKColors.set(0);
      break;
    case GX_TEV_KCSEL_K1:
    case GX_TEV_KCSEL_K1_R:
    case GX_TEV_KCSEL_K1_G:
    case GX_TEV_KCSEL_K1_B:
    case GX_TEV_KCSEL_K1_A:
      info.sampledKColors.set(1);
      break;
    case GX_TEV_KCSEL_K2:
    case GX_TEV_KCSEL_K2_R:
    case GX_TEV_KCSEL_K2_G:
    case GX_TEV_KCSEL_K2_B:
    case GX_TEV_KCSEL_K2_A:
      info.sampledKColors.set(2);
      break;
    case GX_TEV_KCSEL_K3:
    case GX_TEV_KCSEL_K3_R:
    case GX_TEV_KCSEL_K3_G:
    case GX_TEV_KCSEL_K3_B:
    case GX_TEV_KCSEL_K3_A:
      info.sampledKColors.set(3);
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

static bool formatHasAlpha(u32 format) {
  switch (format) {
  case GX_TF_IA4:
  case GX_TF_IA8:
  case GX_TF_RGB5A3:
  case GX_TF_RGBA8:
  case GX_TF_CMPR:
  case GX_CTF_RA4:
  case GX_CTF_RA8:
  case GX_CTF_YUVA8:
  case GX_CTF_A8:
  case GX_TF_RGBA8_PC:
    return true;
  default:
    return false;
  }
}

static std::string color_arg_reg(GXTevColorArg arg, size_t stageIdx, const ShaderConfig& config,
                                 const TevStage& stage) {
  switch (arg) {
    DEFAULT_FATAL("invalid color arg {}", static_cast<int>(arg));
  case GX_CC_CPREV:
    return "prev.rgb";
  case GX_CC_APREV:
    return "vec3<f32>(prev.a)";
  case GX_CC_C0:
    return "tevreg0.rgb";
  case GX_CC_A0:
    return "vec3<f32>(tevreg0.a)";
  case GX_CC_C1:
    return "tevreg1.rgb";
  case GX_CC_A1:
    return "vec3<f32>(tevreg1.a)";
  case GX_CC_C2:
    return "tevreg2.rgb";
  case GX_CC_A2:
    return "vec3<f32>(tevreg2.a)";
  case GX_CC_TEXC: {
    CHECK(stage.texMapId != GX_TEXMAP_NULL, "unmapped texture for stage {}", stageIdx);
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          static_cast<int>(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format(FMT_STRING("sampled{}.{}{}{}"), stageIdx, chan_comp(swap.red), chan_comp(swap.green),
                       chan_comp(swap.blue));
  }
  case GX_CC_TEXA: {
    CHECK(stage.texMapId != GX_TEXMAP_NULL, "unmapped texture for stage {}", stageIdx);
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          static_cast<int>(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format(FMT_STRING("vec3<f32>(sampled{}.{})"), stageIdx, chan_comp(swap.alpha));
  }
  case GX_CC_RASC: {
    CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO) {
      return "vec3<f32>(0.0)";
    }
    CHECK(stage.channelId >= GX_COLOR0A0 && stage.channelId <= GX_COLOR1A1, "invalid color channel {} for stage {}",
          static_cast<int>(stage.channelId), stageIdx);
    u32 idx = stage.channelId - GX_COLOR0A0;
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format(FMT_STRING("rast{}.{}{}{}"), idx, chan_comp(swap.red), chan_comp(swap.green),
                       chan_comp(swap.blue));
  }
  case GX_CC_RASA: {
    CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO) {
      return "vec3<f32>(0.0)";
    }
    CHECK(stage.channelId >= GX_COLOR0A0 && stage.channelId <= GX_COLOR1A1, "invalid color channel {} for stage {}",
          static_cast<int>(stage.channelId), stageIdx);
    u32 idx = stage.channelId - GX_COLOR0A0;
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format(FMT_STRING("vec3<f32>(rast{}.{})"), idx, chan_comp(swap.alpha));
  }
  case GX_CC_ONE:
    return "vec3<f32>(1.0)";
  case GX_CC_HALF:
    return "vec3<f32>(0.5)";
  case GX_CC_KONST: {
    switch (stage.kcSel) {
      DEFAULT_FATAL("invalid kcSel {}", static_cast<int>(stage.kcSel));
    case GX_TEV_KCSEL_8_8:
      return "vec3<f32>(1.0)";
    case GX_TEV_KCSEL_7_8:
      return "vec3<f32>(7.0/8.0)";
    case GX_TEV_KCSEL_6_8:
      return "vec3<f32>(6.0/8.0)";
    case GX_TEV_KCSEL_5_8:
      return "vec3<f32>(5.0/8.0)";
    case GX_TEV_KCSEL_4_8:
      return "vec3<f32>(4.0/8.0)";
    case GX_TEV_KCSEL_3_8:
      return "vec3<f32>(3.0/8.0)";
    case GX_TEV_KCSEL_2_8:
      return "vec3<f32>(2.0/8.0)";
    case GX_TEV_KCSEL_1_8:
      return "vec3<f32>(1.0/8.0)";
    case GX_TEV_KCSEL_K0:
      return "ubuf.kcolor0.rgb";
    case GX_TEV_KCSEL_K1:
      return "ubuf.kcolor1.rgb";
    case GX_TEV_KCSEL_K2:
      return "ubuf.kcolor2.rgb";
    case GX_TEV_KCSEL_K3:
      return "ubuf.kcolor3.rgb";
    case GX_TEV_KCSEL_K0_R:
      return "vec3<f32>(ubuf.kcolor0.r)";
    case GX_TEV_KCSEL_K1_R:
      return "vec3<f32>(ubuf.kcolor1.r)";
    case GX_TEV_KCSEL_K2_R:
      return "vec3<f32>(ubuf.kcolor2.r)";
    case GX_TEV_KCSEL_K3_R:
      return "vec3<f32>(ubuf.kcolor3.r)";
    case GX_TEV_KCSEL_K0_G:
      return "vec3<f32>(ubuf.kcolor0.g)";
    case GX_TEV_KCSEL_K1_G:
      return "vec3<f32>(ubuf.kcolor1.g)";
    case GX_TEV_KCSEL_K2_G:
      return "vec3<f32>(ubuf.kcolor2.g)";
    case GX_TEV_KCSEL_K3_G:
      return "vec3<f32>(ubuf.kcolor3.g)";
    case GX_TEV_KCSEL_K0_B:
      return "vec3<f32>(ubuf.kcolor0.b)";
    case GX_TEV_KCSEL_K1_B:
      return "vec3<f32>(ubuf.kcolor1.b)";
    case GX_TEV_KCSEL_K2_B:
      return "vec3<f32>(ubuf.kcolor2.b)";
    case GX_TEV_KCSEL_K3_B:
      return "vec3<f32>(ubuf.kcolor3.b)";
    case GX_TEV_KCSEL_K0_A:
      return "vec3<f32>(ubuf.kcolor0.a)";
    case GX_TEV_KCSEL_K1_A:
      return "vec3<f32>(ubuf.kcolor1.a)";
    case GX_TEV_KCSEL_K2_A:
      return "vec3<f32>(ubuf.kcolor2.a)";
    case GX_TEV_KCSEL_K3_A:
      return "vec3<f32>(ubuf.kcolor3.a)";
    }
  }
  case GX_CC_ZERO:
    return "vec3<f32>(0.0)";
  }
}

static void alpha_arg_reg_info(GXTevAlphaArg arg, const TevStage& stage, ShaderInfo& info) {
  switch (arg) {
  case GX_CA_APREV:
    if (!info.writesTevReg.test(GX_TEVPREV)) {
      info.loadsTevReg.set(GX_TEVPREV);
    }
    break;
  case GX_CA_A0:
    if (!info.writesTevReg.test(GX_TEVREG0)) {
      info.loadsTevReg.set(GX_TEVREG0);
    }
    break;
  case GX_CA_A1:
    if (!info.writesTevReg.test(GX_TEVREG1)) {
      info.loadsTevReg.set(GX_TEVREG1);
    }
    break;
  case GX_CA_A2:
    if (!info.writesTevReg.test(GX_TEVREG2)) {
      info.loadsTevReg.set(GX_TEVREG2);
    }
    break;
  case GX_CA_TEXA:
    CHECK(stage.texCoordId != GX_TEXCOORD_NULL, "tex coord not bound");
    CHECK(stage.texMapId != GX_TEXMAP_NULL, "tex map not bound");
    info.sampledTexCoords.set(stage.texCoordId);
    info.sampledTextures.set(stage.texMapId);
    break;
  case GX_CA_RASA:
    if (stage.channelId >= GX_COLOR0A0 && stage.channelId <= GX_COLOR1A1) {
      info.sampledColorChannels.set(stage.channelId - GX_COLOR0A0);
    }
    break;
  case GX_CA_KONST:
    switch (stage.kaSel) {
    case GX_TEV_KASEL_K0_R:
    case GX_TEV_KASEL_K0_G:
    case GX_TEV_KASEL_K0_B:
    case GX_TEV_KASEL_K0_A:
      info.sampledKColors.set(0);
      break;
    case GX_TEV_KASEL_K1_R:
    case GX_TEV_KASEL_K1_G:
    case GX_TEV_KASEL_K1_B:
    case GX_TEV_KASEL_K1_A:
      info.sampledKColors.set(1);
      break;
    case GX_TEV_KASEL_K2_R:
    case GX_TEV_KASEL_K2_G:
    case GX_TEV_KASEL_K2_B:
    case GX_TEV_KASEL_K2_A:
      info.sampledKColors.set(2);
      break;
    case GX_TEV_KASEL_K3_R:
    case GX_TEV_KASEL_K3_G:
    case GX_TEV_KASEL_K3_B:
    case GX_TEV_KASEL_K3_A:
      info.sampledKColors.set(3);
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

static std::string alpha_arg_reg(GXTevAlphaArg arg, size_t stageIdx, const ShaderConfig& config,
                                 const TevStage& stage) {
  switch (arg) {
    DEFAULT_FATAL("invalid alpha arg {}", static_cast<int>(arg));
  case GX_CA_APREV:
    return "prev.a";
  case GX_CA_A0:
    return "tevreg0.a";
  case GX_CA_A1:
    return "tevreg1.a";
  case GX_CA_A2:
    return "tevreg2.a";
  case GX_CA_TEXA: {
    CHECK(stage.texMapId != GX_TEXMAP_NULL, "unmapped texture for stage {}", stageIdx);
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          static_cast<int>(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format(FMT_STRING("sampled{}.{}"), stageIdx, chan_comp(swap.alpha));
  }
  case GX_CA_RASA: {
    CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO) {
      return "0.0";
    }
    CHECK(stage.channelId >= GX_COLOR0A0 && stage.channelId <= GX_COLOR1A1, "invalid color channel {} for stage {}",
          static_cast<int>(stage.channelId), stageIdx);
    u32 idx = stage.channelId - GX_COLOR0A0;
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format(FMT_STRING("rast{}.{}"), idx, chan_comp(swap.alpha));
  }
  case GX_CA_KONST: {
    switch (stage.kaSel) {
      DEFAULT_FATAL("invalid kaSel {}", static_cast<int>(stage.kaSel));
    case GX_TEV_KASEL_8_8:
      return "1.0";
    case GX_TEV_KASEL_7_8:
      return "(7.0/8.0)";
    case GX_TEV_KASEL_6_8:
      return "(6.0/8.0)";
    case GX_TEV_KASEL_5_8:
      return "(5.0/8.0)";
    case GX_TEV_KASEL_4_8:
      return "(4.0/8.0)";
    case GX_TEV_KASEL_3_8:
      return "(3.0/8.0)";
    case GX_TEV_KASEL_2_8:
      return "(2.0/8.0)";
    case GX_TEV_KASEL_1_8:
      return "(1.0/8.0)";
    case GX_TEV_KASEL_K0_R:
      return "ubuf.kcolor0.r";
    case GX_TEV_KASEL_K1_R:
      return "ubuf.kcolor1.r";
    case GX_TEV_KASEL_K2_R:
      return "ubuf.kcolor2.r";
    case GX_TEV_KASEL_K3_R:
      return "ubuf.kcolor3.r";
    case GX_TEV_KASEL_K0_G:
      return "ubuf.kcolor0.g";
    case GX_TEV_KASEL_K1_G:
      return "ubuf.kcolor1.g";
    case GX_TEV_KASEL_K2_G:
      return "ubuf.kcolor2.g";
    case GX_TEV_KASEL_K3_G:
      return "ubuf.kcolor3.g";
    case GX_TEV_KASEL_K0_B:
      return "ubuf.kcolor0.b";
    case GX_TEV_KASEL_K1_B:
      return "ubuf.kcolor1.b";
    case GX_TEV_KASEL_K2_B:
      return "ubuf.kcolor2.b";
    case GX_TEV_KASEL_K3_B:
      return "ubuf.kcolor3.b";
    case GX_TEV_KASEL_K0_A:
      return "ubuf.kcolor0.a";
    case GX_TEV_KASEL_K1_A:
      return "ubuf.kcolor1.a";
    case GX_TEV_KASEL_K2_A:
      return "ubuf.kcolor2.a";
    case GX_TEV_KASEL_K3_A:
      return "ubuf.kcolor3.a";
    }
  }
  case GX_CA_ZERO:
    return "0.0";
  }
}

static std::string_view tev_op(GXTevOp op) {
  switch (op) {
    DEFAULT_FATAL("unimplemented tev op {}", static_cast<int>(op));
  case GX_TEV_ADD:
    return ""sv;
  case GX_TEV_SUB:
    return "-"sv;
  }
}

static std::string_view tev_bias(GXTevBias bias) {
  switch (bias) {
    DEFAULT_FATAL("invalid tev bias {}", static_cast<int>(bias));
  case GX_TB_ZERO:
    return ""sv;
  case GX_TB_ADDHALF:
    return " + 0.5"sv;
  case GX_TB_SUBHALF:
    return " - 0.5"sv;
  }
}

static std::string alpha_compare(GXCompare comp, u8 ref, bool& valid) {
  const float fref = ref / 255.f;
  switch (comp) {
    DEFAULT_FATAL("invalid alpha comp {}", static_cast<int>(comp));
  case GX_NEVER:
    return "false"s;
  case GX_LESS:
    return fmt::format(FMT_STRING("(prev.a < {}f)"), fref);
  case GX_LEQUAL:
    return fmt::format(FMT_STRING("(prev.a <= {}f)"), fref);
  case GX_EQUAL:
    return fmt::format(FMT_STRING("(prev.a == {}f)"), fref);
  case GX_NEQUAL:
    return fmt::format(FMT_STRING("(prev.a != {}f)"), fref);
  case GX_GEQUAL:
    return fmt::format(FMT_STRING("(prev.a >= {}f)"), fref);
  case GX_GREATER:
    return fmt::format(FMT_STRING("(prev.a > {}f)"), fref);
  case GX_ALWAYS:
    valid = false;
    return "true"s;
  }
}

static std::string_view tev_scale(GXTevScale scale) {
  switch (scale) {
    DEFAULT_FATAL("invalid tev scale {}", static_cast<int>(scale));
  case GX_CS_SCALE_1:
    return ""sv;
  case GX_CS_SCALE_2:
    return " * 2.0"sv;
  case GX_CS_SCALE_4:
    return " * 4.0"sv;
  case GX_CS_DIVIDE_2:
    return " / 2.0"sv;
  }
}

static inline std::string vtx_attr(const ShaderConfig& config, GXAttr attr) {
  const auto type = config.vtxAttrs[attr];
  if (type == GX_NONE) {
    if (attr == GX_VA_NRM) {
      // Default normal
      return "vec3<f32>(1.0, 0.0, 0.0)"s;
    }
    UNLIKELY FATAL("unmapped vtx attr {}", static_cast<int>(attr));
  }
  if (attr == GX_VA_POS) {
    return "in_pos"s;
  }
  if (attr == GX_VA_NRM) {
    return "in_nrm"s;
  }
  if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
    const auto idx = attr - GX_VA_CLR0;
    return fmt::format(FMT_STRING("in_clr{}"), idx);
  }
  if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
    const auto idx = attr - GX_VA_TEX0;
    return fmt::format(FMT_STRING("in_tex{}_uv"), idx);
  }
  UNLIKELY FATAL("unhandled vtx attr {}", static_cast<int>(attr));
}

static inline std::string texture_conversion(const TextureConfig& tex, u32 stageIdx, u32 texMapId) {
  std::string out;
  if (tex.renderTex)
    switch (tex.copyFmt) {
    default:
      break;
    case GX_TF_RGB565:
      // Set alpha channel to 1.0
      out += fmt::format(FMT_STRING("\n    sampled{0}.a = 1.0;"), stageIdx);
      break;
    case GX_TF_I4:
    case GX_TF_I8:
      // FIXME HACK
      if (!is_palette_format(tex.loadFmt)) {
        // Perform intensity conversion
        out += fmt::format(FMT_STRING("\n    sampled{0} = vec4<f32>(intensityF32(sampled{0}.rgb), 0.f, 0.f, 1.f);"),
                           stageIdx);
      }
      break;
    }
  switch (tex.loadFmt) {
  default:
    break;
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_R8_PC:
    // Splat R to RGBA
    out += fmt::format(FMT_STRING("\n    sampled{0} = vec4<f32>(sampled{0}.r);"), stageIdx);
    break;
  }
  return out;
}

constexpr std::array<std::string_view, GX_CC_ZERO + 1> TevColorArgNames{
    "CPREV"sv, "APREV"sv, "C0"sv,   "A0"sv,   "C1"sv,  "A1"sv,   "C2"sv,    "A2"sv,
    "TEXC"sv,  "TEXA"sv,  "RASC"sv, "RASA"sv, "ONE"sv, "HALF"sv, "KONST"sv, "ZERO"sv,
};
constexpr std::array<std::string_view, GX_CA_ZERO + 1> TevAlphaArgNames{
    "APREV"sv, "A0"sv, "A1"sv, "A2"sv, "TEXA"sv, "RASA"sv, "KONST"sv, "ZERO"sv,
};

constexpr std::array<std::string_view, MaxVtxAttr> VtxAttributeNames{
    "pn_mtx",        "tex0_mtx",      "tex1_mtx",      "tex2_mtx",    "tex3_mtx", "tex4_mtx", "tex5_mtx",
    "tex6_mtx",      "tex7_mtx",      "pos",           "nrm",         "clr0",     "clr1",     "tex0_uv",
    "tex1_uv",       "tex2_uv",       "tex3_uv",       "tex4_uv",     "tex5_uv",  "tex6_uv",  "tex7_uv",
    "pos_mtx_array", "nrm_mtx_array", "tex_mtx_array", "light_array", "nbt",
};

ShaderInfo build_shader_info(const ShaderConfig& config) noexcept {
  //  const auto hash = xxh3_hash(config);
  //  const auto it = g_gxCachedShaders.find(hash);
  //  if (it != g_gxCachedShaders.end()) {
  //    return it->second.second;
  //  }

  ShaderInfo info{
      .uniformSize = 64 * 3, // mv, mvInv, proj
  };
  for (int i = 0; i < config.tevStageCount; ++i) {
    const auto& stage = config.tevStages[i];
    // Color pass
    color_arg_reg_info(stage.colorPass.a, stage, info);
    color_arg_reg_info(stage.colorPass.b, stage, info);
    color_arg_reg_info(stage.colorPass.c, stage, info);
    color_arg_reg_info(stage.colorPass.d, stage, info);
    info.writesTevReg.set(stage.colorOp.outReg);

    // Alpha pass
    alpha_arg_reg_info(stage.alphaPass.a, stage, info);
    alpha_arg_reg_info(stage.alphaPass.b, stage, info);
    alpha_arg_reg_info(stage.alphaPass.c, stage, info);
    alpha_arg_reg_info(stage.alphaPass.d, stage, info);
    if (!info.writesTevReg.test(stage.alphaOp.outReg)) {
      // If we're writing alpha to a register that's not been
      // written to in the shader, load from uniform buffer
      info.loadsTevReg.set(stage.alphaOp.outReg);
      info.writesTevReg.set(stage.alphaOp.outReg);
    }
  }
  info.uniformSize += info.loadsTevReg.count() * 16;
  bool lightingEnabled = false;
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (info.sampledColorChannels.test(i)) {
      const auto& cc = config.colorChannels[i * 2];
      const auto& cca = config.colorChannels[i * 2 + 1];
      if (cc.lightingEnabled || cca.lightingEnabled) {
        lightingEnabled = true;
      }
    }
  }
  if (lightingEnabled) {
    // Lights + light state for all channels
    info.uniformSize += 16 + (80 * GX::MaxLights);
  }
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (info.sampledColorChannels.test(i)) {
      const auto& cc = config.colorChannels[i * 2];
      if (cc.lightingEnabled && cc.ambSrc == GX_SRC_REG) {
        info.uniformSize += 16;
      }
      if (cc.matSrc == GX_SRC_REG) {
        info.uniformSize += 16;
      }
      const auto& cca = config.colorChannels[i * 2 + 1];
      if (cca.lightingEnabled && cca.ambSrc == GX_SRC_REG) {
        info.uniformSize += 16;
      }
      if (cca.matSrc == GX_SRC_REG) {
        info.uniformSize += 16;
      }
    }
  }
  info.uniformSize += info.sampledKColors.count() * 16;
  for (int i = 0; i < info.sampledTexCoords.size(); ++i) {
    if (!info.sampledTexCoords.test(i)) {
      continue;
    }
    const auto& tcg = config.tcgs[i];
    if (tcg.mtx != GX_IDENTITY) {
      u32 texMtxIdx = (tcg.mtx - GX_TEXMTX0) / 3;
      info.usesTexMtx.set(texMtxIdx);
      info.texMtxTypes[texMtxIdx] = tcg.type;
    }
    if (tcg.postMtx != GX_PTIDENTITY) {
      u32 postMtxIdx = (tcg.postMtx - GX_PTTEXMTX0) / 3;
      info.usesPTTexMtx.set(postMtxIdx);
    }
  }
  for (int i = 0; i < info.usesTexMtx.size(); ++i) {
    if (info.usesTexMtx.test(i)) {
      switch (info.texMtxTypes[i]) {
      case GX_TG_MTX2x4:
        info.uniformSize += 32;
        break;
      case GX_TG_MTX3x4:
        info.uniformSize += 64;
        break;
      default:
        break;
      }
    }
  }
  info.uniformSize += info.usesPTTexMtx.count() * 64;
  if (config.fogType != GX_FOG_NONE) {
    info.usesFog = true;
    info.uniformSize += 32;
  }
  info.uniformSize += info.sampledTextures.count() * 4;
  info.uniformSize = align_uniform(info.uniformSize);
  return info;
}

wgpu::ShaderModule build_shader(const ShaderConfig& config, const ShaderInfo& info) noexcept {
  const auto hash = xxh3_hash(config);
  const auto it = g_gxCachedShaders.find(hash);
  if (it != g_gxCachedShaders.end()) {
    CHECK(g_gxCachedShaderConfigs[hash] == config, "Shader collision! {:x}", hash);
    return it->second.first;
  }

  if (EnableDebugPrints) {
    Log.report(LOG_INFO, FMT_STRING("Shader config (hash {:x}):"), hash);
    {
      for (int i = 0; i < config.tevStageCount; ++i) {
        const auto& stage = config.tevStages[i];
        Log.report(LOG_INFO, FMT_STRING("  tevStages[{}]:"), i);
        Log.report(LOG_INFO, FMT_STRING("    color_a: {}"), TevColorArgNames[stage.colorPass.a]);
        Log.report(LOG_INFO, FMT_STRING("    color_b: {}"), TevColorArgNames[stage.colorPass.b]);
        Log.report(LOG_INFO, FMT_STRING("    color_c: {}"), TevColorArgNames[stage.colorPass.c]);
        Log.report(LOG_INFO, FMT_STRING("    color_d: {}"), TevColorArgNames[stage.colorPass.d]);
        Log.report(LOG_INFO, FMT_STRING("    alpha_a: {}"), TevAlphaArgNames[stage.alphaPass.a]);
        Log.report(LOG_INFO, FMT_STRING("    alpha_b: {}"), TevAlphaArgNames[stage.alphaPass.b]);
        Log.report(LOG_INFO, FMT_STRING("    alpha_c: {}"), TevAlphaArgNames[stage.alphaPass.c]);
        Log.report(LOG_INFO, FMT_STRING("    alpha_d: {}"), TevAlphaArgNames[stage.alphaPass.d]);
        Log.report(LOG_INFO, FMT_STRING("    color_op_clamp: {}"), stage.colorOp.clamp);
        Log.report(LOG_INFO, FMT_STRING("    color_op_op: {}"), stage.colorOp.op);
        Log.report(LOG_INFO, FMT_STRING("    color_op_bias: {}"), stage.colorOp.bias);
        Log.report(LOG_INFO, FMT_STRING("    color_op_scale: {}"), stage.colorOp.scale);
        Log.report(LOG_INFO, FMT_STRING("    color_op_reg_id: {}"), stage.colorOp.outReg);
        Log.report(LOG_INFO, FMT_STRING("    alpha_op_clamp: {}"), stage.alphaOp.clamp);
        Log.report(LOG_INFO, FMT_STRING("    alpha_op_op: {}"), stage.alphaOp.op);
        Log.report(LOG_INFO, FMT_STRING("    alpha_op_bias: {}"), stage.alphaOp.bias);
        Log.report(LOG_INFO, FMT_STRING("    alpha_op_scale: {}"), stage.alphaOp.scale);
        Log.report(LOG_INFO, FMT_STRING("    alpha_op_reg_id: {}"), stage.alphaOp.outReg);
        Log.report(LOG_INFO, FMT_STRING("    kc_sel: {}"), stage.kcSel);
        Log.report(LOG_INFO, FMT_STRING("    ka_sel: {}"), stage.kaSel);
        Log.report(LOG_INFO, FMT_STRING("    texCoordId: {}"), stage.texCoordId);
        Log.report(LOG_INFO, FMT_STRING("    texMapId: {}"), stage.texMapId);
        Log.report(LOG_INFO, FMT_STRING("    channelId: {}"), stage.channelId);
      }
      for (int i = 0; i < config.colorChannels.size(); ++i) {
        const auto& chan = config.colorChannels[i];
        Log.report(LOG_INFO, FMT_STRING("  colorChannels[{}]: enabled {} mat {} amb {}"), i, chan.lightingEnabled,
                   chan.matSrc, chan.ambSrc);
      }
      for (int i = 0; i < config.tcgs.size(); ++i) {
        const auto& tcg = config.tcgs[i];
        if (tcg.src != GX_MAX_TEXGENSRC) {
          Log.report(LOG_INFO, FMT_STRING("  tcg[{}]: src {} mtx {} post {} type {} norm {}"), i, tcg.src, tcg.mtx,
                     tcg.postMtx, tcg.type, tcg.normalize);
        }
      }
      Log.report(LOG_INFO, FMT_STRING("  alphaCompare: comp0 {} ref0 {} op {} comp1 {} ref1 {}"),
                 config.alphaCompare.comp0, config.alphaCompare.ref0, config.alphaCompare.op, config.alphaCompare.comp1,
                 config.alphaCompare.ref1);
      Log.report(LOG_INFO, FMT_STRING("  indexedAttributeCount: {}"), config.indexedAttributeCount);
      Log.report(LOG_INFO, FMT_STRING("  fogType: {}"), config.fogType);
    }
  }

  std::string uniformPre;
  std::string uniBufAttrs;
  std::string uniformBindings;
  std::string sampBindings;
  std::string texBindings;
  std::string vtxOutAttrs;
  std::string vtxInAttrs;
  std::string vtxXfrAttrsPre;
  std::string vtxXfrAttrs;
  size_t locIdx = 0;
  size_t vtxOutIdx = 0;
  size_t uniBindingIdx = 1;
  if (config.indexedAttributeCount > 0) {
    // Display list attributes
    int currAttrIdx = 0;
    for (GXAttr attr{}; attr < MaxVtxAttr; attr = GXAttr(attr + 1)) {
      // Indexed attributes
      if (config.vtxAttrs[attr] != GX_INDEX8 && config.vtxAttrs[attr] != GX_INDEX16) {
        continue;
      }
      const auto [div, rem] = std::div(currAttrIdx, 4);
      std::string_view attrName;
      bool addUniformBinding = true;
      if (config.attrMapping[attr] != attr) {
        attrName = VtxAttributeNames[config.attrMapping[attr]];
        addUniformBinding = false;
      } else {
        attrName = VtxAttributeNames[attr];
      }
      vtxXfrAttrsPre +=
          fmt::format(FMT_STRING("\n    var {} = v_arr_{}[in_dl{}[{}]];"), vtx_attr(config, attr), attrName, div, rem);
      if (addUniformBinding) {
        std::string_view arrType;
        if (attr == GX_VA_POS || attr == GX_VA_NRM) {
          arrType = "vec3<f32>";
        } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
          arrType = "vec2<f32>";
        }
        uniformBindings += fmt::format(FMT_STRING("\n@group(0) @binding({})"
                                                  "\nvar<storage, read> v_arr_{}: array<{}>;"),
                                       uniBindingIdx++, attrName, arrType);
      }
      ++currAttrIdx;
    }
    auto [num4xAttrArrays, rem] = std::div(currAttrIdx, 4);
    u32 num2xAttrArrays = 0;
    if (rem > 2) {
      ++num4xAttrArrays;
    } else if (rem > 0) {
      num2xAttrArrays = 1;
    }
    for (u32 i = 0; i < num4xAttrArrays; ++i) {
      if (locIdx > 0) {
        vtxInAttrs += "\n    , ";
      } else {
        vtxInAttrs += "\n    ";
      }
      vtxInAttrs += fmt::format(FMT_STRING("@location({}) in_dl{}: vec4<i32>"), locIdx++, i);
    }
    for (u32 i = 0; i < num2xAttrArrays; ++i) {
      if (locIdx > 0) {
        vtxInAttrs += "\n    , ";
      } else {
        vtxInAttrs += "\n    ";
      }
      vtxInAttrs += fmt::format(FMT_STRING("@location({}) in_dl{}: vec2<i32>"), locIdx++, num4xAttrArrays + i);
    }
  }
  for (GXAttr attr{}; attr < MaxVtxAttr; attr = GXAttr(attr + 1)) {
    // Direct attributes
    if (config.vtxAttrs[attr] != GX_DIRECT) {
      continue;
    }
    if (locIdx > 0) {
      vtxInAttrs += "\n    , ";
    } else {
      vtxInAttrs += "\n    ";
    }
    if (attr == GX_VA_POS) {
      vtxInAttrs += fmt::format(FMT_STRING("@location({}) in_pos: vec3<f32>"), locIdx++);
    } else if (attr == GX_VA_NRM) {
      vtxInAttrs += fmt::format(FMT_STRING("@location({}) in_nrm: vec3<f32>"), locIdx++);
    } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
      vtxInAttrs += fmt::format(FMT_STRING("@location({}) in_clr{}: vec4<f32>"), locIdx++, attr - GX_VA_CLR0);
    } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
      vtxInAttrs += fmt::format(FMT_STRING("@location({}) in_tex{}_uv: vec2<f32>"), locIdx++, attr - GX_VA_TEX0);
    }
  }
  vtxXfrAttrsPre += fmt::format(FMT_STRING("\n    var mv_pos = mul4x3(ubuf.pos_mtx, vec4<f32>({}, 1.0));"
                                           "\n    var mv_nrm = normalize(mul4x3(ubuf.nrm_mtx, vec4<f32>({}, 0.0)));"
                                           "\n    out.pos = mul4x4(ubuf.proj, vec4<f32>(mv_pos, 1.0));"),
                                vtx_attr(config, GX_VA_POS), vtx_attr(config, GX_VA_NRM));
  if constexpr (EnableNormalVisualization) {
    vtxOutAttrs += fmt::format(FMT_STRING("\n    @location({}) nrm: vec3<f32>,"), vtxOutIdx++);
    vtxXfrAttrsPre += "\n    out.nrm = mv_nrm;";
  }

  std::string fragmentFnPre;
  std::string fragmentFn;
  for (u32 idx = 0; idx < config.tevStageCount; ++idx) {
    const auto& stage = config.tevStages[idx];
    {
      std::string outReg;
      switch (stage.colorOp.outReg) {
        DEFAULT_FATAL("invalid colorOp outReg {}", static_cast<int>(stage.colorOp.outReg));
      case GX_TEVPREV:
        outReg = "prev";
        break;
      case GX_TEVREG0:
        outReg = "tevreg0";
        break;
      case GX_TEVREG1:
        outReg = "tevreg1";
        break;
      case GX_TEVREG2:
        outReg = "tevreg2";
        break;
      }
      std::string op = fmt::format(
          FMT_STRING("(({4}mix({0}, {1}, {2}) + {3}){5}){6}"), color_arg_reg(stage.colorPass.a, idx, config, stage),
          color_arg_reg(stage.colorPass.b, idx, config, stage), color_arg_reg(stage.colorPass.c, idx, config, stage),
          color_arg_reg(stage.colorPass.d, idx, config, stage), tev_op(stage.colorOp.op), tev_bias(stage.colorOp.bias),
          tev_scale(stage.colorOp.scale));
      if (stage.colorOp.clamp) {
        op = fmt::format(FMT_STRING("clamp({}, vec3<f32>(0.0), vec3<f32>(1.0))"), op);
      }
      fragmentFn +=
          fmt::format(FMT_STRING("\n    // TEV stage {2}\n    {0} = vec4<f32>({1}, {0}.a);"), outReg, op, idx);
    }
    {
      std::string outReg;
      switch (stage.alphaOp.outReg) {
        DEFAULT_FATAL("invalid alphaOp outReg {}", static_cast<int>(stage.alphaOp.outReg));
      case GX_TEVPREV:
        outReg = "prev.a";
        break;
      case GX_TEVREG0:
        outReg = "tevreg0.a";
        break;
      case GX_TEVREG1:
        outReg = "tevreg1.a";
        break;
      case GX_TEVREG2:
        outReg = "tevreg2.a";
        break;
      }
      std::string op = fmt::format(
          FMT_STRING("(({4}mix({0}, {1}, {2}) + {3}){5}){6}"), alpha_arg_reg(stage.alphaPass.a, idx, config, stage),
          alpha_arg_reg(stage.alphaPass.b, idx, config, stage), alpha_arg_reg(stage.alphaPass.c, idx, config, stage),
          alpha_arg_reg(stage.alphaPass.d, idx, config, stage), tev_op(stage.alphaOp.op), tev_bias(stage.alphaOp.bias),
          tev_scale(stage.alphaOp.scale));
      if (stage.alphaOp.clamp) {
        op = fmt::format(FMT_STRING("clamp({}, 0.0, 1.0)"), op);
      }
      fragmentFn += fmt::format(FMT_STRING("\n    {0} = {1};"), outReg, op);
    }
  }
  if (info.loadsTevReg.test(0)) {
    uniBufAttrs += "\n    tevprev: vec4<f32>,";
    fragmentFnPre += "\n    var prev = ubuf.tevprev;";
  } else {
    fragmentFnPre += "\n    var prev: vec4<f32>;";
  }
  for (int i = 1 /* Skip TEVPREV */; i < info.loadsTevReg.size(); ++i) {
    if (info.loadsTevReg.test(i)) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    tevreg{}: vec4<f32>,"), i - 1);
      fragmentFnPre += fmt::format(FMT_STRING("\n    var tevreg{0} = ubuf.tevreg{0};"), i - 1);
    } else if (info.writesTevReg.test(i)) {
      fragmentFnPre += fmt::format(FMT_STRING("\n    var tevreg{0}: vec4<f32>;"), i - 1);
    }
  }
  bool addedLightStruct = false;
  int vtxColorIdx = 0;
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (!info.sampledColorChannels.test(i)) {
      continue;
    }
    const auto& cc = config.colorChannels[i * 2];
    const auto& cca = config.colorChannels[i * 2 + 1];

    if (!addedLightStruct && (cc.lightingEnabled || cca.lightingEnabled)) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    lights: array<Light, {}>,"
                                            "\n    lightState0: u32,"
                                            "\n    lightState0a: u32,"
                                            "\n    lightState1: u32,"
                                            "\n    lightState1a: u32,"),
                                 GX::MaxLights);
      uniformPre +=
          "\n"
          "struct Light {\n"
          "    pos: vec3<f32>,\n"
          "    dir: vec3<f32>,\n"
          "    color: vec4<f32>,\n"
          "    cos_att: vec3<f32>,\n"
          "    dist_att: vec3<f32>,\n"
          "};";
      if (UsePerPixelLighting) {
        vtxOutAttrs += fmt::format(FMT_STRING("\n    @location({}) mv_pos: vec3<f32>,"), vtxOutIdx++);
        vtxOutAttrs += fmt::format(FMT_STRING("\n    @location({}) mv_nrm: vec3<f32>,"), vtxOutIdx++);
        vtxXfrAttrs += fmt::format(FMT_STRING(R"""(
    out.mv_pos = mv_pos;
    out.mv_nrm = mv_nrm;)"""));
      }
      addedLightStruct = true;
    }

    if (cc.lightingEnabled && cc.ambSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    cc{0}_amb: vec4<f32>,"), i);
    }
    if (cc.matSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    cc{0}_mat: vec4<f32>,"), i);
    }
    if (cca.lightingEnabled && cca.ambSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    cc{0}a_amb: vec4<f32>,"), i);
    }
    if (cca.matSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    cc{0}a_mat: vec4<f32>,"), i);
    }

    // Output vertex color if necessary
    bool usesVtxColor = false;
    if (((cc.lightingEnabled && cc.ambSrc == GX_SRC_VTX) || cc.matSrc == GX_SRC_VTX ||
         (cca.lightingEnabled && cca.matSrc == GX_SRC_VTX) || cca.matSrc == GX_SRC_VTX)) {
      if (UsePerPixelLighting) {
        vtxOutAttrs += fmt::format(FMT_STRING("\n    @location({}) clr{}: vec4<f32>,"), vtxOutIdx++, vtxColorIdx);
        vtxXfrAttrs += fmt::format(FMT_STRING("\n    out.clr{} = {};"), vtxColorIdx,
                                   vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + vtxColorIdx)));
      }
      usesVtxColor = true;
    }

    // TODO handle alpha lighting
    if (cc.lightingEnabled) {
      std::string ambSrc, matSrc, lightAttnFn, lightDiffFn;
      if (cc.ambSrc == GX_SRC_VTX) {
        if (UsePerPixelLighting) {
          ambSrc = fmt::format(FMT_STRING("in.clr{}"), vtxColorIdx);
        } else {
          ambSrc = vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + vtxColorIdx));
        }
      } else if (cc.ambSrc == GX_SRC_REG) {
        ambSrc = fmt::format(FMT_STRING("ubuf.cc{0}_amb"), i);
      }
      if (cc.matSrc == GX_SRC_VTX) {
        if (UsePerPixelLighting) {
          matSrc = fmt::format(FMT_STRING("in.clr{}"), vtxColorIdx);
        } else {
          matSrc = vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + vtxColorIdx));
        }
      } else if (cc.matSrc == GX_SRC_REG) {
        matSrc = fmt::format(FMT_STRING("ubuf.cc{0}_mat"), i);
      }
      GXDiffuseFn diffFn = cc.diffFn;
      if (cc.attnFn == GX_AF_NONE) {
        lightAttnFn = "attn = 1.0;";
      } else if (cc.attnFn == GX_AF_SPOT) {
        lightAttnFn = fmt::format(FMT_STRING(R"""(
          var cosine = max(0.0, dot(ldir, light.dir));
          var cos_attn = dot(light.cos_att, vec3<f32>(1.0, cosine, cosine * cosine));
          var dist_attn = dot(light.dist_att, vec3<f32>(1.0, dist, dist2));
          attn = max(0.0, cos_attn / dist_attn);)"""));
      } else if (cc.attnFn == GX_AF_SPEC) {
        diffFn = GX_DF_NONE;
        FATAL("AF_SPEC unimplemented");
      }
      if (diffFn == GX_DF_NONE) {
        lightDiffFn = "1.0";
      } else if (diffFn == GX_DF_SIGN) {
        if (UsePerPixelLighting) {
          lightDiffFn = "dot(ldir, in.mv_nrm)";
        } else {
          lightDiffFn = "dot(ldir, mv_nrm)";
        }
      } else if (diffFn == GX_DF_CLAMP) {
        if (UsePerPixelLighting) {
          lightDiffFn = "max(0.0, dot(ldir, in.mv_nrm))";
        } else {
          lightDiffFn = "max(0.0, dot(ldir, mv_nrm))";
        }
      }
      std::string outVar, posVar;
      if (UsePerPixelLighting) {
        outVar = fmt::format(FMT_STRING("rast{}"), i);
        posVar = "in.mv_pos";
      } else {
        outVar = fmt::format(FMT_STRING("out.cc{}"), i);
        posVar = "mv_pos";
      }
      auto lightFunc = fmt::format(FMT_STRING(R"""(
    {{
      var lighting = {5};
      for (var i = 0u; i < {1}u; i++) {{
          if ((ubuf.lightState{0} & (1u << i)) == 0u) {{ continue; }}
          var light = ubuf.lights[i];
          var ldir = light.pos - {7};
          var dist2 = dot(ldir, ldir);
          var dist = sqrt(dist2);
          ldir = ldir / dist;
          var attn: f32;{2}
          var diff = {3};
          lighting = lighting + (attn * diff * light.color);
      }}
      // TODO alpha lighting
      {6} = vec4<f32>(({4} * clamp(lighting, vec4<f32>(0.0), vec4<f32>(1.0))).xyz, {4}.a);
    }})"""),
                                   i, GX::MaxLights, lightAttnFn, lightDiffFn, matSrc, ambSrc, outVar, posVar);
      if (UsePerPixelLighting) {
        fragmentFnPre += fmt::format(FMT_STRING("\n    var rast{}: vec4<f32>;"), i);
        fragmentFnPre += lightFunc;
      } else {
        vtxOutAttrs += fmt::format(FMT_STRING("\n    @location({}) cc{}: vec4<f32>,"), vtxOutIdx++, i);
        vtxXfrAttrs += lightFunc;
        fragmentFnPre += fmt::format(FMT_STRING("\n    var rast{0} = in.cc{0};"), i);
      }
    } else if (cc.matSrc == GX_SRC_VTX) {
      if (UsePerPixelLighting) {
        // Color will already be written to clr{}
        fragmentFnPre += fmt::format(FMT_STRING("\n    var rast{0} = in.clr{0};"), vtxColorIdx);
      } else {
        vtxOutAttrs += fmt::format(FMT_STRING("\n    @location({}) cc{}: vec4<f32>,"), vtxOutIdx++, i);
        vtxXfrAttrs +=
            fmt::format(FMT_STRING("\n    out.cc{} = {};"), i, vtx_attr(config, GXAttr(GX_VA_CLR0 + vtxColorIdx)));
        fragmentFnPre += fmt::format(FMT_STRING("\n    var rast{0} = in.cc{0};"), i);
      }
    } else {
      fragmentFnPre += fmt::format(FMT_STRING("\n    var rast{0} = ubuf.cc{0}_mat;"), i);
    }

    if (usesVtxColor) {
      ++vtxColorIdx;
    }
  }
  for (int i = 0; i < info.sampledKColors.size(); ++i) {
    if (info.sampledKColors.test(i)) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    kcolor{}: vec4<f32>,"), i);
    }
  }
  for (int i = 0; i < info.sampledTexCoords.size(); ++i) {
    if (!info.sampledTexCoords.test(i)) {
      continue;
    }
    const auto& tcg = config.tcgs[i];
    vtxOutAttrs += fmt::format(FMT_STRING("\n    @location({}) tex{}_uv: vec2<f32>,"), vtxOutIdx++, i);
    if (tcg.src >= GX_TG_TEX0 && tcg.src <= GX_TG_TEX7) {
      vtxXfrAttrs += fmt::format(FMT_STRING("\n    var tc{} = vec4<f32>({}, 0.0, 1.0);"), i,
                                 vtx_attr(config, GXAttr(GX_VA_TEX0 + (tcg.src - GX_TG_TEX0))));
    } else if (tcg.src == GX_TG_POS) {
      vtxXfrAttrs += fmt::format(FMT_STRING("\n    var tc{} = vec4<f32>(in_pos, 1.0);"), i);
    } else if (tcg.src == GX_TG_NRM) {
      vtxXfrAttrs += fmt::format(FMT_STRING("\n    var tc{} = vec4<f32>(in_nrm, 1.0);"), i);
    } else
      UNLIKELY FATAL("unhandled tcg src {}", static_cast<int>(tcg.src));
    if (tcg.mtx == GX_IDENTITY) {
      vtxXfrAttrs += fmt::format(FMT_STRING("\n    var tc{0}_tmp = tc{0}.xyz;"), i);
    } else {
      u32 texMtxIdx = (tcg.mtx - GX_TEXMTX0) / 3;
      vtxXfrAttrs += fmt::format(FMT_STRING("\n    var tc{0}_tmp = mul{2}(ubuf.texmtx{1}, tc{0});"), i, texMtxIdx,
                                 info.texMtxTypes[texMtxIdx] == GX_TG_MTX3x4 ? "4x3" : "4x2");
    }
    if (tcg.normalize) {
      vtxXfrAttrs += fmt::format(FMT_STRING("\n    tc{0}_tmp = normalize(tc{0}_tmp);"), i);
    }
    if (tcg.postMtx == GX_PTIDENTITY) {
      vtxXfrAttrs += fmt::format(FMT_STRING("\n    var tc{0}_proj = tc{0}_tmp;"), i);
    } else {
      u32 postMtxIdx = (tcg.postMtx - GX_PTTEXMTX0) / 3;
      vtxXfrAttrs += fmt::format(
          FMT_STRING("\n    var tc{0}_proj = mul4x3(ubuf.postmtx{1}, vec4<f32>(tc{0}_tmp.xyz, 1.0));"), i, postMtxIdx);
    }
    vtxXfrAttrs += fmt::format(FMT_STRING("\n    out.tex{0}_uv = tc{0}_proj.xy;"), i);
  }
  for (int i = 0; i < config.tevStages.size(); ++i) {
    const auto& stage = config.tevStages[i];
    if (stage.texMapId == GX_TEXMAP_NULL ||
        stage.texCoordId == GX_TEXCOORD_NULL
        // TODO should check this per-stage probably
        || !info.sampledTextures.test(stage.texMapId)) {
      continue;
    }
    std::string uvIn = fmt::format(FMT_STRING("in.tex{0}_uv"), stage.texCoordId);
    const auto& texConfig = config.textureConfig[stage.texMapId];
    if (is_palette_format(texConfig.loadFmt)) {
      std::string_view suffix;
      if (!is_palette_format(texConfig.copyFmt)) {
        switch (texConfig.loadFmt) {
          DEFAULT_FATAL("unimplemented palette format {}", static_cast<int>(texConfig.loadFmt));
        case GX_TF_C4:
          suffix = "I4"sv;
          break;
          //        case GX_TF_C8:
          //          suffix = "I8";
          //          break;
          //        case GX_TF_C14X2:
          //          suffix = "I14X2";
          //          break;
        }
      }
      fragmentFnPre +=
          fmt::format(FMT_STRING("\n    var sampled{0} = textureSamplePalette{3}(tex{1}, tex{1}_samp, {2}, tlut{1});"),
                      i, stage.texMapId, uvIn, suffix);
    } else {
      fragmentFnPre += fmt::format(
          FMT_STRING("\n    var sampled{0} = textureSampleBias(tex{1}, tex{1}_samp, {2}, ubuf.tex{1}_lod);"), i,
          stage.texMapId, uvIn);
    }
    fragmentFnPre += texture_conversion(texConfig, i, stage.texMapId);
  }
  for (int i = 0; i < info.usesTexMtx.size(); ++i) {
    if (info.usesTexMtx.test(i)) {
      switch (info.texMtxTypes[i]) {
        DEFAULT_FATAL("unhandled tex mtx type {}", static_cast<int>(info.texMtxTypes[i]));
      case GX_TG_MTX2x4:
        uniBufAttrs += fmt::format(FMT_STRING("\n    texmtx{}: mtx4x2,"), i);
        break;
      case GX_TG_MTX3x4:
        uniBufAttrs += fmt::format(FMT_STRING("\n    texmtx{}: mtx4x3,"), i);
        break;
      }
    }
  }
  for (int i = 0; i < info.usesPTTexMtx.size(); ++i) {
    if (info.usesPTTexMtx.test(i)) {
      uniBufAttrs += fmt::format(FMT_STRING("\n    postmtx{}: mtx4x3,"), i);
    }
  }
  if (info.usesFog) {
    uniformPre +=
        "\n"
        "struct Fog {\n"
        "    color: vec4<f32>,\n"
        "    a: f32,\n"
        "    b: f32,\n"
        "    c: f32,\n"
        "    pad: f32,\n"
        "}";
    uniBufAttrs += "\n    fog: Fog,";

    fragmentFn += "\n    // Fog\n    var fogF = clamp((ubuf.fog.a / (ubuf.fog.b - in.pos.z)) - ubuf.fog.c, 0.0, 1.0);";
    switch (config.fogType) {
      DEFAULT_FATAL("invalid fog type {}", static_cast<int>(config.fogType));
    case GX_FOG_PERSP_LIN:
    case GX_FOG_ORTHO_LIN:
      fragmentFn += "\n    var fogZ = fogF;";
      break;
    case GX_FOG_PERSP_EXP:
    case GX_FOG_ORTHO_EXP:
      fragmentFn += "\n    var fogZ = 1.0 - exp2(-8.0 * fogF);";
      break;
    case GX_FOG_PERSP_EXP2:
    case GX_FOG_ORTHO_EXP2:
      fragmentFn += "\n    var fogZ = 1.0 - exp2(-8.0 * fogF * fogF);";
      break;
    case GX_FOG_PERSP_REVEXP:
    case GX_FOG_ORTHO_REVEXP:
      fragmentFn += "\n    var fogZ = exp2(-8.0 * (1.0 - fogF));";
      break;
    case GX_FOG_PERSP_REVEXP2:
    case GX_FOG_ORTHO_REVEXP2:
      fragmentFn +=
          "\n    fogF = 1.0 - fogF;"
          "\n    var fogZ = exp2(-8.0 * fogF * fogF);";
      break;
    }
    fragmentFn += "\n    prev = vec4<f32>(mix(prev.rgb, ubuf.fog.color.rgb, clamp(fogZ, 0.0, 1.0)), prev.a);";
  }
  size_t texBindIdx = 0;
  for (int i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    uniBufAttrs += fmt::format(FMT_STRING("\n    tex{}_lod: f32,"), i);

    sampBindings += fmt::format(FMT_STRING("\n@group(1) @binding({})\n"
                                           "var tex{}_samp: sampler;"),
                                texBindIdx, i);

    const auto& texConfig = config.textureConfig[i];
    if (is_palette_format(texConfig.loadFmt)) {
      texBindings += fmt::format(FMT_STRING("\n@group(2) @binding({})\n"
                                            "var tex{}: texture_2d<{}>;"),
                                 texBindIdx, i, is_palette_format(texConfig.copyFmt) ? "i32"sv : "f32"sv);
      ++texBindIdx;
      texBindings += fmt::format(FMT_STRING("\n@group(2) @binding({})\n"
                                            "var tlut{}: texture_2d<f32>;"),
                                 texBindIdx, i);
    } else {
      texBindings += fmt::format(FMT_STRING("\n@group(2) @binding({})\n"
                                            "var tex{}: texture_2d<f32>;"),
                                 texBindIdx, i);
    }
    ++texBindIdx;
  }

  if (config.alphaCompare) {
    bool comp0Valid = true;
    bool comp1Valid = true;
    std::string comp0 = alpha_compare(config.alphaCompare.comp0, config.alphaCompare.ref0, comp0Valid);
    std::string comp1 = alpha_compare(config.alphaCompare.comp1, config.alphaCompare.ref1, comp1Valid);
    if (comp0Valid || comp1Valid) {
      fragmentFn += "\n    // Alpha compare";
      switch (config.alphaCompare.op) {
        DEFAULT_FATAL("invalid alpha compare op {}", static_cast<int>(config.alphaCompare.op));
      case GX_AOP_AND:
        fragmentFn += fmt::format(FMT_STRING("\n    if (!({} && {})) {{ discard; }}"), comp0, comp1);
        break;
      case GX_AOP_OR:
        fragmentFn += fmt::format(FMT_STRING("\n    if (!({} || {})) {{ discard; }}"), comp0, comp1);
        break;
      case GX_AOP_XOR:
        fragmentFn += fmt::format(FMT_STRING("\n    if (!({} ^^ {})) {{ discard; }}"), comp0, comp1);
        break;
      case GX_AOP_XNOR:
        fragmentFn += fmt::format(FMT_STRING("\n    if (({} ^^ {})) {{ discard; }}"), comp0, comp1);
        break;
      }
    }
  }
  if constexpr (EnableNormalVisualization) {
    fragmentFn += "\n    prev = vec4<f32>(in.nrm, prev.a);";
  }

  const auto shaderSource = fmt::format(FMT_STRING(R"""(
struct mtx4x4 {{ mx: vec4<f32>, my: vec4<f32>, mz: vec4<f32>, mw: vec4<f32> }};
struct mtx4x3 {{ mx: vec4<f32>, my: vec4<f32>, mz: vec4<f32>, mw: vec4<f32> }};
struct mtx4x2 {{ mx: vec4<f32>, my: vec4<f32>, }};
// TODO convert these to row major
fn mul4x4(m: mtx4x4, v: vec4<f32>) -> vec4<f32> {{
  var mx = vec4<f32>(m.mx.x, m.my.x, m.mz.x, m.mw.x);
  var my = vec4<f32>(m.mx.y, m.my.y, m.mz.y, m.mw.y);
  var mz = vec4<f32>(m.mx.z, m.my.z, m.mz.z, m.mw.z);
  var mw = vec4<f32>(m.mx.w, m.my.w, m.mz.w, m.mw.w);
  return vec4<f32>(dot(mx, v), dot(my, v), dot(mz, v), dot(mw, v));
}}
fn mul4x3(m: mtx4x3, v: vec4<f32>) -> vec3<f32> {{
  var mx = vec4<f32>(m.mx.x, m.my.x, m.mz.x, m.mw.x);
  var my = vec4<f32>(m.mx.y, m.my.y, m.mz.y, m.mw.y);
  var mz = vec4<f32>(m.mx.z, m.my.z, m.mz.z, m.mw.z);
  return vec3<f32>(dot(mx, v), dot(my, v), dot(mz, v));
}}
fn mul4x2(m: mtx4x2, v: vec4<f32>) -> vec2<f32> {{
  return vec2<f32>(dot(m.mx, v), dot(m.my, v));
}}
{10}
struct Uniform {{
    pos_mtx: mtx4x3,
    nrm_mtx: mtx4x3,
    proj: mtx4x4,{0}
}};
@group(0) @binding(0)
var<uniform> ubuf: Uniform;{3}{1}{2}

struct VertexOutput {{
    @builtin(position) pos: vec4<f32>,{4}
}};

fn intensityF32(rgb: vec3<f32>) -> f32 {{
    // RGB to intensity conversion
    // https://github.com/dolphin-emu/dolphin/blob/4cd48e609c507e65b95bca5afb416b59eaf7f683/Source/Core/VideoCommon/TextureConverterShaderGen.cpp#L237-L241
    return dot(rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0;
}}
fn intensityI4(rgb: vec3<f32>) -> i32 {{
    return i32(intensityF32(rgb) * 16.f);
}}
fn textureSamplePalette(tex: texture_2d<i32>, samp: sampler, uv: vec2<f32>, tlut: texture_2d<f32>) -> vec4<f32> {{
    // Gather index values
    var i = textureGather(0, tex, samp, uv);
    // Load palette colors
    var c0 = textureLoad(tlut, vec2<i32>(i[0], 0), 0);
    var c1 = textureLoad(tlut, vec2<i32>(i[1], 0), 0);
    var c2 = textureLoad(tlut, vec2<i32>(i[2], 0), 0);
    var c3 = textureLoad(tlut, vec2<i32>(i[3], 0), 0);
    // Perform bilinear filtering
    var f = fract(uv * vec2<f32>(textureDimensions(tex)) + 0.5);
    var t0 = mix(c3, c2, f.x);
    var t1 = mix(c0, c1, f.x);
    return mix(t0, t1, f.y);
}}
fn textureSamplePaletteI4(tex: texture_2d<f32>, samp: sampler, uv: vec2<f32>, tlut: texture_2d<f32>) -> vec4<f32> {{
    // Gather RGB channels
    var iR = textureGather(0, tex, samp, uv);
    var iG = textureGather(1, tex, samp, uv);
    var iB = textureGather(2, tex, samp, uv);
    // Perform intensity conversion
    var i0 = intensityI4(vec3<f32>(iR[0], iG[0], iB[0]));
    var i1 = intensityI4(vec3<f32>(iR[1], iG[1], iB[1]));
    var i2 = intensityI4(vec3<f32>(iR[2], iG[2], iB[2]));
    var i3 = intensityI4(vec3<f32>(iR[3], iG[3], iB[3]));
    // Load palette colors
    var c0 = textureLoad(tlut, vec2<i32>(i0, 0), 0);
    var c1 = textureLoad(tlut, vec2<i32>(i1, 0), 0);
    var c2 = textureLoad(tlut, vec2<i32>(i2, 0), 0);
    var c3 = textureLoad(tlut, vec2<i32>(i3, 0), 0);
    // Perform bilinear filtering
    var f = fract(uv * vec2<f32>(textureDimensions(tex)) + 0.5);
    var t0 = mix(c3, c2, f.x);
    var t1 = mix(c0, c1, f.x);
    return mix(t0, t1, f.y);
}}

@vertex
fn vs_main({5}
) -> VertexOutput {{
    var out: VertexOutput;{9}{6}
    return out;
}}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {{{8}{7}
    return prev;
}}
)"""),
                                        uniBufAttrs, sampBindings, texBindings, uniformBindings, vtxOutAttrs,
                                        vtxInAttrs, vtxXfrAttrs, fragmentFn, fragmentFnPre, vtxXfrAttrsPre, uniformPre);
  if (EnableDebugPrints) {
    Log.report(LOG_INFO, FMT_STRING("Generated shader: {}"), shaderSource);
  }

  wgpu::ShaderModuleWGSLDescriptor wgslDescriptor{};
  wgslDescriptor.source = shaderSource.c_str();
  const auto label = fmt::format(FMT_STRING("GX Shader {:x}"), hash);
  const auto shaderDescriptor = wgpu::ShaderModuleDescriptor{
      .nextInChain = &wgslDescriptor,
      .label = label.c_str(),
  };
  auto shader = webgpu::g_device.CreateShaderModule(&shaderDescriptor);

  auto pair = std::make_pair(shader, info);
  g_gxCachedShaders.emplace(hash, pair);
#ifndef NDEBUG
  g_gxCachedShaderConfigs.emplace(hash, config);
#endif

  return pair.first;
}
} // namespace aurora::gfx::gx

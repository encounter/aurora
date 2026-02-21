#include "common.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "gx.hpp"
#include "gx_fmt.hpp"

#include <dolphin/gx/GXEnum.h>

#include <absl/container/flat_hash_map.h>
#include <string_view>
#include <utility>

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

u8 color_channel(GXChannelID id) {
  switch (id) {
    DEFAULT_FATAL("unimplemented color channel {}", id);
  case GX_COLOR0:
  case GX_ALPHA0:
  case GX_COLOR0A0:
    return 0;
  case GX_COLOR1:
  case GX_ALPHA1:
  case GX_COLOR1A1:
    return 1;
  }
}

static std::string color_arg_reg(GXTevColorArg arg, size_t stageIdx, const ShaderConfig& config,
                                 const TevStage& stage) {
  switch (arg) {
    DEFAULT_FATAL("invalid color arg {}", underlying(arg));
  case GX_CC_CPREV:
    return "prev.rgb";
  case GX_CC_APREV:
    return "vec3f(prev.a)";
  case GX_CC_C0:
    return "tevreg0.rgb";
  case GX_CC_A0:
    return "vec3f(tevreg0.a)";
  case GX_CC_C1:
    return "tevreg1.rgb";
  case GX_CC_A1:
    return "vec3f(tevreg1.a)";
  case GX_CC_C2:
    return "tevreg2.rgb";
  case GX_CC_A2:
    return "vec3f(tevreg2.a)";
  case GX_CC_TEXC: {
    CHECK(stage.texMapId != GX_TEXMAP_NULL, "unmapped texture for stage {}", stageIdx);
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          underlying(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format("sampled{}.{}{}{}", stageIdx, chan_comp(swap.red), chan_comp(swap.green), chan_comp(swap.blue));
  }
  case GX_CC_TEXA: {
    CHECK(stage.texMapId != GX_TEXMAP_NULL, "unmapped texture for stage {}", stageIdx);
    CHECK(stage.texMapId >= GX_TEXMAP0 && stage.texMapId <= GX_TEXMAP7, "invalid texture {} for stage {}",
          underlying(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format("vec3f(sampled{}.{})", stageIdx, chan_comp(swap.alpha));
  }
  case GX_CC_RASC: {
    CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO) {
      return "vec3f(0.0)";
    }
    u32 idx = color_channel(stage.channelId);
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format("rast{}.{}{}{}", idx, chan_comp(swap.red), chan_comp(swap.green), chan_comp(swap.blue));
  }
  case GX_CC_RASA: {
    CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO) {
      return "vec3f(0.0)";
    }
    u32 idx = color_channel(stage.channelId);
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format("vec3f(rast{}.{})", idx, chan_comp(swap.alpha));
  }
  case GX_CC_ONE:
    return "vec3f(1.0)";
  case GX_CC_HALF:
    return "vec3f(0.5)";
  case GX_CC_KONST: {
    switch (stage.kcSel) {
      DEFAULT_FATAL("invalid kcSel {}", underlying(stage.kcSel));
    case GX_TEV_KCSEL_8_8:
      return "vec3f(1.0)";
    case GX_TEV_KCSEL_7_8:
      return "vec3f(7.0/8.0)";
    case GX_TEV_KCSEL_6_8:
      return "vec3f(6.0/8.0)";
    case GX_TEV_KCSEL_5_8:
      return "vec3f(5.0/8.0)";
    case GX_TEV_KCSEL_4_8:
      return "vec3f(4.0/8.0)";
    case GX_TEV_KCSEL_3_8:
      return "vec3f(3.0/8.0)";
    case GX_TEV_KCSEL_2_8:
      return "vec3f(2.0/8.0)";
    case GX_TEV_KCSEL_1_8:
      return "vec3f(1.0/8.0)";
    case GX_TEV_KCSEL_K0:
      return "ubuf.kcolor0.rgb";
    case GX_TEV_KCSEL_K1:
      return "ubuf.kcolor1.rgb";
    case GX_TEV_KCSEL_K2:
      return "ubuf.kcolor2.rgb";
    case GX_TEV_KCSEL_K3:
      return "ubuf.kcolor3.rgb";
    case GX_TEV_KCSEL_K0_R:
      return "vec3f(ubuf.kcolor0.r)";
    case GX_TEV_KCSEL_K1_R:
      return "vec3f(ubuf.kcolor1.r)";
    case GX_TEV_KCSEL_K2_R:
      return "vec3f(ubuf.kcolor2.r)";
    case GX_TEV_KCSEL_K3_R:
      return "vec3f(ubuf.kcolor3.r)";
    case GX_TEV_KCSEL_K0_G:
      return "vec3f(ubuf.kcolor0.g)";
    case GX_TEV_KCSEL_K1_G:
      return "vec3f(ubuf.kcolor1.g)";
    case GX_TEV_KCSEL_K2_G:
      return "vec3f(ubuf.kcolor2.g)";
    case GX_TEV_KCSEL_K3_G:
      return "vec3f(ubuf.kcolor3.g)";
    case GX_TEV_KCSEL_K0_B:
      return "vec3f(ubuf.kcolor0.b)";
    case GX_TEV_KCSEL_K1_B:
      return "vec3f(ubuf.kcolor1.b)";
    case GX_TEV_KCSEL_K2_B:
      return "vec3f(ubuf.kcolor2.b)";
    case GX_TEV_KCSEL_K3_B:
      return "vec3f(ubuf.kcolor3.b)";
    case GX_TEV_KCSEL_K0_A:
      return "vec3f(ubuf.kcolor0.a)";
    case GX_TEV_KCSEL_K1_A:
      return "vec3f(ubuf.kcolor1.a)";
    case GX_TEV_KCSEL_K2_A:
      return "vec3f(ubuf.kcolor2.a)";
    case GX_TEV_KCSEL_K3_A:
      return "vec3f(ubuf.kcolor3.a)";
    }
  }
  case GX_CC_ZERO:
    return "vec3f(0.0)";
  }
}

static std::string alpha_arg_reg(GXTevAlphaArg arg, size_t stageIdx, const ShaderConfig& config,
                                 const TevStage& stage) {
  switch (arg) {
    DEFAULT_FATAL("invalid alpha arg {}", underlying(arg));
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
          underlying(stage.texMapId), stageIdx);
    const auto& swap = config.tevSwapTable[stage.tevSwapTex];
    return fmt::format("sampled{}.{}", stageIdx, chan_comp(swap.alpha));
  }
  case GX_CA_RASA: {
    CHECK(stage.channelId != GX_COLOR_NULL, "unmapped color channel for stage {}", stageIdx);
    if (stage.channelId == GX_COLOR_ZERO) {
      return "0.0";
    }
    u32 idx = color_channel(stage.channelId);
    const auto& swap = config.tevSwapTable[stage.tevSwapRas];
    return fmt::format("rast{}.{}", idx, chan_comp(swap.alpha));
  }
  case GX_CA_KONST: {
    switch (stage.kaSel) {
      DEFAULT_FATAL("invalid kaSel {}", underlying(stage.kaSel));
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
    DEFAULT_FATAL("unimplemented tev op {}", underlying(op));
  case GX_TEV_ADD:
    return ""sv;
  case GX_TEV_SUB:
    return "-"sv;
  }
}

static std::string_view tev_bias(GXTevBias bias) {
  switch (bias) {
    DEFAULT_FATAL("invalid tev bias {}", underlying(bias));
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
    DEFAULT_FATAL("invalid alpha comp {}", underlying(comp));
  case GX_NEVER:
    return "false"s;
  case GX_LESS:
    return fmt::format("(prev.a < {}f)", fref);
  case GX_LEQUAL:
    return fmt::format("(prev.a <= {}f)", fref);
  case GX_EQUAL:
    return fmt::format("(prev.a == {}f)", fref);
  case GX_NEQUAL:
    return fmt::format("(prev.a != {}f)", fref);
  case GX_GEQUAL:
    return fmt::format("(prev.a >= {}f)", fref);
  case GX_GREATER:
    return fmt::format("(prev.a > {}f)", fref);
  case GX_ALWAYS:
    valid = false;
    return "true"s;
  }
}

static std::string_view tev_scale(GXTevScale scale) {
  switch (scale) {
    DEFAULT_FATAL("invalid tev scale {}", underlying(scale));
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
      return "vec3f(1.0, 0.0, 0.0)"s;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
      return "vec4f(0.0, 0.0, 0.0, 0.0)"s;
    }
    UNLIKELY FATAL("unmapped vtx attr {}", underlying(attr));
  }
  if (attr == GX_VA_POS) {
    return "in_pos"s;
  }
  if (attr == GX_VA_NRM) {
    return "in_nrm"s;
  }
  if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
    const auto idx = attr - GX_VA_CLR0;
    return fmt::format("in_clr{}", idx);
  }
  if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
    const auto idx = attr - GX_VA_TEX0;
    return fmt::format("in_tex{}_uv", idx);
  }
  UNLIKELY FATAL("unhandled vtx attr {}", underlying(attr));
}

static inline std::string texture_conversion(const TextureConfig& tex, u32 stageIdx, u32 texMapId) {
  std::string out;
  if (tex.renderTex)
    switch (tex.copyFmt) {
    default:
      break;
    case GX_TF_RGB565:
      // Set alpha channel to 1.0
      out += fmt::format("\n    sampled{0}.a = 1.0;", stageIdx);
      break;
    case GX_TF_I4:
    case GX_TF_I8:
      // FIXME HACK
      if (!is_palette_format(tex.loadFmt)) {
        // Perform intensity conversion
        out += fmt::format("\n    sampled{0} = vec4f(intensityF32(sampled{0}.rgb), 0.f, 0.f, 1.f);", stageIdx);
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
    out += fmt::format("\n    sampled{0} = vec4f(sampled{0}.r);", stageIdx);
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

struct StorageLoadResult {
  std::string attrLoad;
  std::string_view arrType;
};

auto storage_load(const StorageConfig& mapping, u32 attrIdx) -> StorageLoadResult {
  const std::string_view attrName = VtxAttributeNames[mapping.attr];

  uint8_t compCnt = 0;
  GXCompType compType = GX_U8;
  switch (mapping.attr) {
  case GX_VA_POS:
    switch (mapping.cnt) {
    case GX_POS_XY:
      compCnt = 2;
      break;
    case GX_POS_XYZ:
      compCnt = 3;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component count {}", mapping.attr, mapping.cnt);
    }
    switch (mapping.compType) {
    case GX_U8:
    case GX_S8:
    case GX_U16:
    case GX_S16:
    case GX_F32:
      compType = mapping.compType;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component type {}", mapping.attr, mapping.compType);
    }
    break;
  case GX_VA_NRM:
    switch (mapping.cnt) {
    case GX_NRM_XYZ:
      compCnt = 3;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component count {}", mapping.attr, mapping.cnt);
    }
    switch (mapping.compType) {
    case GX_S8:
    case GX_S16:
    case GX_F32:
      compType = mapping.compType;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component type {}", mapping.attr, mapping.compType);
    }
    break;
  case GX_VA_CLR0:
  case GX_VA_CLR1:
    switch (mapping.cnt) {
    case GX_CLR_RGB:
      compCnt = 3;
      break;
    case GX_CLR_RGBA:
      compCnt = 4;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component count {}", mapping.attr, mapping.cnt);
    }
    switch (mapping.compType) {
    case GX_RGB8:
    case GX_RGBA8:
      compType = mapping.compType;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component type {}", mapping.attr, mapping.compType);
    }
    break;
  case GX_VA_TEX0:
  case GX_VA_TEX1:
  case GX_VA_TEX2:
  case GX_VA_TEX3:
  case GX_VA_TEX4:
  case GX_VA_TEX5:
  case GX_VA_TEX6:
  case GX_VA_TEX7:
    switch (mapping.cnt) {
    case GX_TEX_S:
      compCnt = 1;
      break;
    case GX_TEX_ST:
      compCnt = 2;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component count {}", mapping.attr, mapping.cnt);
    }
    switch (mapping.compType) {
    case GX_U8:
    case GX_S8:
    case GX_U16:
    case GX_S16:
    case GX_F32:
      compType = mapping.compType;
      break;
    default:
      Log.fatal("storage_load: Unsupported {} component type {}", mapping.attr, mapping.compType);
    }
    break;
  default:
    Log.fatal("storage_load: Unsupported attribute {}", mapping.attr);
  }

  const auto [div, rem] = std::div(attrIdx, 4);
  std::string idxFetch = fmt::format("in_dl{}[{}]", div, rem);

  std::string_view arrType;
  std::string attrLoad;

  switch (compType) {
  case GX_S8:
    switch (compCnt) {
    case 3:
      arrType = "i32";
      attrLoad = fmt::format("fetch_i8_3(&v_arr_{}, {}, {})", attrName, idxFetch, mapping.frac);
      break;
    default:
      Log.fatal("storage_load: Unsupported {} count {}", compType, compCnt);
    }
    break;
  case GX_U16:
    switch (compCnt) {
    case 2:
      arrType = "u32";
      attrLoad = fmt::format("fetch_u16_2(&v_arr_{}, {}, {})", attrName, idxFetch, mapping.frac);
      break;
    default:
      Log.fatal("storage_load: Unsupported {} count {}", compType, compCnt);
    }
    break;
  case GX_S16:
    switch (compCnt) {
    case 3:
      arrType = "i32";
      attrLoad = fmt::format("fetch_i16_3(&v_arr_{}, {}, {})", attrName, idxFetch, mapping.frac);
      break;
    default:
      Log.fatal("storage_load: Unsupported {} count {}", compType, compCnt);
    }
    break;
  case GX_F32:
    switch (compCnt) {
    case 1:
      arrType = "f32";
      attrLoad = fmt::format("v_arr_{}[{}]", attrName, idxFetch);
      break;
    case 2:
      arrType = "vec2f";
      attrLoad = fmt::format("v_arr_{}[{}]", attrName, idxFetch);
      break;
    case 3:
      arrType = "f32";
      attrLoad = fmt::format("fetch_f32_3(&v_arr_{}, {})", attrName, idxFetch);
      break;
    case 4:
      arrType = "vec4f";
      attrLoad = fmt::format("v_arr_{}[{}]", attrName, idxFetch);
      break;
    default:
      Log.fatal("storage_load: Unsupported {} count {}", compType, compCnt);
    }
    break;
  case GX_RGBA8:
    arrType = "u32";
    attrLoad = fmt::format("unpack4x8unorm(v_arr_{}[{}])", attrName, idxFetch);
    break;
  default:
    Log.fatal("storage_load: Unimplemented {}", compType);
  }

  return {
      .attrLoad = attrLoad,
      .arrType = arrType,
  };
}

wgpu::ShaderModule build_shader(const ShaderConfig& config, const ShaderInfo& info) noexcept {
  const auto hash = xxh3_hash(config);
  const auto it = g_gxCachedShaders.find(hash);
  if (it != g_gxCachedShaders.end()) {
    // CHECK(g_gxCachedShaderConfigs[hash] == config, "Shader collision! {:x}", hash);
    return it->second.first;
  }

  if (EnableDebugPrints) {
    Log.info("Shader config (hash {:x}):", hash);
    {
      for (int i = 0; i < config.tevStageCount; ++i) {
        const auto& stage = config.tevStages[i];
        Log.info("  tevStages[{}]:", i);
        Log.info("    color_a: {}", TevColorArgNames[stage.colorPass.a]);
        Log.info("    color_b: {}", TevColorArgNames[stage.colorPass.b]);
        Log.info("    color_c: {}", TevColorArgNames[stage.colorPass.c]);
        Log.info("    color_d: {}", TevColorArgNames[stage.colorPass.d]);
        Log.info("    alpha_a: {}", TevAlphaArgNames[stage.alphaPass.a]);
        Log.info("    alpha_b: {}", TevAlphaArgNames[stage.alphaPass.b]);
        Log.info("    alpha_c: {}", TevAlphaArgNames[stage.alphaPass.c]);
        Log.info("    alpha_d: {}", TevAlphaArgNames[stage.alphaPass.d]);
        Log.info("    color_op_clamp: {}", stage.colorOp.clamp);
        Log.info("    color_op_op: {}", stage.colorOp.op);
        Log.info("    color_op_bias: {}", stage.colorOp.bias);
        Log.info("    color_op_scale: {}", stage.colorOp.scale);
        Log.info("    color_op_reg_id: {}", stage.colorOp.outReg);
        Log.info("    alpha_op_clamp: {}", stage.alphaOp.clamp);
        Log.info("    alpha_op_op: {}", stage.alphaOp.op);
        Log.info("    alpha_op_bias: {}", stage.alphaOp.bias);
        Log.info("    alpha_op_scale: {}", stage.alphaOp.scale);
        Log.info("    alpha_op_reg_id: {}", stage.alphaOp.outReg);
        Log.info("    kc_sel: {}", stage.kcSel);
        Log.info("    ka_sel: {}", stage.kaSel);
        Log.info("    texCoordId: {}", stage.texCoordId);
        Log.info("    texMapId: {}", stage.texMapId);
        Log.info("    channelId: {}", stage.channelId);
      }
      for (int i = 0; i < config.colorChannels.size(); ++i) {
        const auto& chan = config.colorChannels[i];
        Log.info("  colorChannels[{}]: enabled {} mat {} amb {}", static_cast<GXChannelID>(i), chan.lightingEnabled,
                 chan.matSrc, chan.ambSrc);
      }
      for (int i = 0; i < config.tcgs.size(); ++i) {
        const auto& tcg = config.tcgs[i];
        if (tcg.src != GX_MAX_TEXGENSRC) {
          Log.info("  tcg[{}]: src {} mtx {} post {} type {} norm {}", i, tcg.src, tcg.mtx, tcg.postMtx, tcg.type,
                   tcg.normalize);
        }
      }
      Log.info("  alphaCompare: comp0 {} ref0 {} op {} comp1 {} ref1 {}", config.alphaCompare.comp0,
               config.alphaCompare.ref0, config.alphaCompare.op, config.alphaCompare.comp1, config.alphaCompare.ref1);
      Log.info("  indexedAttributeCount: {}", config.indexedAttributeCount);
      Log.info("  fogType: {}", config.fogType);
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
    for (GXAttr attr{}; attr < MaxVtxAttr; attr = static_cast<GXAttr>(attr + 1)) {
      // Indexed attributes
      if (config.vtxAttrs[attr] != GX_INDEX8 && config.vtxAttrs[attr] != GX_INDEX16) {
        continue;
      }
      const auto& mapping = config.attrMapping[attr];
      std::string_view attrName = VtxAttributeNames[mapping.attr];
      const auto result = storage_load(mapping, currAttrIdx);
      vtxXfrAttrsPre += fmt::format("\n    var {} = {};", vtx_attr(config, attr), result.attrLoad);
      uniformBindings += fmt::format(
          "\n@group(0) @binding({})"
          "\nvar<storage, read> v_arr_{}: array<{}>;",
          uniBindingIdx++, attrName, result.arrType);
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
      vtxInAttrs += fmt::format("@location({}) in_dl{}: vec4u", locIdx++, i);
    }
    for (u32 i = 0; i < num2xAttrArrays; ++i) {
      if (locIdx > 0) {
        vtxInAttrs += "\n    , ";
      } else {
        vtxInAttrs += "\n    ";
      }
      vtxInAttrs += fmt::format("@location({}) in_dl{}: vec2u", locIdx++, num4xAttrArrays + i);
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
      vtxInAttrs += fmt::format("@location({}) in_pos: vec3f", locIdx++);
    } else if (attr == GX_VA_NRM) {
      vtxInAttrs += fmt::format("@location({}) in_nrm: vec3f", locIdx++);
    } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
      vtxInAttrs += fmt::format("@location({}) in_clr{}: vec4f", locIdx++, attr - GX_VA_CLR0);
    } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
      vtxInAttrs += fmt::format("@location({}) in_tex{}_uv: vec2f", locIdx++, attr - GX_VA_TEX0);
    } else {
      FATAL("unhandled vtx attr {}", underlying(attr));
    }
  }
  vtxXfrAttrsPre += fmt::format(
      "\n    var mv_pos = vec4<f32>({}, 1.0) * ubuf.pos_mtx;"
      "\n    var mv_nrm = normalize(vec4<f32>({}, 0.0) * ubuf.nrm_mtx);"
      "\n    out.pos = vec4f(mv_pos, 1.0) * ubuf.proj;",
      vtx_attr(config, GX_VA_POS), vtx_attr(config, GX_VA_NRM));
  if constexpr (UseReversedZ) {
    vtxXfrAttrsPre += "\n    out.pos.z = -out.pos.z;";
  } else {
    vtxXfrAttrsPre += "\n    out.pos.z += out.pos.w;";
  }
  if constexpr (EnableNormalVisualization) {
    vtxOutAttrs += fmt::format("\n    @location({}) nrm: vec3f,", vtxOutIdx++);
    vtxXfrAttrsPre += "\n    out.nrm = mv_nrm;";
  }

  std::string fragmentFnPre;
  std::string fragmentFn;
  for (u32 idx = 0; idx < config.tevStageCount; ++idx) {
    const auto& stage = config.tevStages[idx];
    {
      std::string outReg;
      switch (stage.colorOp.outReg) {
        DEFAULT_FATAL("invalid colorOp outReg {}", underlying(stage.colorOp.outReg));
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
          "(({4}mix({0}, {1}, {2}) + {3}){5}){6}", color_arg_reg(stage.colorPass.a, idx, config, stage),
          color_arg_reg(stage.colorPass.b, idx, config, stage), color_arg_reg(stage.colorPass.c, idx, config, stage),
          color_arg_reg(stage.colorPass.d, idx, config, stage), tev_op(stage.colorOp.op), tev_bias(stage.colorOp.bias),
          tev_scale(stage.colorOp.scale));
      if (stage.colorOp.clamp) {
        op = fmt::format("clamp({}, vec3f(0.0), vec3f(1.0))", op);
      }
      fragmentFn += fmt::format("\n    // TEV stage {2}\n    {0} = vec4f({1}, {0}.a);", outReg, op, idx);
    }
    {
      std::string outReg;
      switch (stage.alphaOp.outReg) {
        DEFAULT_FATAL("invalid alphaOp outReg {}", underlying(stage.alphaOp.outReg));
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
          "(({4}mix({0}, {1}, {2}) + {3}){5}){6}", alpha_arg_reg(stage.alphaPass.a, idx, config, stage),
          alpha_arg_reg(stage.alphaPass.b, idx, config, stage), alpha_arg_reg(stage.alphaPass.c, idx, config, stage),
          alpha_arg_reg(stage.alphaPass.d, idx, config, stage), tev_op(stage.alphaOp.op), tev_bias(stage.alphaOp.bias),
          tev_scale(stage.alphaOp.scale));
      if (stage.alphaOp.clamp) {
        op = fmt::format("clamp({}, 0.0, 1.0)", op);
      }
      fragmentFn += fmt::format("\n    {0} = {1};", outReg, op);
    }
  }
  if (info.loadsTevReg.test(0)) {
    uniBufAttrs += "\n    tevprev: vec4f,";
    fragmentFnPre += "\n    var prev = ubuf.tevprev;";
  } else {
    fragmentFnPre += "\n    var prev: vec4f;";
  }
  for (int i = 1 /* Skip TEVPREV */; i < info.loadsTevReg.size(); ++i) {
    if (info.loadsTevReg.test(i)) {
      uniBufAttrs += fmt::format("\n    tevreg{}: vec4f,", i - 1);
      fragmentFnPre += fmt::format("\n    var tevreg{0} = ubuf.tevreg{0};", i - 1);
    } else if (info.writesTevReg.test(i)) {
      fragmentFnPre += fmt::format("\n    var tevreg{0}: vec4f;", i - 1);
    }
  }

  if (info.lightingEnabled) {
    uniBufAttrs += fmt::format(FMT_STRING(R"""(
    lights: array<Light, {}>,
    lightState0: u32,
    lightState1: u32,
    lightState0a: u32,
    lightState1a: u32,)"""),
                               GX::MaxLights);
    uniformPre +=
        "\n"
        "struct Light {\n"
        "    pos: vec3f,\n"
        "    dir: vec3f,\n"
        "    color: vec4f,\n"
        "    cos_att: vec3f,\n"
        "    dist_att: vec3f,\n"
        "};";
    if (UsePerPixelLighting) {
      vtxOutAttrs += fmt::format("\n    @location({}) mv_pos: vec3f,", vtxOutIdx++);
      vtxOutAttrs += fmt::format("\n    @location({}) mv_nrm: vec3f,", vtxOutIdx++);
      vtxXfrAttrs += fmt::format(FMT_STRING(R"""(
    out.mv_pos = mv_pos;
    out.mv_nrm = mv_nrm;)"""));
    }
  }

  int vtxColorIdx = 0;
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (!info.sampledColorChannels.test(i)) {
      continue;
    }

    const auto& cc = config.colorChannels[i];
    const auto& cca = config.colorChannels[i + GX_ALPHA0];
    if (cc.lightingEnabled && cc.ambSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}_amb: vec4f,", i);
    }
    if (cc.matSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}_mat: vec4f,", i);
    }
    if (cca.lightingEnabled && cca.ambSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}a_amb: vec4f,", i);
    }
    if (cca.matSrc == GX_SRC_REG) {
      uniBufAttrs += fmt::format("\n    cc{0}a_mat: vec4f,", i);
    }

    // Output vertex color if necessary
    bool usesVtxColor = false;
    if (((cc.lightingEnabled && cc.ambSrc == GX_SRC_VTX) || cc.matSrc == GX_SRC_VTX ||
         (cca.lightingEnabled && cca.matSrc == GX_SRC_VTX) || cca.matSrc == GX_SRC_VTX)) {
      if (UsePerPixelLighting) {
        vtxOutAttrs += fmt::format("\n    @location({}) clr{}: vec4f,", vtxOutIdx++, vtxColorIdx);
        vtxXfrAttrs += fmt::format("\n    out.clr{} = {};", vtxColorIdx,
                                   vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + vtxColorIdx)));
      }
      usesVtxColor = true;
    }

    // TODO handle alpha lighting
    if (cc.lightingEnabled) {
      std::string ambSrc, matSrc, lightAttnFn, lightDiffFn;
      if (cc.ambSrc == GX_SRC_VTX) {
        if (UsePerPixelLighting) {
          ambSrc = fmt::format("in.clr{}", vtxColorIdx);
        } else {
          ambSrc = vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + vtxColorIdx));
        }
      } else if (cc.ambSrc == GX_SRC_REG) {
        ambSrc = fmt::format("ubuf.cc{0}_amb", i);
      }
      if (cc.matSrc == GX_SRC_VTX) {
        if (UsePerPixelLighting) {
          matSrc = fmt::format("in.clr{}", vtxColorIdx);
        } else {
          matSrc = vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + vtxColorIdx));
        }
      } else if (cc.matSrc == GX_SRC_REG) {
        matSrc = fmt::format("ubuf.cc{0}_mat", i);
      }
      GXDiffuseFn diffFn = cc.diffFn;
      if (cc.attnFn == GX_AF_NONE) {
        lightAttnFn = "attn = 1.0;";
      } else if (cc.attnFn == GX_AF_SPOT) {
        lightAttnFn = fmt::format(R"""(
          var cosine = max(0.0, dot(ldir, light.dir));
          var cos_attn = dot(light.cos_att, vec3f(1.0, cosine, cosine * cosine));
          var dist_attn = dot(light.dist_att, vec3f(1.0, dist, dist2));
          attn = max(0.0, cos_attn / dist_attn);)""");
      } else if (cc.attnFn == GX_AF_SPEC) {
        std::string normal = UsePerPixelLighting ? "in.mv_nrm" : "mv_nrm";
        std::string dist_attn = diffFn != GX_DF_NONE
                      ? "max(0.0, dot(normalize(light.dist_att), vec3f(1.0, attn, attn * attn)));"
                      : "max(0.0, dot(light.dist_att, vec3f(1.0, attn, attn * attn)));";
        lightAttnFn = fmt::format(R"""(
          attn = select(0.0, max(0.0, dot({0}, light.dir)), dot({0}, ldir) >= 0.0);
          var cos_attn = dot(light.cos_att, vec3f(1.0, attn, attn * attn));
          var dist_attn = {1};
          attn = max(0.0, cos_attn / dist_attn);)""", normal, dist_attn);
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
      std::string alphaSrc;
      if (cca.matSrc == GX_SRC_VTX) {
        if (UsePerPixelLighting) {
          alphaSrc = fmt::format("in.clr{}", vtxColorIdx);
        } else {
          alphaSrc = vtx_attr(config, static_cast<GXAttr>(GX_VA_CLR0 + vtxColorIdx));
        }
      } else {
        alphaSrc = fmt::format("ubuf.cc{0}a_mat", i);
      }

      std::string outVar, posVar;
      if (UsePerPixelLighting) {
        outVar = fmt::format("rast{}", i);
        posVar = "in.mv_pos";
      } else {
        outVar = fmt::format("out.cc{}", i);
        posVar = "mv_pos";
      }
      auto lightFunc = fmt::format(R"""(
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
      {6} = vec4f(({4} * clamp(lighting, vec4f(0.0), vec4f(1.0))).xyz, {8}.a);
    }})""",
                                   i, GX::MaxLights, lightAttnFn, lightDiffFn, matSrc, ambSrc, outVar, posVar, alphaSrc);
      if (UsePerPixelLighting) {
        fragmentFnPre += fmt::format("\n    var rast{}: vec4f;", i);
        fragmentFnPre += lightFunc;
      } else {
        vtxOutAttrs += fmt::format("\n    @location({}) cc{}: vec4f,", vtxOutIdx++, i);
        vtxXfrAttrs += lightFunc;
        fragmentFnPre += fmt::format("\n    var rast{0} = in.cc{0};", i);
      }
    } else if (cc.matSrc == GX_SRC_VTX) {
      if (UsePerPixelLighting) {
        // Color will already be written to clr{}
        fragmentFnPre += fmt::format("\n    var rast{0} = in.clr{0};", vtxColorIdx);
      } else {
        vtxOutAttrs += fmt::format("\n    @location({}) cc{}: vec4f,", vtxOutIdx++, i);
        vtxXfrAttrs += fmt::format("\n    out.cc{} = {};", i, vtx_attr(config, GXAttr(GX_VA_CLR0 + vtxColorIdx)));
        fragmentFnPre += fmt::format("\n    var rast{0} = in.cc{0};", i);
      }
    } else {
      fragmentFnPre += fmt::format("\n    var rast{0} = ubuf.cc{0}_mat;", i);
    }

    if (usesVtxColor) {
      ++vtxColorIdx;
    }
  }
  for (int i = 0; i < info.sampledKColors.size(); ++i) {
    if (info.sampledKColors.test(i)) {
      uniBufAttrs += fmt::format("\n    kcolor{}: vec4f,", i);
    }
  }
  for (int i = 0; i < info.sampledTexCoords.size(); ++i) {
    if (!info.sampledTexCoords.test(i)) {
      continue;
    }
    const auto& tcg = config.tcgs[i];
    vtxOutAttrs += fmt::format("\n    @location({}) tex{}_uv: vec2f,", vtxOutIdx++, i);
    if (tcg.src >= GX_TG_TEX0 && tcg.src <= GX_TG_TEX7) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f({}, 0.0, 1.0);", i,
                                 vtx_attr(config, GXAttr(GX_VA_TEX0 + (tcg.src - GX_TG_TEX0))));
    } else if (tcg.src == GX_TG_POS) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f(in_pos, 1.0);", i);
    } else if (tcg.src == GX_TG_NRM) {
      vtxXfrAttrs += fmt::format("\n    var tc{} = vec4f(in_nrm, 1.0);", i);
    } else
      UNLIKELY FATAL("unhandled tcg src {}", underlying(tcg.src));
    if (tcg.mtx == GX_IDENTITY) {
      vtxXfrAttrs += fmt::format("\n    var tc{0}_tmp = tc{0}.xyz;", i);
    } else {
      u32 texMtxIdx = (tcg.mtx - GX_TEXMTX0) / 3;
      vtxXfrAttrs += fmt::format("\n    var tc{0}_tmp = tc{0} * ubuf.texmtx{1};", i, texMtxIdx);
    }
    if (tcg.normalize) {
      vtxXfrAttrs += fmt::format("\n    tc{0}_tmp = normalize(tc{0}_tmp);", i);
    }
    if (tcg.postMtx == GX_PTIDENTITY) {
      vtxXfrAttrs += fmt::format("\n    var tc{0}_proj = tc{0}_tmp;", i);
    } else {
      u32 postMtxIdx = (tcg.postMtx - GX_PTTEXMTX0) / 3;
      vtxXfrAttrs += fmt::format("\n    var tc{0}_proj = vec4f(tc{0}_tmp.xyz, 1.0) * ubuf.postmtx{1};", i, postMtxIdx);
    }
    vtxXfrAttrs += fmt::format("\n    out.tex{0}_uv = tc{0}_proj.xy;", i);
  }
  for (int i = 0; i < config.tevStages.size(); ++i) {
    const auto& stage = config.tevStages[i];
    if (stage.texMapId == GX_TEXMAP_NULL ||
        stage.texCoordId == GX_TEXCOORD_NULL
        // TODO should check this per-stage probably
        || !info.sampledTextures.test(stage.texMapId)) {
      continue;
    }
    std::string uvIn = fmt::format("in.tex{0}_uv", underlying(stage.texCoordId));
    const auto& texConfig = config.textureConfig[stage.texMapId];
    if (is_palette_format(texConfig.loadFmt)) {
      std::string_view suffix;
      if (!is_palette_format(texConfig.copyFmt)) {
        switch (texConfig.loadFmt) {
          DEFAULT_FATAL("unimplemented palette format {}", texConfig.loadFmt);
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
      fragmentFnPre += fmt::format("\n    var sampled{0} = textureSamplePalette{3}(tex{1}, tex{1}_samp, {2}, tlut{1});",
                                   i, underlying(stage.texMapId), uvIn, suffix);
    } else {
      fragmentFnPre +=
          fmt::format("\n    var sampled{0} = textureSampleBias(tex{1}, tex{1}_samp, {2}, ubuf.tex{1}_lod);", i,
                      underlying(stage.texMapId), uvIn);
    }
    fragmentFnPre += texture_conversion(texConfig, i, stage.texMapId);
  }
  for (int i = 0; i < info.usesTexMtx.size(); ++i) {
    if (info.usesTexMtx.test(i)) {
      switch (info.texMtxTypes[i]) {
        DEFAULT_FATAL("unhandled tex mtx type {}", underlying(info.texMtxTypes[i]));
      case GX_TG_MTX2x4:
        uniBufAttrs += fmt::format("\n    texmtx{}: mat2x4f,", i);
        break;
      case GX_TG_MTX3x4:
        uniBufAttrs += fmt::format("\n    texmtx{}: mat3x4f,", i);
        break;
      }
    }
  }
  for (int i = 0; i < info.usesPTTexMtx.size(); ++i) {
    if (info.usesPTTexMtx.test(i)) {
      uniBufAttrs += fmt::format("\n    postmtx{}: mat3x4f,", i);
    }
  }
  if (info.usesFog) {
    uniformPre +=
        "\n"
        "struct Fog {\n"
        "    color: vec4f,\n"
        "    a: f32,\n"
        "    b: f32,\n"
        "    c: f32,\n"
        "    pad: f32,\n"
        "}";
    uniBufAttrs += "\n    fog: Fog,";

    fragmentFn +=
        fmt::format("\n    // Fog\n    var fogF = clamp((ubuf.fog.a / (ubuf.fog.b - {})) - ubuf.fog.c, 0.0, 1.0);",
                    UseReversedZ ? "(1.0 - in.pos.z)" : "in.pos.z");
    switch (config.fogType) {
      DEFAULT_FATAL("invalid fog type {}", underlying(config.fogType));
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
    fragmentFn += "\n    prev = vec4f(mix(prev.rgb, ubuf.fog.color.rgb, clamp(fogZ, 0.0, 1.0)), prev.a);";
  }
  size_t texBindIdx = 0;
  for (int i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    uniBufAttrs += fmt::format("\n    tex{}_lod: f32,", i);

    sampBindings += fmt::format(
        "\n@group(1) @binding({})\n"
        "var tex{}_samp: sampler;",
        texBindIdx, i);

    const auto& texConfig = config.textureConfig[i];
    if (is_palette_format(texConfig.loadFmt)) {
      texBindings += fmt::format(
          "\n@group(2) @binding({})\n"
          "var tex{}: texture_2d<{}>;",
          texBindIdx, i, is_palette_format(texConfig.copyFmt) ? "i32"sv : "f32"sv);
      ++texBindIdx;
      texBindings += fmt::format(
          "\n@group(2) @binding({})\n"
          "var tlut{}: texture_2d<f32>;",
          texBindIdx, i);
    } else {
      texBindings += fmt::format(
          "\n@group(2) @binding({})\n"
          "var tex{}: texture_2d<f32>;",
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
        DEFAULT_FATAL("invalid alpha compare op {}", underlying(config.alphaCompare.op));
      case GX_AOP_AND:
        fragmentFn += fmt::format("\n    if (!({} && {})) {{ discard; }}", comp0, comp1);
        break;
      case GX_AOP_OR:
        fragmentFn += fmt::format("\n    if (!({} || {})) {{ discard; }}", comp0, comp1);
        break;
      case GX_AOP_XOR:
        fragmentFn += fmt::format("\n    if (!({} ^^ {})) {{ discard; }}", comp0, comp1);
        break;
      case GX_AOP_XNOR:
        fragmentFn += fmt::format("\n    if (({} ^^ {})) {{ discard; }}", comp0, comp1);
        break;
      }
    }
  }
  if constexpr (EnableNormalVisualization) {
    fragmentFn += "\n    prev = vec4f(in.nrm, prev.a);";
  }

  const auto shaderSource = fmt::format(R"""(
fn fetch_f32_3(p: ptr<storage, array<f32>>, idx: u32) -> vec3<f32> {{
  var n = idx * 3;
  return vec3<f32>(p[n], p[n + 1], p[n + 2]);
}}
fn fetch_u8_2(p: ptr<storage, array<u32>>, idx: u32, frac: u32) -> vec2<f32> {{
  var v0 = p[idx / 2];
  var r = (idx % 2) != 0;
  var o0 = select(extractBits(v0, 0, 8), extractBits(v0, 16, 8), r);
  var o1 = select(extractBits(v0, 8, 8), extractBits(v0, 24, 8), r);
  return vec2<f32>(f32(o0), f32(o1)) / f32(1u << frac);
}}
fn fetch_i8_3(p: ptr<storage, array<i32>>, idx: u32, frac: u32) -> vec3<f32> {{
  let byte_idx = idx * 3u;
  let word0 = p[byte_idx / 4u];
  let word1 = p[(byte_idx + 1u) / 4u];
  let word2 = p[(byte_idx + 2u) / 4u];

  let shift0 = (byte_idx % 4u) * 8u;
  let shift1 = ((byte_idx + 1u) % 4u) * 8u;
  let shift2 = ((byte_idx + 2u) % 4u) * 8u;

  let o0 = extractBits(word0, shift0, 8);
  let o1 = extractBits(word1, shift1, 8);
  let o2 = extractBits(word2, shift2, 8);

  return vec3<f32>(f32(o0), f32(o1), f32(o2)) / f32(1u << frac);
}}
fn fetch_u16_2(p: ptr<storage, array<u32>>, idx: u32, frac: u32) -> vec2<f32> {{
  var v0 = p[idx];
  var o0 = extractBits(v0, 0, 16);
  var o1 = extractBits(v0, 16, 16);
  return vec2<f32>(f32(o0), f32(o1)) / f32(1u << frac);
}}
fn fetch_i16_3(p: ptr<storage, array<i32>>, idx: u32, frac: u32) -> vec3<f32> {{
  var n = idx * 3;
  var d = n / 2;
  var r = (n % 2) != 0;
  var v0 = p[d];
  var v1 = p[d + 1];
  var o0 = select(extractBits(v0, 0, 16), extractBits(v0, 16, 16), r);
  var o1 = select(extractBits(v0, 16, 16), extractBits(v1, 0, 16), r);
  var o2 = select(extractBits(v1, 0, 16), extractBits(v1, 16, 16), r);
  return vec3<f32>(f32(o0), f32(o1), f32(o2)) / f32(1u << frac);
}}
{10}
struct Uniform {{
    pos_mtx: mat3x4f,
    nrm_mtx: mat3x4f,
    proj: mat4x4f,{0}
}};
@group(0) @binding(0)
var<uniform> ubuf: Uniform;{3}{1}{2}

struct VertexOutput {{
    @builtin(position) pos: vec4f,{4}
}};

fn intensityF32(rgb: vec3f) -> f32 {{
    // RGB to intensity conversion
    // https://github.com/dolphin-emu/dolphin/blob/4cd48e609c507e65b95bca5afb416b59eaf7f683/Source/Core/VideoCommon/TextureConverterShaderGen.cpp#L237-L241
    return dot(rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0;
}}
fn intensityI4(rgb: vec3f) -> i32 {{
    return i32(intensityF32(rgb) * 16.f);
}}
fn textureSamplePalette(tex: texture_2d<i32>, samp: sampler, uv: vec2f, tlut: texture_2d<f32>) -> vec4f {{
    // Gather index values
    var i = textureGather(0, tex, samp, uv);
    // Load palette colors
    var c0 = textureLoad(tlut, vec2i(i[0], 0), 0);
    var c1 = textureLoad(tlut, vec2i(i[1], 0), 0);
    var c2 = textureLoad(tlut, vec2i(i[2], 0), 0);
    var c3 = textureLoad(tlut, vec2i(i[3], 0), 0);
    // Perform bilinear filtering
    var f = fract(uv * vec2f(textureDimensions(tex)) + 0.5);
    var t0 = mix(c3, c2, f.x);
    var t1 = mix(c0, c1, f.x);
    return mix(t0, t1, f.y);
}}
fn textureSamplePaletteI4(tex: texture_2d<f32>, samp: sampler, uv: vec2f, tlut: texture_2d<f32>) -> vec4f {{
    // Gather RGB channels
    var iR = textureGather(0, tex, samp, uv);
    var iG = textureGather(1, tex, samp, uv);
    var iB = textureGather(2, tex, samp, uv);
    // Perform intensity conversion
    var i0 = intensityI4(vec3f(iR[0], iG[0], iB[0]));
    var i1 = intensityI4(vec3f(iR[1], iG[1], iB[1]));
    var i2 = intensityI4(vec3f(iR[2], iG[2], iB[2]));
    var i3 = intensityI4(vec3f(iR[3], iG[3], iB[3]));
    // Load palette colors
    var c0 = textureLoad(tlut, vec2i(i0, 0), 0);
    var c1 = textureLoad(tlut, vec2i(i1, 0), 0);
    var c2 = textureLoad(tlut, vec2i(i2, 0), 0);
    var c3 = textureLoad(tlut, vec2i(i3, 0), 0);
    // Perform bilinear filtering
    var f = fract(uv * vec2f(textureDimensions(tex)) + 0.5);
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
fn fs_main(in: VertexOutput) -> @location(0) vec4f {{{8}{7}
    return prev;
}}
)""",
                                        uniBufAttrs, sampBindings, texBindings, uniformBindings, vtxOutAttrs,
                                        vtxInAttrs, vtxXfrAttrs, fragmentFn, fragmentFnPre, vtxXfrAttrsPre, uniformPre);
  if (EnableDebugPrints) {
    Log.info("Generated shader: {}", shaderSource);
  }

  wgpu::ShaderSourceWGSL wgslDescriptor{};
  wgslDescriptor.code = shaderSource.c_str();
  const auto label = fmt::format("GX Shader {:x}", hash);
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

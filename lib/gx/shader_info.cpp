#include "shader_info.hpp"

#include <cmath>

#include <tracy/Tracy.hpp>

namespace aurora::gx {
// TODO: remove, just for testing
bool enableLodBias = true;

namespace {
Module Log("aurora::gx");

bool is_alpha_bump_channel(GXChannelID id) { return id == GX_ALPHA_BUMP || id == GX_ALPHA_BUMPN; }

Vec4<float> texture_size_bias(const gfx::TextureBind& tex) {
  auto width = static_cast<float>(tex.texObj.width());
  auto height = static_cast<float>(tex.texObj.height());
  const auto vpBias =
      enableLodBias && tex.ref->hasArbitraryMips
          ? log2(std::min(g_gxState.renderViewport.width / std::max(g_gxState.logicalViewport.width, 1.f),
                          g_gxState.renderViewport.height / std::max(g_gxState.logicalViewport.height, 1.f)))
          : 0.f;
  return {width, height, tex.texObj.lod_bias() + vpBias, 0.0f};
}

void color_arg_reg_info(GXTevColorArg arg, const TevStage& stage, ShaderInfo& info) {
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
    if (stage.channelId != GX_COLOR_NULL && stage.channelId != GX_COLOR_ZERO &&
        !is_alpha_bump_channel(stage.channelId)) {
      info.sampledColorChannels.set(color_channel(stage.channelId));
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

void alpha_arg_reg_info(GXTevAlphaArg arg, const TevStage& stage, ShaderInfo& info) {
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
    if (stage.channelId != GX_COLOR_NULL && stage.channelId != GX_COLOR_ZERO &&
        !is_alpha_bump_channel(stage.channelId)) {
      info.sampledColorChannels.set(color_channel(stage.channelId));
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
} // namespace

ShaderInfo build_shader_info(const ShaderConfig& config) noexcept {
  ZoneScoped;

  ShaderInfo info{
      // vtx_start, current_pnmtx, render/logical viewport size, array_start, pad, proj
      .uniformSize = 4 + 4 + 8 + 8 + 8 + 48 + 64,
  };

  if (config.lineMode != 0) {
    info.uniformSize += 4 + 4 + 4 + 4; // line_width, line_aspect_y, line_tex_offset, line_texcoord_mask
    info.lineMode = config.lineMode;
  }

  for (int attr = 0; attr < config.attrs.size(); attr++) {
    const auto attrType = config.attrs[attr].attrType;
    if ((attr == GX_VA_PNMTXIDX || (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX7MTXIDX)) && attrType == GX_DIRECT) {
      info.indexAttr.set(attr);
    } else if (attrType == GX_INDEX8 || attrType == GX_INDEX16) {
      info.indexAttr.set(attr);
    }
  }

  // 10 position matrices, 10 texture matrices, 10 normal matrices.
  info.uniformSize += sizeof(Mat3x4<float>) * 30;
  info.uniformSize += 16; // active PN matrix index + padding

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
  for (int i = 0; i < config.tevStageCount; ++i) {
    const auto& stage = config.tevStages[i];
    // Skip if not enabled
    if (stage.indTexStage >= config.numIndStages) {
      continue;
    }
    const bool usesIndStage = stage.indTexMtxId != GX_ITM_OFF || is_alpha_bump_channel(stage.channelId);
    const bool usesTevTexCoord = stage.indTexMtxId != GX_ITM_OFF || stage.indTexWrapS != GX_ITW_OFF ||
                                 stage.indTexWrapT != GX_ITW_OFF || stage.indTexAddPrev;
    if (usesTevTexCoord && stage.texCoordId != GX_TEXCOORD_NULL) {
      info.sampledTexCoords.set(stage.texCoordId);
    }
    if (usesTevTexCoord && stage.texMapId != GX_TEXMAP_NULL &&
        (stage.indTexMtxId != GX_ITM_OFF || stage.indTexAddPrev)) {
      info.sampledTextures.set(stage.texMapId);
    }
    if (!usesIndStage) {
      continue;
    }
    info.usedIndStages.set(stage.indTexStage);
    const auto& indStage = config.indStages[stage.indTexStage];
    info.sampledTextures.set(indStage.texMapId);
    info.sampledTexCoords.set(indStage.texCoordId);
    info.sampledIndTextures.set(indStage.texMapId);
    // Track which indirect matrix is used
    if (stage.indTexMtxId >= GX_ITM_0 && stage.indTexMtxId <= GX_ITM_2) {
      info.usedIndTexMtxs.set(stage.indTexMtxId - GX_ITM_0);
    } else if (stage.indTexMtxId >= GX_ITM_S0 && stage.indTexMtxId <= GX_ITM_S2) {
      info.usedIndTexMtxs.set(stage.indTexMtxId - GX_ITM_S0);
    } else if (stage.indTexMtxId >= GX_ITM_T0 && stage.indTexMtxId <= GX_ITM_T2) {
      info.usedIndTexMtxs.set(stage.indTexMtxId - GX_ITM_T0);
    }
  }

  info.uniformSize += info.loadsTevReg.count() * sizeof(Vec4<float>);
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (info.sampledColorChannels.test(i)) {
      const auto& cc = config.colorChannels[i];
      const auto& cca = config.colorChannels[i + GX_ALPHA0];
      if (cc.lightingEnabled || cca.lightingEnabled) {
        info.lightingEnabled = true;
      }
    }
  }
  if (info.lightingEnabled) {
    // Lights + light state for all channels
    info.uniformSize += 16 + sizeof(Light) * GX::MaxLights;
  }
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (info.sampledColorChannels.test(i)) {
      const auto& cc = config.colorChannels[i];
      if (cc.lightingEnabled && cc.ambSrc == GX_SRC_REG) {
        info.uniformSize += sizeof(Vec4<float>);
      }
      if (cc.matSrc == GX_SRC_REG) {
        info.uniformSize += sizeof(Vec4<float>);
      }
      const auto& cca = config.colorChannels[i + GX_ALPHA0];
      if (cca.lightingEnabled && cca.ambSrc == GX_SRC_REG) {
        info.uniformSize += sizeof(Vec4<float>);
      }
      if (cca.matSrc == GX_SRC_REG) {
        info.uniformSize += sizeof(Vec4<float>);
      }
    }
  }
  info.uniformSize += info.sampledKColors.count() * sizeof(Vec4<float>);
  for (int i = 0; i < info.sampledTexCoords.size(); ++i) {
    if (!info.sampledTexCoords.test(i)) {
      continue;
    }
    const auto& tcg = config.tcgs[i];
    if (tcg.postMtx != GX_PTIDENTITY) {
      u32 postMtxIdx = (tcg.postMtx - GX_PTTEXMTX0) / 3;
      info.usesPTTexMtx.set(postMtxIdx);
    }
  }
  if (info.usesPTTexMtx.any())
    info.uniformSize += sizeof(Mat3x4<float>) * MaxPTTexMtx;
  if (config.fogType != GX_FOG_NONE) {
    info.usesFog = true;
    info.uniformSize += sizeof(Fog);
  }
  if (info.usedIndTexMtxs.any()) {
    info.uniformSize += MaxIndTexMtxs * sizeof(Mat2x4<float>);
  }
  info.uniformSize += info.sampledTextures.count() * sizeof(Vec4<float>);
  info.uniformSize = gfx::align_uniform(info.uniformSize);
  if (info.uniformSize > MaxUniformSize) {
    Log.fatal("Uniform size exceeds maximum: {} > {}", info.uniformSize, MaxUniformSize);
  }
  return info;
}

static f32 tex_offset(GXTexOffset offs) noexcept {
  switch (offs) {
    DEFAULT_FATAL("invalid tex offset {}", underlying(offs));
  case GX_TO_ZERO:
    return 0.f;
  case GX_TO_SIXTEENTH:
    return 1.f / 16.f;
  case GX_TO_EIGHTH:
    return 1.f / 8.f;
  case GX_TO_FOURTH:
    return 1.f / 4.f;
  case GX_TO_HALF:
    return 1.f / 2.f;
  case GX_TO_ONE:
    return 1.f;
  }
}

static u32 point_texcoord_mask() noexcept {
  u32 mask = 0;
  for (int i = 0; i < MaxTexCoord; ++i) {
    if (g_gxState.texCoordScales[i].pointOffset) {
      mask |= 1 << i;
    }
  }
  return mask;
}

static u32 line_texcoord_mask() noexcept {
  u32 mask = 0;
  for (int i = 0; i < MaxTexCoord; ++i) {
    if (g_gxState.texCoordScales[i].lineOffset) {
      mask |= 1 << i;
    }
  }
  return mask;
}

gfx::Range build_uniform(const ShaderInfo& info, u32 vtxStart, const BindGroupRanges& ranges) noexcept {
  ZoneScoped;

  auto [buf, range] = gfx::map_uniform(info.uniformSize);
  buf.append(vtxStart);
  buf.append(g_gxState.currentPnMtx);
  buf.append<f32>(g_gxState.renderViewport.width);
  buf.append<f32>(g_gxState.renderViewport.height);
  buf.append<f32>(g_gxState.logicalViewport.width);
  buf.append<f32>(g_gxState.logicalViewport.height);
  buf.append_zeroes(8); // pad
  for (const auto& vaRange : ranges.vaRanges) {
    buf.append<u32>(vaRange.offset);
  }
  if (info.lineMode != 0) {
    if (info.lineMode == 3) { // GX_POINTS
      buf.append<f32>(static_cast<f32>(g_gxState.pointSize) / 6.f);
      buf.append<f32>(1.0f);
      buf.append<f32>(tex_offset(g_gxState.pointTexOffset));
      buf.append<u32>(point_texcoord_mask());
    } else { // GX_LINES / GX_LINESTRIP
      buf.append<f32>(static_cast<f32>(g_gxState.lineWidth) / 6.f);
      buf.append<f32>(g_gxState.lineHalfAspect ? 0.5f : 1.f);
      buf.append<f32>(tex_offset(g_gxState.lineTexOffset));
      buf.append<u32>(line_texcoord_mask());
    }
  }
  buf.append(g_gxState.proj);

  for (int i = 0; i < MaxPnMtx; i++) {
    buf.append(g_gxState.pnMtx[i].pos);
  }

  for (int i = 0; i < MaxTexMtx; i++) {
    buf.append(g_gxState.texMtxs[i]);
  }

  for (int i = 0; i < MaxPnMtx; i++) {
    buf.append(g_gxState.pnMtx[i].nrm);
  }

  for (int i = 0; i < info.loadsTevReg.size(); ++i) {
    if (info.loadsTevReg.test(i)) {
      buf.append(g_gxState.colorRegs[i]);
    }
  }
  if (info.lightingEnabled) {
    // Lights
    static_assert(sizeof(g_gxState.lights) == 80 * GX::MaxLights);
    buf.append(g_gxState.lights);
    // Light state for all channels
    for (int i = 0; i < 4; ++i) {
      buf.append<u32>(g_gxState.colorChannelState[i].lightMask.to_ulong());
    }
  }
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (!info.sampledColorChannels.test(i)) {
      continue;
    }
    const auto& ccc = g_gxState.colorChannelConfig[i];
    const auto& ccs = g_gxState.colorChannelState[i];
    if (ccc.lightingEnabled && ccc.ambSrc == GX_SRC_REG) {
      buf.append(ccs.ambColor);
    }
    if (ccc.matSrc == GX_SRC_REG) {
      buf.append(ccs.matColor);
    }
    const auto& ccca = g_gxState.colorChannelConfig[i + GX_ALPHA0];
    const auto& ccsa = g_gxState.colorChannelState[i + GX_ALPHA0];
    if (ccca.lightingEnabled && ccca.ambSrc == GX_SRC_REG) {
      buf.append(ccsa.ambColor);
    }
    if (ccca.matSrc == GX_SRC_REG) {
      buf.append(ccsa.matColor);
    }
  }
  for (int i = 0; i < info.sampledKColors.size(); ++i) {
    if (info.sampledKColors.test(i)) {
      buf.append(g_gxState.kcolors[i]);
    }
  }
  if (info.usesPTTexMtx.any()) {
    for (int i = 0; i < info.usesPTTexMtx.size(); ++i) {
      buf.append(g_gxState.ptTexMtxs[i]);
    }
  }
  if (info.usesFog) {
    const auto& state = g_gxState.fog;
    Fog fog{.color = state.color, .a = state.a, .b = state.b, .c = state.c};
    buf.append(fog);
  }
  if (info.usedIndTexMtxs.any()) {
    for (int i = 0; i < MaxIndTexMtxs; ++i) {
      const auto& mtx = g_gxState.indTexMtxs[i];
      buf.append(Vec4{mtx.mtx.m0.x, mtx.mtx.m0.y, mtx.mtx.m1.x, mtx.mtx.m1.y});
      buf.append(Vec4{mtx.mtx.m2.x, mtx.mtx.m2.y, std::exp2f(mtx.scaleExp), 0.0f});
    }
  }
  for (int i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    const auto& tex = get_texture(static_cast<GXTexMapID>(i));
    // CHECK(tex, "unbound texture {}", i);
    buf.append(texture_size_bias(tex));
  }
  g_gxState.stateDirty = false;
  return range;
}
} // namespace aurora::gx

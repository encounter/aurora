#include "gx.hpp"

#include "../webgpu/gpu.hpp"
#include "../window.hpp"
#include "../internal.hpp"
#include "common.hpp"

#include <absl/container/flat_hash_map.h>
#include <cfloat>
#include <cmath>

using aurora::gfx::gx::g_gxState;
static aurora::Module Log("aurora::gx");

namespace aurora::gfx {
static Module Log("aurora::gfx::gx");

namespace gx {
using webgpu::g_device;
using webgpu::g_graphicsConfig;

GXState g_gxState{};

const TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[static_cast<size_t>(id)]; }

static inline WGPUBlendFactor to_blend_factor(GXBlendFactor fac, bool isDst) {
  switch (fac) {
  case GX_BL_ZERO:
    return WGPUBlendFactor_Zero;
  case GX_BL_ONE:
    return WGPUBlendFactor_One;
  case GX_BL_SRCCLR: // + GX_BL_DSTCLR
    if (isDst) {
      return WGPUBlendFactor_Src;
    } else {
      return WGPUBlendFactor_Dst;
    }
  case GX_BL_INVSRCCLR: // + GX_BL_INVDSTCLR
    if (isDst) {
      return WGPUBlendFactor_OneMinusSrc;
    } else {
      return WGPUBlendFactor_OneMinusDst;
    }
  case GX_BL_SRCALPHA:
    return WGPUBlendFactor_SrcAlpha;
  case GX_BL_INVSRCALPHA:
    return WGPUBlendFactor_OneMinusSrcAlpha;
  case GX_BL_DSTALPHA:
    return WGPUBlendFactor_DstAlpha;
  case GX_BL_INVDSTALPHA:
    return WGPUBlendFactor_OneMinusDstAlpha;
  default:
    Log.report(LOG_FATAL, FMT_STRING("invalid blend factor {}"), fac);
    unreachable();
  }
}

static inline WGPUCompareFunction to_compare_function(GXCompare func) {
  switch (func) {
  case GX_NEVER:
    return WGPUCompareFunction_Never;
  case GX_LESS:
    return WGPUCompareFunction_Less;
  case GX_EQUAL:
    return WGPUCompareFunction_Equal;
  case GX_LEQUAL:
    return WGPUCompareFunction_LessEqual;
  case GX_GREATER:
    return WGPUCompareFunction_Greater;
  case GX_NEQUAL:
    return WGPUCompareFunction_NotEqual;
  case GX_GEQUAL:
    return WGPUCompareFunction_GreaterEqual;
  case GX_ALWAYS:
    return WGPUCompareFunction_Always;
  default:
    Log.report(LOG_FATAL, FMT_STRING("invalid depth fn {}"), func);
    unreachable();
  }
}

static inline WGPUBlendState to_blend_state(GXBlendMode mode, GXBlendFactor srcFac, GXBlendFactor dstFac, GXLogicOp op,
                                            u32 dstAlpha) {
  WGPUBlendComponent colorBlendComponent;
  switch (mode) {
  case GX_BM_NONE:
    colorBlendComponent = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_Zero,
    };
    break;
  case GX_BM_BLEND:
    colorBlendComponent = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = to_blend_factor(srcFac, false),
        .dstFactor = to_blend_factor(dstFac, true),
    };
    break;
  case GX_BM_SUBTRACT:
    colorBlendComponent = {
        .operation = WGPUBlendOperation_ReverseSubtract,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_One,
    };
    break;
  case GX_BM_LOGIC:
    switch (op) {
    case GX_LO_CLEAR:
      colorBlendComponent = {
          .operation = WGPUBlendOperation_Add,
          .srcFactor = WGPUBlendFactor_Zero,
          .dstFactor = WGPUBlendFactor_Zero,
      };
      break;
    case GX_LO_COPY:
      colorBlendComponent = {
          .operation = WGPUBlendOperation_Add,
          .srcFactor = WGPUBlendFactor_One,
          .dstFactor = WGPUBlendFactor_Zero,
      };
      break;
    case GX_LO_NOOP:
      colorBlendComponent = {
          .operation = WGPUBlendOperation_Add,
          .srcFactor = WGPUBlendFactor_Zero,
          .dstFactor = WGPUBlendFactor_One,
      };
      break;
    default:
      Log.report(LOG_FATAL, FMT_STRING("unsupported logic op {}"), op);
      unreachable();
    }
    break;
  default:
    Log.report(LOG_FATAL, FMT_STRING("unsupported blend mode {}"), mode);
    unreachable();
  }
  WGPUBlendComponent alphaBlendComponent{
      .operation = WGPUBlendOperation_Add,
      .srcFactor = WGPUBlendFactor_One,
      .dstFactor = WGPUBlendFactor_Zero,
  };
  if (dstAlpha != UINT32_MAX) {
    alphaBlendComponent = WGPUBlendComponent{
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_Constant,
        .dstFactor = WGPUBlendFactor_Zero,
    };
  }
  return {
      .color = colorBlendComponent,
      .alpha = alphaBlendComponent,
  };
}

static inline WGPUColorWriteMaskFlags to_write_mask(bool colorUpdate, bool alphaUpdate) {
  WGPUColorWriteMaskFlags writeMask = WGPUColorWriteMask_None;
  if (colorUpdate) {
    writeMask |= WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue;
  }
  if (alphaUpdate) {
    writeMask |= WGPUColorWriteMask_Alpha;
  }
  return writeMask;
}

static inline WGPUPrimitiveState to_primitive_state(GXPrimitive gx_prim, GXCullMode gx_cullMode) {
  WGPUPrimitiveTopology primitive = WGPUPrimitiveTopology_TriangleList;
  switch (gx_prim) {
  case GX_TRIANGLES:
    break;
  case GX_TRIANGLESTRIP:
    primitive = WGPUPrimitiveTopology_TriangleStrip;
    break;
  default:
    Log.report(LOG_FATAL, FMT_STRING("Unsupported primitive type {}"), gx_prim);
    unreachable();
  }
  WGPUCullMode cullMode = WGPUCullMode_None;
  switch (gx_cullMode) {
  case GX_CULL_FRONT:
    cullMode = WGPUCullMode_Front;
    break;
  case GX_CULL_BACK:
    cullMode = WGPUCullMode_Back;
    break;
  case GX_CULL_NONE:
    break;
  default:
    Log.report(LOG_FATAL, FMT_STRING("Unsupported cull mode {}"), gx_cullMode);
    unreachable();
  }
  return {
      .topology = primitive,
      .frontFace = WGPUFrontFace_CW,
      .cullMode = cullMode,
  };
}

WGPURenderPipeline build_pipeline(const PipelineConfig& config, const ShaderInfo& info,
                                  ArrayRef<WGPUVertexBufferLayout> vtxBuffers, WGPUShaderModule shader,
                                  const char* label) noexcept {
  const WGPUDepthStencilState depthStencil{
      .format = g_graphicsConfig.depthFormat,
      .depthWriteEnabled = config.depthUpdate,
      .depthCompare = to_compare_function(config.depthFunc),
      .stencilFront =
          WGPUStencilFaceState{
              .compare = WGPUCompareFunction_Always,
          },
      .stencilBack =
          WGPUStencilFaceState{
              .compare = WGPUCompareFunction_Always,
          },
  };
  const auto blendState =
      to_blend_state(config.blendMode, config.blendFacSrc, config.blendFacDst, config.blendOp, config.dstAlpha);
  const std::array colorTargets{WGPUColorTargetState{
      .format = g_graphicsConfig.colorFormat,
      .blend = &blendState,
      .writeMask = to_write_mask(config.colorUpdate, config.alphaUpdate),
  }};
  const WGPUFragmentState fragmentState{
      .module = shader,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  auto layouts = build_bind_group_layouts(info, config.shaderConfig);
  const std::array bindGroupLayouts{
      layouts.uniformLayout,
      layouts.samplerLayout,
      layouts.textureLayout,
  };
  const WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{
      .label = "GX Pipeline Layout",
      .bindGroupLayoutCount = static_cast<uint32_t>(info.sampledTextures.any() ? bindGroupLayouts.size() : 1),
      .bindGroupLayouts = bindGroupLayouts.data(),
  };
  auto pipelineLayout = wgpuDeviceCreatePipelineLayout(g_device, &pipelineLayoutDescriptor);
  const WGPURenderPipelineDescriptor descriptor{
      .label = label,
      .layout = pipelineLayout,
      .vertex =
          {
              .module = shader,
              .entryPoint = "vs_main",
              .bufferCount = static_cast<uint32_t>(vtxBuffers.size()),
              .buffers = vtxBuffers.data(),
          },
      .primitive = to_primitive_state(config.primitive, config.cullMode),
      .depthStencil = &depthStencil,
      .multisample =
          WGPUMultisampleState{
              .count = g_graphicsConfig.msaaSamples,
              .mask = UINT32_MAX,
          },
      .fragment = &fragmentState,
  };
  auto pipeline = wgpuDeviceCreateRenderPipeline(g_device, &descriptor);
  wgpuPipelineLayoutRelease(pipelineLayout);
  return pipeline;
}

void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive) noexcept {
  config.shaderConfig.fogType = g_gxState.fog.type;
  config.shaderConfig.vtxAttrs = g_gxState.vtxDesc;
  int lastIndexedAttr = -1;
  for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
    const auto type = g_gxState.vtxDesc[i];
    if (type != GX_INDEX8 && type != GX_INDEX16) {
      config.shaderConfig.attrMapping[i] = GX_VA_NULL;
      continue;
    }
    const auto& array = g_gxState.arrays[i];
    if (lastIndexedAttr >= 0 && array == g_gxState.arrays[lastIndexedAttr]) {
      // Map attribute to previous attribute
      config.shaderConfig.attrMapping[i] = config.shaderConfig.attrMapping[lastIndexedAttr];
    } else {
      // Map attribute to its own storage
      config.shaderConfig.attrMapping[i] = static_cast<GXAttr>(i);
    }
    lastIndexedAttr = i;
  }
  config.shaderConfig.tevSwapTable = g_gxState.tevSwapTable;
  for (u8 i = 0; i < g_gxState.numTevStages; ++i) {
    config.shaderConfig.tevStages[i] = g_gxState.tevStages[i];
  }
  config.shaderConfig.tevStageCount = g_gxState.numTevStages;
  for (u8 i = 0; i < g_gxState.numChans * 2; ++i) {
    const auto& cc = g_gxState.colorChannelConfig[i];
    if (cc.lightingEnabled) {
      config.shaderConfig.colorChannels[i] = cc;
    } else {
      // Only matSrc matters when lighting disabled
      config.shaderConfig.colorChannels[i] = {
          .matSrc = cc.matSrc,
      };
    }
  }
  for (u8 i = 0; i < g_gxState.numTexGens; ++i) {
    config.shaderConfig.tcgs[i] = g_gxState.tcgs[i];
  }
  if (g_gxState.alphaCompare) {
    config.shaderConfig.alphaCompare = g_gxState.alphaCompare;
  }
  config.shaderConfig.indexedAttributeCount =
      std::count_if(config.shaderConfig.vtxAttrs.begin(), config.shaderConfig.vtxAttrs.end(),
                    [](const auto type) { return type == GX_INDEX8 || type == GX_INDEX16; });
  for (u8 i = 0; i < MaxTextures; ++i) {
    const auto& bind = g_gxState.textures[i];
    TextureConfig texConfig{};
    if (bind.texObj.ref) {
      if (requires_copy_conversion(bind.texObj)) {
        texConfig.copyFmt = bind.texObj.ref->gxFormat;
      }
      if (requires_load_conversion(bind.texObj)) {
        texConfig.loadFmt = bind.texObj.fmt;
      }
      texConfig.renderTex = bind.texObj.ref->isRenderTexture;
    }
    config.shaderConfig.textureConfig[i] = texConfig;
  }
  config = {
      .shaderConfig = config.shaderConfig,
      .primitive = primitive,
      .depthFunc = g_gxState.depthFunc,
      .cullMode = g_gxState.cullMode,
      .blendMode = g_gxState.blendMode,
      .blendFacSrc = g_gxState.blendFacSrc,
      .blendFacDst = g_gxState.blendFacDst,
      .blendOp = g_gxState.blendOp,
      .dstAlpha = g_gxState.dstAlpha,
      .depthCompare = g_gxState.depthCompare,
      .depthUpdate = g_gxState.depthUpdate,
      .alphaUpdate = g_gxState.alphaUpdate,
      .colorUpdate = g_gxState.colorUpdate,
  };
}

Range build_uniform(const ShaderInfo& info) noexcept {
  auto [buf, range] = map_uniform(info.uniformSize);
  {
    buf.append(&g_gxState.pnMtx[g_gxState.currentPnMtx], 128);
    buf.append(&g_gxState.proj, 64);
  }
  for (int i = 0; i < info.loadsTevReg.size(); ++i) {
    if (!info.loadsTevReg.test(i)) {
      continue;
    }
    buf.append(&g_gxState.colorRegs[i], 16);
  }
  bool lightingEnabled = false;
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (!info.sampledColorChannels.test(i)) {
      continue;
    }
    const auto& ccc = g_gxState.colorChannelConfig[i * 2];
    const auto& ccca = g_gxState.colorChannelConfig[i * 2 + 1];
    if (ccc.lightingEnabled || ccca.lightingEnabled) {
      lightingEnabled = true;
      break;
    }
  }
  if (lightingEnabled) {
    // Lights
    static_assert(sizeof(g_gxState.lights) == 80 * GX::MaxLights);
    buf.append(&g_gxState.lights, 80 * GX::MaxLights);
    // Light state for all channels
    for (int i = 0; i < 4; ++i) {
      u32 lightState = g_gxState.colorChannelState[i].lightMask.to_ulong();
      buf.append(&lightState, 4);
    }
  }
  for (int i = 0; i < info.sampledColorChannels.size(); ++i) {
    if (!info.sampledColorChannels.test(i)) {
      continue;
    }
    const auto& ccc = g_gxState.colorChannelConfig[i * 2];
    const auto& ccs = g_gxState.colorChannelState[i * 2];
    if (ccc.lightingEnabled && ccc.ambSrc == GX_SRC_REG) {
      buf.append(&ccs.ambColor, 16);
    }
    if (ccc.matSrc == GX_SRC_REG) {
      buf.append(&ccs.matColor, 16);
    }
    const auto& ccca = g_gxState.colorChannelConfig[i * 2 + 1];
    const auto& ccsa = g_gxState.colorChannelState[i * 2 + 1];
    if (ccca.lightingEnabled && ccca.ambSrc == GX_SRC_REG) {
      buf.append(&ccsa.ambColor, 16);
    }
    if (ccca.matSrc == GX_SRC_REG) {
      buf.append(&ccsa.matColor, 16);
    }
  }
  for (int i = 0; i < info.sampledKColors.size(); ++i) {
    if (!info.sampledKColors.test(i)) {
      continue;
    }
    buf.append(&g_gxState.kcolors[i], 16);
  }
  for (int i = 0; i < info.usesTexMtx.size(); ++i) {
    if (!info.usesTexMtx.test(i)) {
      continue;
    }
    const auto& state = g_gxState;
    switch (info.texMtxTypes[i]) {
    case GX_TG_MTX2x4:
      if (std::holds_alternative<Mat4x2<float>>(state.texMtxs[i])) {
        buf.append(&std::get<Mat4x2<float>>(state.texMtxs[i]), 32);
      } else if (std::holds_alternative<Mat4x4<float>>(g_gxState.texMtxs[i])) {
        // TODO: SMB hits this?
        Mat4x2<float> mtx{
            {1.f, 0.f},
            {0.f, 1.f},
            {0.f, 0.f},
            {0.f, 0.f},
        };
        buf.append(&mtx, 32);
      } else {
        Log.report(LOG_FATAL, FMT_STRING("expected 2x4 mtx in idx {}"), i);
        unreachable();
      }
      break;
    case GX_TG_MTX3x4:
      if (std::holds_alternative<Mat4x4<float>>(g_gxState.texMtxs[i])) {
        const auto& mat = std::get<Mat4x4<float>>(g_gxState.texMtxs[i]);
        buf.append(&mat, 64);
      } else {
        Log.report(LOG_FATAL, FMT_STRING("expected 3x4 mtx in idx {}"), i);
        buf.append(&Mat4x4_Identity, 64);
      }
      break;
    default:
      Log.report(LOG_FATAL, FMT_STRING("unhandled tex mtx type {}"), info.texMtxTypes[i]);
      unreachable();
    }
  }
  for (int i = 0; i < info.usesPTTexMtx.size(); ++i) {
    if (!info.usesPTTexMtx.test(i)) {
      continue;
    }
    buf.append(&g_gxState.ptTexMtxs[i], 64);
  }
  if (info.usesFog) {
    const auto& state = g_gxState.fog;
    struct Fog {
      Vec4<float> color = state.color;
      float a = 0.f;
      float b = 0.5f;
      float c = 0.f;
      float pad = FLT_MAX;
    } fog{};
    static_assert(sizeof(Fog) == 32);
    if (state.nearZ != state.farZ && state.startZ != state.endZ) {
      const float depthRange = state.farZ - state.nearZ;
      const float fogRange = state.endZ - state.startZ;
      fog.a = (state.farZ * state.nearZ) / (depthRange * fogRange);
      fog.b = state.farZ / depthRange;
      fog.c = state.startZ / fogRange;
    }
    buf.append(&fog, 32);
  }
  for (int i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    const auto& tex = get_texture(static_cast<GXTexMapID>(i));
    if (!tex) {
      Log.report(LOG_FATAL, FMT_STRING("unbound texture {}"), i);
      unreachable();
    }
    buf.append(&tex.texObj.lodBias, 4);
  }
  return range;
}

static absl::flat_hash_map<u32, WGPUBindGroupLayout> sUniformBindGroupLayouts;
static absl::flat_hash_map<u32, std::pair<WGPUBindGroupLayout, WGPUBindGroupLayout>> sTextureBindGroupLayouts;

GXBindGroups build_bind_groups(const ShaderInfo& info, const ShaderConfig& config,
                               const BindGroupRanges& ranges) noexcept {
  const auto layouts = build_bind_group_layouts(info, config);

  std::array<WGPUBindGroupEntry, GX_VA_MAX_ATTR + 1> uniformEntries{
      WGPUBindGroupEntry{
          .binding = 0,
          .buffer = g_uniformBuffer,
          .size = info.uniformSize,
      },
  };
  u32 uniformBindIdx = 1;
  for (u32 i = 0; i < GX_VA_MAX_ATTR; ++i) {
    const Range& range = ranges.vaRanges[i];
    if (range.size <= 0) {
      continue;
    }
    uniformEntries[uniformBindIdx] = WGPUBindGroupEntry{
        .binding = uniformBindIdx,
        .buffer = g_storageBuffer,
        .size = range.size,
    };
    ++uniformBindIdx;
  }

  std::array<WGPUBindGroupEntry, MaxTextures> samplerEntries;
  std::array<WGPUBindGroupEntry, MaxTextures * 2> textureEntries;
  u32 samplerCount = 0;
  u32 textureCount = 0;
  for (u32 i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    const auto& tex = g_gxState.textures[i];
    if (!tex) {
      Log.report(LOG_FATAL, FMT_STRING("unbound texture {}"), i);
      unreachable();
    }
    samplerEntries[samplerCount] = {
        .binding = samplerCount,
        .sampler = sampler_ref(tex.get_descriptor()),
    };
    ++samplerCount;
    textureEntries[textureCount] = {
        .binding = textureCount,
        .textureView = tex.texObj.ref->view,
    };
    ++textureCount;
    // Load palette
    const auto& texConfig = config.textureConfig[i];
    if (is_palette_format(texConfig.loadFmt)) {
      u32 tlut = tex.texObj.tlut;
      if (tlut < GX_TLUT0 || tlut > GX_TLUT7) {
        Log.report(LOG_FATAL, FMT_STRING("tlut out of bounds {}"), tlut);
        unreachable();
      } else if (!g_gxState.tluts[tlut].ref) {
        Log.report(LOG_FATAL, FMT_STRING("tlut unbound {}"), tlut);
        unreachable();
      }
      textureEntries[textureCount] = {
          .binding = textureCount,
          .textureView = g_gxState.tluts[tlut].ref->view,
      };
      ++textureCount;
    }
  }
  return {
      .uniformBindGroup = bind_group_ref(WGPUBindGroupDescriptor{
          .label = "GX Uniform Bind Group",
          .layout = layouts.uniformLayout,
          .entryCount = uniformBindIdx,
          .entries = uniformEntries.data(),
      }),
      .samplerBindGroup = bind_group_ref(WGPUBindGroupDescriptor{
          .label = "GX Sampler Bind Group",
          .layout = layouts.samplerLayout,
          .entryCount = samplerCount,
          .entries = samplerEntries.data(),
      }),
      .textureBindGroup = bind_group_ref(WGPUBindGroupDescriptor{
          .label = "GX Texture Bind Group",
          .layout = layouts.textureLayout,
          .entryCount = textureCount,
          .entries = textureEntries.data(),
      }),
  };
}

GXBindGroupLayouts build_bind_group_layouts(const ShaderInfo& info, const ShaderConfig& config) noexcept {
  GXBindGroupLayouts out;
  u32 uniformSizeKey = info.uniformSize + (config.indexedAttributeCount > 0 ? 1 : 0);
  const auto uniformIt = sUniformBindGroupLayouts.find(uniformSizeKey);
  if (uniformIt != sUniformBindGroupLayouts.end()) {
    out.uniformLayout = uniformIt->second;
  } else {
    std::array<WGPUBindGroupLayoutEntry, GX_VA_MAX_ATTR + 1> uniformLayoutEntries{
        WGPUBindGroupLayoutEntry{
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer =
                WGPUBufferBindingLayout{
                    .type = WGPUBufferBindingType_Uniform,
                    .hasDynamicOffset = true,
                    .minBindingSize = info.uniformSize,
                },
        },
    };
    u32 bindIdx = 1;
    for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
      if (config.attrMapping[i] == static_cast<GXAttr>(i)) {
        uniformLayoutEntries[bindIdx] = WGPUBindGroupLayoutEntry{
            .binding = bindIdx,
            .visibility = WGPUShaderStage_Vertex,
            .buffer =
                WGPUBufferBindingLayout{
                    .type = WGPUBufferBindingType_ReadOnlyStorage,
                    .hasDynamicOffset = true,
                },
        };
        ++bindIdx;
      }
    }
    const auto uniformLayoutDescriptor = WGPUBindGroupLayoutDescriptor{
        .label = "GX Uniform Bind Group Layout",
        .entryCount = bindIdx,
        .entries = uniformLayoutEntries.data(),
    };
    out.uniformLayout = wgpuDeviceCreateBindGroupLayout(g_device, &uniformLayoutDescriptor);
    //    sUniformBindGroupLayouts.try_emplace(uniformSizeKey, out.uniformLayout);
  }

  //  u32 textureCount = info.sampledTextures.count();
  //  const auto textureIt = sTextureBindGroupLayouts.find(textureCount);
  //  if (textureIt != sTextureBindGroupLayouts.end()) {
  //    const auto& [sl, tl] = textureIt->second;
  //    out.samplerLayout = sl;
  //    out.textureLayout = tl;
  //  } else {
  u32 numSamplers = 0;
  u32 numTextures = 0;
  std::array<WGPUBindGroupLayoutEntry, MaxTextures> samplerEntries;
  std::array<WGPUBindGroupLayoutEntry, MaxTextures * 2> textureEntries;
  for (u32 i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    const auto& texConfig = config.textureConfig[i];
    bool copyAsPalette = is_palette_format(texConfig.copyFmt);
    bool loadAsPalette = is_palette_format(texConfig.loadFmt);
    samplerEntries[numSamplers] = {
        .binding = numSamplers,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = {.type = copyAsPalette && loadAsPalette ? WGPUSamplerBindingType_NonFiltering
                                                           : WGPUSamplerBindingType_Filtering},
    };
    ++numSamplers;
    if (loadAsPalette) {
      textureEntries[numTextures] = {
          .binding = numTextures,
          .visibility = WGPUShaderStage_Fragment,
          .texture =
              {
                  .sampleType = copyAsPalette ? WGPUTextureSampleType_Sint : WGPUTextureSampleType_Float,
                  .viewDimension = WGPUTextureViewDimension_2D,
              },
      };
      ++numTextures;
      textureEntries[numTextures] = {
          .binding = numTextures,
          .visibility = WGPUShaderStage_Fragment,
          .texture =
              {
                  .sampleType = WGPUTextureSampleType_Float,
                  .viewDimension = WGPUTextureViewDimension_2D,
              },
      };
      ++numTextures;
    } else {
      textureEntries[numTextures] = {
          .binding = numTextures,
          .visibility = WGPUShaderStage_Fragment,
          .texture =
              {
                  .sampleType = WGPUTextureSampleType_Float,
                  .viewDimension = WGPUTextureViewDimension_2D,
              },
      };
      ++numTextures;
    }
  }
  {
    const WGPUBindGroupLayoutDescriptor descriptor{
        .label = "GX Sampler Bind Group Layout",
        .entryCount = numSamplers,
        .entries = samplerEntries.data(),
    };
    out.samplerLayout = wgpuDeviceCreateBindGroupLayout(g_device, &descriptor);
  }
  {
    const WGPUBindGroupLayoutDescriptor descriptor{
        .label = "GX Texture Bind Group Layout",
        .entryCount = numTextures,
        .entries = textureEntries.data(),
    };
    out.textureLayout = wgpuDeviceCreateBindGroupLayout(g_device, &descriptor);
  }
  //    sTextureBindGroupLayouts.try_emplace(textureCount, out.samplerLayout, out.textureLayout);
  //  }
  return out;
}

// TODO this is awkward
extern absl::flat_hash_map<ShaderRef, std::pair<WGPUShaderModule, gx::ShaderInfo>> g_gxCachedShaders;
void shutdown() noexcept {
  // TODO we should probably store this all in g_state.gx instead
  for (const auto& item : sUniformBindGroupLayouts) {
    wgpuBindGroupLayoutRelease(item.second);
  }
  sUniformBindGroupLayouts.clear();
  for (const auto& item : sTextureBindGroupLayouts) {
    wgpuBindGroupLayoutRelease(item.second.first);
    wgpuBindGroupLayoutRelease(item.second.second);
  }
  sTextureBindGroupLayouts.clear();
  for (auto& item : g_gxState.textures) {
    item.texObj.ref.reset();
  }
  for (auto& item : g_gxState.tluts) {
    item.ref.reset();
  }
  for (const auto& item : g_gxCachedShaders) {
    wgpuShaderModuleRelease(item.second.first);
  }
  g_gxCachedShaders.clear();
}
} // namespace gx

static WGPUAddressMode wgpu_address_mode(GXTexWrapMode mode) {
  switch (mode) {
  case GX_CLAMP:
    return WGPUAddressMode_ClampToEdge;
  case GX_REPEAT:
    return WGPUAddressMode_Repeat;
  case GX_MIRROR:
    return WGPUAddressMode_MirrorRepeat;
  default:
    Log.report(LOG_FATAL, FMT_STRING("invalid wrap mode {}"), mode);
    unreachable();
  }
}
static std::pair<WGPUFilterMode, WGPUFilterMode> wgpu_filter_mode(GXTexFilter filter) {
  switch (filter) {
  case GX_NEAR:
    return {WGPUFilterMode_Nearest, WGPUFilterMode_Linear};
  case GX_LINEAR:
    return {WGPUFilterMode_Linear, WGPUFilterMode_Linear};
  case GX_NEAR_MIP_NEAR:
    return {WGPUFilterMode_Nearest, WGPUFilterMode_Nearest};
  case GX_LIN_MIP_NEAR:
    return {WGPUFilterMode_Linear, WGPUFilterMode_Nearest};
  case GX_NEAR_MIP_LIN:
    return {WGPUFilterMode_Nearest, WGPUFilterMode_Linear};
  case GX_LIN_MIP_LIN:
    return {WGPUFilterMode_Linear, WGPUFilterMode_Linear};
  default:
    Log.report(LOG_FATAL, FMT_STRING("invalid filter mode {}"), filter);
    unreachable();
  }
}
static u16 wgpu_aniso(GXAnisotropy aniso) {
  switch (aniso) {
  case GX_ANISO_1:
    return 1;
  case GX_ANISO_2:
    return std::max<u16>(webgpu::g_graphicsConfig.textureAnisotropy / 2, 1);
  case GX_ANISO_4:
    return std::max<u16>(webgpu::g_graphicsConfig.textureAnisotropy, 1);
  default:
    Log.report(LOG_FATAL, FMT_STRING("invalid aniso mode {}"), aniso);
    unreachable();
  }
}
WGPUSamplerDescriptor TextureBind::get_descriptor() const noexcept {
  if (gx::requires_copy_conversion(texObj) && gx::is_palette_format(texObj.ref->gxFormat)) {
    return {
        .label = "Generated Non-Filtering Sampler",
        .addressModeU = wgpu_address_mode(texObj.wrapS),
        .addressModeV = wgpu_address_mode(texObj.wrapT),
        .addressModeW = WGPUAddressMode_Repeat,
        .magFilter = WGPUFilterMode_Nearest,
        .minFilter = WGPUFilterMode_Nearest,
        .mipmapFilter = WGPUFilterMode_Nearest,
        .lodMinClamp = 0.f,
        .lodMaxClamp = 1000.f,
        .maxAnisotropy = 1,
    };
  }
  const auto [minFilter, mipFilter] = wgpu_filter_mode(texObj.minFilter);
  const auto [magFilter, _] = wgpu_filter_mode(texObj.magFilter);
  return {
      .label = "Generated Filtering Sampler",
      .addressModeU = wgpu_address_mode(texObj.wrapS),
      .addressModeV = wgpu_address_mode(texObj.wrapT),
      .addressModeW = WGPUAddressMode_Repeat,
      .magFilter = magFilter,
      .minFilter = minFilter,
      .mipmapFilter = mipFilter,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 1000.f,
      .maxAnisotropy = wgpu_aniso(texObj.maxAniso),
  };
}
} // namespace aurora::gfx

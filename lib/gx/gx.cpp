#include "gx.hpp"

#include "pipeline.hpp"
#include "../webgpu/gpu.hpp"
#include "../window.hpp"
#include "../internal.hpp"
#include "../gfx/common.hpp"
#include "../gfx/texture.hpp"

#include <absl/container/flat_hash_map.h>
#include <cfloat>
#include <mutex>

static aurora::Module Log("aurora::gx");

namespace aurora::gx {
using webgpu::g_device;
using webgpu::g_graphicsConfig;

GXState g_gxState{};

const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[static_cast<size_t>(id)]; }

static inline wgpu::BlendFactor to_blend_factor(GXBlendFactor fac, bool isDst) {
  switch (fac) {
  case GX_BL_ZERO:
    return wgpu::BlendFactor::Zero;
  case GX_BL_ONE:
    return wgpu::BlendFactor::One;
  case GX_BL_SRCCLR: // + GX_BL_DSTCLR
    if (isDst) {
      return wgpu::BlendFactor::Src;
    } else {
      return wgpu::BlendFactor::Dst;
    }
  case GX_BL_INVSRCCLR: // + GX_BL_INVDSTCLR
    if (isDst) {
      return wgpu::BlendFactor::OneMinusSrc;
    } else {
      return wgpu::BlendFactor::OneMinusDst;
    }
  case GX_BL_SRCALPHA:
    return wgpu::BlendFactor::SrcAlpha;
  case GX_BL_INVSRCALPHA:
    return wgpu::BlendFactor::OneMinusSrcAlpha;
  case GX_BL_DSTALPHA:
    return wgpu::BlendFactor::DstAlpha;
  case GX_BL_INVDSTALPHA:
    return wgpu::BlendFactor::OneMinusDstAlpha;
  default:
    Log.warn("invalid blend factor {}, using {}", underlying(fac), isDst ? "dst=Zero" : "src=One");
    return isDst ? wgpu::BlendFactor::Zero : wgpu::BlendFactor::One;
  }
}

static inline wgpu::CompareFunction to_compare_function(GXCompare func) {
  switch (func) {
  case GX_NEVER:
    return wgpu::CompareFunction::Never;
  case GX_LESS:
    return UseReversedZ ? wgpu::CompareFunction::Greater : wgpu::CompareFunction::Less;
  case GX_EQUAL:
    return wgpu::CompareFunction::Equal;
  case GX_LEQUAL:
    return UseReversedZ ? wgpu::CompareFunction::GreaterEqual : wgpu::CompareFunction::LessEqual;
  case GX_GREATER:
    return UseReversedZ ? wgpu::CompareFunction::Less : wgpu::CompareFunction::Greater;
  case GX_NEQUAL:
    return wgpu::CompareFunction::NotEqual;
  case GX_GEQUAL:
    return UseReversedZ ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::GreaterEqual;
  case GX_ALWAYS:
    return wgpu::CompareFunction::Always;
  default:
    Log.warn("invalid depth fn {}, using Always", underlying(func));
    return wgpu::CompareFunction::Always;
  }
}

static inline wgpu::BlendState to_blend_state(GXBlendMode mode, GXBlendFactor srcFac, GXBlendFactor dstFac,
                                              GXLogicOp op, u32 dstAlpha) {
  wgpu::BlendComponent colorBlendComponent{
      .operation = wgpu::BlendOperation::Add,
      .srcFactor = wgpu::BlendFactor::One,
      .dstFactor = wgpu::BlendFactor::Zero,
  };

  switch (mode) {
  case GX_BM_NONE:
    break;
  case GX_BM_BLEND:
    colorBlendComponent = {
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = to_blend_factor(srcFac, false),
        .dstFactor = to_blend_factor(dstFac, true),
    };
    break;
  case GX_BM_SUBTRACT:
    colorBlendComponent = {
        .operation = wgpu::BlendOperation::ReverseSubtract,
        .srcFactor = wgpu::BlendFactor::One,
        .dstFactor = wgpu::BlendFactor::One,
    };
    break;
  case GX_BM_LOGIC:
    switch (op) {
    case GX_LO_CLEAR:
      colorBlendComponent = {
          .operation = wgpu::BlendOperation::Add,
          .srcFactor = wgpu::BlendFactor::Zero,
          .dstFactor = wgpu::BlendFactor::Zero,
      };
      break;
    case GX_LO_COPY:
      colorBlendComponent = {
          .operation = wgpu::BlendOperation::Add,
          .srcFactor = wgpu::BlendFactor::One,
          .dstFactor = wgpu::BlendFactor::Zero,
      };
      break;
    case GX_LO_NOOP:
      colorBlendComponent = {
          .operation = wgpu::BlendOperation::Add,
          .srcFactor = wgpu::BlendFactor::Zero,
          .dstFactor = wgpu::BlendFactor::One,
      };
      break;
    default:
      Log.warn("unsupported logic op {}, using COPY", underlying(op));
      break;
    }
    break;
  default:
    Log.warn("unsupported blend mode {}, using NONE", underlying(mode));
    break;
  }

  wgpu::BlendComponent alphaBlendComponent{
      .operation = wgpu::BlendOperation::Add,
      .srcFactor = wgpu::BlendFactor::One,
      .dstFactor = wgpu::BlendFactor::Zero,
  };
  if (dstAlpha != UINT32_MAX) {
    // Avoid constant blend factor in PSO state for now; some D3D12 paths reject it with E_INVALIDARG.
    // We still keep dstAlpha value for logging/diagnostics and potential shader-side handling.
  }
  return {
      .color = colorBlendComponent,
      .alpha = alphaBlendComponent,
  };
}

static inline wgpu::ColorWriteMask to_write_mask(bool colorUpdate, bool alphaUpdate) {
  wgpu::ColorWriteMask writeMask = wgpu::ColorWriteMask::None;
  if (colorUpdate) {
    writeMask |= wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green | wgpu::ColorWriteMask::Blue;
  }
  if (alphaUpdate) {
    writeMask |= wgpu::ColorWriteMask::Alpha;
  }
  return writeMask;
}

static inline wgpu::PrimitiveState to_primitive_state(GXPrimitive gx_prim, GXCullMode gx_cullMode) {
  wgpu::PrimitiveTopology primitive = wgpu::PrimitiveTopology::TriangleList;
  switch (gx_prim) {
    DEFAULT_FATAL("unsupported primitive type {}", underlying(gx_prim));
  case GX_TRIANGLES:
    break;
  case GX_TRIANGLESTRIP:
    primitive = wgpu::PrimitiveTopology::TriangleStrip;
    break;
  }
  wgpu::CullMode cullMode = wgpu::CullMode::None;
  switch (gx_cullMode) {
    DEFAULT_FATAL("unsupported cull mode {}", underlying(gx_cullMode));
  case GX_CULL_FRONT:
    cullMode = wgpu::CullMode::Front;
    break;
  case GX_CULL_BACK:
    cullMode = wgpu::CullMode::Back;
    break;
  case GX_CULL_NONE:
    break;
  }
  return {
      .topology = primitive,
      .stripIndexFormat = primitive == wgpu::PrimitiveTopology::TriangleStrip ? wgpu::IndexFormat::Uint16
                                                                              : wgpu::IndexFormat::Undefined,
      .frontFace = wgpu::FrontFace::CW,
      .cullMode = cullMode,
  };
}

wgpu::RenderPipeline build_pipeline(const PipelineConfig& config, const ShaderInfo& info,
                                    ArrayRef<wgpu::VertexBufferLayout> vtxBuffers, wgpu::ShaderModule shader,
                                    const char* label) noexcept {
  const wgpu::DepthStencilState depthStencil{
      .format = g_graphicsConfig.depthFormat,
      .depthWriteEnabled = config.depthCompare && config.depthUpdate,
      .depthCompare = config.depthCompare ? to_compare_function(config.depthFunc) : wgpu::CompareFunction::Always,
  };
  const auto blendState =
      to_blend_state(config.blendMode, config.blendFacSrc, config.blendFacDst, config.blendOp, config.dstAlpha);
  const std::array colorTargets{wgpu::ColorTargetState{
      .format = g_graphicsConfig.surfaceConfiguration.format,
      .blend = &blendState,
      .writeMask = to_write_mask(config.colorUpdate, config.alphaUpdate),
  }};
  const wgpu::FragmentState fragmentState{
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
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{
      .label = "GX Pipeline Layout",
      .bindGroupLayoutCount = static_cast<uint32_t>(info.sampledTextures.any() ? bindGroupLayouts.size() : 1),
      .bindGroupLayouts = bindGroupLayouts.data(),
  };
  auto pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDescriptor);
  const wgpu::RenderPipelineDescriptor descriptor{
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
          wgpu::MultisampleState{
              .count = g_graphicsConfig.msaaSamples,
              .mask = UINT32_MAX,
          },
      .fragment = &fragmentState,
  };
  const auto uncapturedErrorCountBefore = webgpu::g_uncapturedErrorCount.load(std::memory_order_relaxed);
  auto pipeline = g_device.CreateRenderPipeline(&descriptor);
  const auto uncapturedErrorCountAfter = webgpu::g_uncapturedErrorCount.load(std::memory_order_relaxed);
  const bool uncapturedError = uncapturedErrorCountAfter != uncapturedErrorCountBefore;
  if (pipeline && !uncapturedError) {
    return pipeline;
  }

  Log.error(
      "GX pipeline create failed; retrying fallback. prim={} cull={} blendMode={} srcFac={} dstFac={} logicOp={} depthCmp={} depthFn={} depthUpd={} dstAlpha={} colorUpd={} alphaUpd={} tevStages={} sampled={} stride={} attrs={} msaa={} surfFmt={} depthFmt={} pipeline_valid={} uncaptured_error={}",
      underlying(config.primitive), underlying(config.cullMode), underlying(config.blendMode),
      underlying(config.blendFacSrc), underlying(config.blendFacDst), underlying(config.blendOp), config.depthCompare,
      underlying(config.depthFunc), config.depthUpdate, config.dstAlpha, config.colorUpdate, config.alphaUpdate,
      config.shaderConfig.tevStageCount, info.sampledTextures.count(),
      vtxBuffers.empty() ? 0u : static_cast<u32>(vtxBuffers[0].arrayStride),
      vtxBuffers.empty() ? 0u : static_cast<u32>(vtxBuffers[0].attributeCount), g_graphicsConfig.msaaSamples,
      underlying(g_graphicsConfig.surfaceConfiguration.format), underlying(g_graphicsConfig.depthFormat),
      static_cast<bool>(pipeline), uncapturedError);

  const std::array fallbackColorTargets{wgpu::ColorTargetState{
      .format = g_graphicsConfig.surfaceConfiguration.format,
      .writeMask = to_write_mask(config.colorUpdate, config.alphaUpdate),
  }};
  const wgpu::FragmentState fallbackFragmentState{
      .module = shader,
      .entryPoint = "fs_main",
      .targetCount = fallbackColorTargets.size(),
      .targets = fallbackColorTargets.data(),
  };
  const wgpu::PrimitiveState fallbackPrimitive{
      .topology = wgpu::PrimitiveTopology::TriangleList,
      .stripIndexFormat = wgpu::IndexFormat::Undefined,
      .frontFace = wgpu::FrontFace::CW,
      .cullMode = wgpu::CullMode::None,
  };
  const wgpu::RenderPipelineDescriptor fallbackDescriptor{
      .label = "GX Pipeline Fallback",
      .layout = pipelineLayout,
      .vertex =
          {
              .module = shader,
              .entryPoint = "vs_main",
              .bufferCount = static_cast<uint32_t>(vtxBuffers.size()),
              .buffers = vtxBuffers.data(),
          },
      .primitive = fallbackPrimitive,
      .depthStencil = nullptr,
      .multisample =
          wgpu::MultisampleState{
              .count = 1,
              .mask = UINT32_MAX,
          },
      .fragment = &fallbackFragmentState,
  };

  const auto fallbackErrorCountBefore = webgpu::g_uncapturedErrorCount.load(std::memory_order_relaxed);
  pipeline = g_device.CreateRenderPipeline(&fallbackDescriptor);
  const auto fallbackErrorCountAfter = webgpu::g_uncapturedErrorCount.load(std::memory_order_relaxed);
  const bool fallbackError = fallbackErrorCountAfter != fallbackErrorCountBefore;
  if (pipeline && !fallbackError) {
    Log.warn("Using GX fallback pipeline for this draw state");
    return pipeline;
  }
  if (pipeline && fallbackError) {
    Log.warn("GX fallback pipeline reported uncaptured error; discarding");
  }

  Log.error("GX fallback pipeline creation also failed");
  return {};
}

void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept {
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  config.shaderConfig.fogType = g_gxState.fog.type;
  config.shaderConfig.vtxAttrs = g_gxState.vtxDesc;
  for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
    const auto type = g_gxState.vtxDesc[i];
    if (type != GX_INDEX8 && type != GX_INDEX16) {
      config.shaderConfig.attrMapping[i] = {};
      continue;
    }
    const auto& attrFmt = vtxFmt.attrs[i];
    // Map attribute to its own storage
    config.shaderConfig.attrMapping[i] = StorageConfig{
        .attr = static_cast<GXAttr>(i),
        .cnt = attrFmt.cnt,
        .compType = attrFmt.type,
        .frac = attrFmt.frac,
        .le = attrFmt.le,
    };
  }
  config.shaderConfig.tevSwapTable = g_gxState.tevSwapTable;
  for (u8 i = 0; i < g_gxState.numTevStages; ++i) {
    config.shaderConfig.tevStages[i] = g_gxState.tevStages[i];
  }
  config.shaderConfig.tevStageCount = g_gxState.numTevStages;
  for (u8 i = 0; i < g_gxState.numIndStages; ++i) {
    config.shaderConfig.indStages[i] = g_gxState.indStages[i];
  }
  config.shaderConfig.numIndStages = g_gxState.numIndStages;
  for (u8 i = 0; i < gx::MaxColorChannels; ++i) {
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
  bool hasPnMtxIdx = config.shaderConfig.vtxAttrs[GX_VA_PNMTXIDX] == GX_DIRECT;
  config.shaderConfig.currentPnMtx = hasPnMtxIdx ? 0 : g_gxState.currentPnMtx;
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

static std::mutex sBindGroupLayoutMutex;
static absl::flat_hash_map<u32, wgpu::BindGroupLayout> sUniformBindGroupLayouts;
static absl::flat_hash_map<u32, std::pair<wgpu::BindGroupLayout, wgpu::BindGroupLayout>> sTextureBindGroupLayouts;

GXBindGroups build_bind_groups(const ShaderInfo& info, const ShaderConfig& config,
                               const BindGroupRanges& ranges) noexcept {
  const auto layouts = build_bind_group_layouts(info, config);

  std::array<wgpu::BindGroupEntry, GX_VA_MAX_ATTR + 1> uniformEntries;
  memset(&uniformEntries, 0, sizeof(uniformEntries));
  uniformEntries[0].binding = 0;
  uniformEntries[0].buffer = gfx::g_uniformBuffer;
  uniformEntries[0].size = info.uniformSize;
  u32 uniformBindIdx = 1;
  for (u32 i = 0; i < GX_VA_MAX_ATTR; ++i) {
    const gfx::Range& range = ranges.vaRanges[i];
    if (range.size <= 0) {
      continue;
    }
    wgpu::BindGroupEntry& entry = uniformEntries[uniformBindIdx];
    entry.binding = uniformBindIdx;
    entry.buffer = gfx::g_storageBuffer;
    entry.size = range.size;
    ++uniformBindIdx;
  }

  std::array<wgpu::BindGroupEntry, MaxTextures> samplerEntries;
  std::array<wgpu::BindGroupEntry, MaxTextures * 2> textureEntries;
  memset(&samplerEntries, 0, sizeof(samplerEntries));
  memset(&textureEntries, 0, sizeof(textureEntries));
  u32 samplerCount = 0;
  u32 textureCount = 0;
  for (u32 i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    const auto& tex = g_gxState.textures[i];
    CHECK(tex, "unbound texture {}", i);
    wgpu::BindGroupEntry& samplerEntry = samplerEntries[samplerCount];
    samplerEntry.binding = samplerCount;
    samplerEntry.size = wgpu::kWholeSize;
    samplerEntry.sampler = gfx::sampler_ref(tex.get_descriptor());
    ++samplerCount;
    wgpu::BindGroupEntry& textureEntry = textureEntries[textureCount];
    textureEntry.binding = textureCount;
    textureEntry.size = wgpu::kWholeSize;
    textureEntry.textureView = tex.texObj.ref->view;
    ++textureCount;
    // Load palette
    const auto& texConfig = config.textureConfig[i];
    if (is_palette_format(texConfig.loadFmt)) {
      u32 tlut = tex.texObj.tlut;
      CHECK(tlut >= GX_TLUT0 && tlut <= GX_BIGTLUT3, "tlut out of bounds {}", tlut);
      CHECK(g_gxState.tluts[tlut].ref, "tlut unbound {}", tlut);
      wgpu::BindGroupEntry& tlutEntry = textureEntries[textureCount];
      tlutEntry.binding = textureCount;
      tlutEntry.size = wgpu::kWholeSize;
      tlutEntry.textureView = g_gxState.tluts[tlut].ref->view;
      ++textureCount;
    }
  }
  const wgpu::BindGroupDescriptor uniformBindGroupDescriptor{
      .label = "GX Uniform Bind Group",
      .layout = layouts.uniformLayout,
      .entryCount = uniformBindIdx,
      .entries = uniformEntries.data(),
  };
  const wgpu::BindGroupDescriptor samplerBindGroupDescriptor{
      .label = "GX Sampler Bind Group",
      .layout = layouts.samplerLayout,
      .entryCount = samplerCount,
      .entries = samplerEntries.data(),
  };
  const wgpu::BindGroupDescriptor textureBindGroupDescriptor{
      .label = "GX Texture Bind Group",
      .layout = layouts.textureLayout,
      .entryCount = textureCount,
      .entries = textureEntries.data(),
  };
  return {
      .uniformBindGroup = gfx::bind_group_ref(uniformBindGroupDescriptor),
      .samplerBindGroup = gfx::bind_group_ref(samplerBindGroupDescriptor),
      .textureBindGroup = gfx::bind_group_ref(textureBindGroupDescriptor),
  };
}

GXBindGroupLayouts build_bind_group_layouts(const ShaderInfo& info, const ShaderConfig& config) noexcept {
  GXBindGroupLayouts out;

  Hasher uniformHasher;
  uniformHasher.update(info.uniformSize);
  uniformHasher.update(config.attrMapping);
  const auto uniformLayoutHash = uniformHasher.digest();
  {
    std::lock_guard lock{sBindGroupLayoutMutex};
    auto it = sUniformBindGroupLayouts.find(uniformLayoutHash);
    if (it != sUniformBindGroupLayouts.end()) {
      out.uniformLayout = it->second;
    }
  }
  if (!out.uniformLayout) {
    std::array<wgpu::BindGroupLayoutEntry, GX_VA_MAX_ATTR + 1> uniformLayoutEntries{
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = true,
                    .minBindingSize = info.uniformSize,
                },
        },
    };
    u32 bindIdx = 1;
    for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
      if (config.attrMapping[i].attr == static_cast<GXAttr>(i)) {
        uniformLayoutEntries[bindIdx] = wgpu::BindGroupLayoutEntry{
            .binding = bindIdx,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = true,
                },
        };
        ++bindIdx;
      }
    }
    const auto uniformLayoutDescriptor = wgpu::BindGroupLayoutDescriptor{
        .label = "GX Uniform Bind Group Layout",
        .entryCount = bindIdx,
        .entries = uniformLayoutEntries.data(),
    };
    out.uniformLayout = g_device.CreateBindGroupLayout(&uniformLayoutDescriptor);
    std::lock_guard lock{sBindGroupLayoutMutex};
    sUniformBindGroupLayouts.try_emplace(uniformLayoutHash, out.uniformLayout);
  }

  Hasher textureHasher;
  textureHasher.update(info.sampledTextures);
  textureHasher.update(config.textureConfig);
  const auto textureLayoutHash = textureHasher.digest();
  {
    std::lock_guard lock{sBindGroupLayoutMutex};
    auto it2 = sTextureBindGroupLayouts.find(textureLayoutHash);
    if (it2 != sTextureBindGroupLayouts.end()) {
      out.samplerLayout = it2->second.first;
      out.textureLayout = it2->second.second;
      return out;
    }
  }

  u32 numSamplers = 0;
  u32 numTextures = 0;
  std::array<wgpu::BindGroupLayoutEntry, MaxTextures> samplerEntries;
  std::array<wgpu::BindGroupLayoutEntry, MaxTextures * 2> textureEntries;
  for (u32 i = 0; i < info.sampledTextures.size(); ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }
    const auto& texConfig = config.textureConfig[i];
    bool copyAsPalette = is_palette_format(texConfig.copyFmt);
    bool loadAsPalette = is_palette_format(texConfig.loadFmt);
    samplerEntries[numSamplers] = {
        .binding = numSamplers,
        .visibility = wgpu::ShaderStage::Fragment,
        .sampler = {.type = copyAsPalette && loadAsPalette ? wgpu::SamplerBindingType::NonFiltering
                                                           : wgpu::SamplerBindingType::Filtering},
    };
    ++numSamplers;
    if (loadAsPalette) {
      textureEntries[numTextures] = {
          .binding = numTextures,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              {
                  .sampleType = copyAsPalette ? wgpu::TextureSampleType::Sint : wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      };
      ++numTextures;
      textureEntries[numTextures] = {
          .binding = numTextures,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              {
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      };
      ++numTextures;
    } else {
      textureEntries[numTextures] = {
          .binding = numTextures,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              {
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      };
      ++numTextures;
    }
  }
  {
    const wgpu::BindGroupLayoutDescriptor descriptor{
        .label = "GX Sampler Bind Group Layout",
        .entryCount = numSamplers,
        .entries = samplerEntries.data(),
    };
    out.samplerLayout = g_device.CreateBindGroupLayout(&descriptor);
  }
  {
    const wgpu::BindGroupLayoutDescriptor descriptor{
        .label = "GX Texture Bind Group Layout",
        .entryCount = numTextures,
        .entries = textureEntries.data(),
    };
    out.textureLayout = g_device.CreateBindGroupLayout(&descriptor);
  }
  {
    std::lock_guard lock{sBindGroupLayoutMutex};
    sTextureBindGroupLayouts.try_emplace(textureLayoutHash,
                                         std::make_pair(out.samplerLayout, out.textureLayout));
  }
  return out;
}

// TODO this is awkward
extern absl::flat_hash_map<gfx::ShaderRef, std::pair<wgpu::ShaderModule, gx::ShaderInfo>> g_gxCachedShaders;
void shutdown() noexcept {
  // TODO we should probably store this all in g_state.gx instead
  sUniformBindGroupLayouts.clear();
  sTextureBindGroupLayouts.clear();
  for (auto& item : g_gxState.textures) {
    item.texObj.ref.reset();
  }
  for (auto& item : g_gxState.tluts) {
    item.ref.reset();
  }
  g_gxCachedShaders.clear();
  g_gxState.copyTextures.clear();
}
} // namespace aurora::gx

static wgpu::AddressMode wgpu_address_mode(GXTexWrapMode mode) {
  switch (mode) {
    DEFAULT_FATAL("invalid wrap mode {}", underlying(mode));
  case GX_CLAMP:
    return wgpu::AddressMode::ClampToEdge;
  case GX_REPEAT:
    return wgpu::AddressMode::Repeat;
  case GX_MIRROR:
    return wgpu::AddressMode::MirrorRepeat;
  }
}
static std::pair<wgpu::FilterMode, wgpu::MipmapFilterMode> wgpu_filter_mode(GXTexFilter filter) {
  switch (filter) {
    DEFAULT_FATAL("invalid filter mode {}", static_cast<int>(filter));
  case GX_NEAR:
    return {wgpu::FilterMode::Nearest, wgpu::MipmapFilterMode::Linear};
  case GX_LINEAR:
    return {wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Linear};
  case GX_NEAR_MIP_NEAR:
    return {wgpu::FilterMode::Nearest, wgpu::MipmapFilterMode::Nearest};
  case GX_LIN_MIP_NEAR:
    return {wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Nearest};
  case GX_NEAR_MIP_LIN:
    return {wgpu::FilterMode::Nearest, wgpu::MipmapFilterMode::Linear};
  case GX_LIN_MIP_LIN:
    return {wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Linear};
  }
}
static u16 wgpu_aniso(GXAnisotropy aniso) {
  switch (aniso) {
    DEFAULT_FATAL("invalid aniso {}", static_cast<int>(aniso));
  case GX_ANISO_1:
    return 1;
  case GX_ANISO_2:
    return std::max<u16>(aurora::webgpu::g_graphicsConfig.textureAnisotropy / 2, 1);
  case GX_ANISO_4:
    return std::max<u16>(aurora::webgpu::g_graphicsConfig.textureAnisotropy, 1);
  }
}
wgpu::SamplerDescriptor aurora::gfx::TextureBind::get_descriptor() const noexcept {
  if (gx::requires_copy_conversion(texObj) && gx::is_palette_format(texObj.ref->gxFormat)) {
    return {
        .label = "Generated Non-Filtering Sampler",
        .addressModeU = wgpu_address_mode(texObj.wrapS),
        .addressModeV = wgpu_address_mode(texObj.wrapT),
        .addressModeW = wgpu::AddressMode::Repeat,
        .magFilter = wgpu::FilterMode::Nearest,
        .minFilter = wgpu::FilterMode::Nearest,
        .mipmapFilter = wgpu::MipmapFilterMode::Nearest,
        .maxAnisotropy = 1,
    };
  }
  const auto [minFilter, mipFilter] = wgpu_filter_mode(texObj.minFilter);
  const auto [magFilter, _] = wgpu_filter_mode(texObj.magFilter);
  return {
      .label = "Generated Filtering Sampler",
      .addressModeU = wgpu_address_mode(texObj.wrapS),
      .addressModeV = wgpu_address_mode(texObj.wrapT),
      .addressModeW = wgpu::AddressMode::Repeat,
      .magFilter = magFilter,
      .minFilter = minFilter,
      .mipmapFilter = mipFilter,
      .maxAnisotropy = wgpu_aniso(texObj.maxAniso),
  };
} // namespace aurora::gx





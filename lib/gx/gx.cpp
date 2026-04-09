#include "gx.hpp"

#include "pipeline.hpp"
#include "../webgpu/gpu.hpp"
#include "../window.hpp"
#include "../internal.hpp"
#include "../gfx/common.hpp"
#include "../gfx/texture.hpp"
#include "gx_fmt.hpp"

#include <absl/container/flat_hash_map.h>
#include <cfloat>
#include <mutex>

#include "tracy/Tracy.hpp"

static aurora::Module Log("aurora::gx");

namespace aurora::gx {
using webgpu::g_device;
using webgpu::g_graphicsConfig;

GXState g_gxState{};

static wgpu::Sampler sEmptySampler;
static wgpu::Texture sEmptyTexture;
static wgpu::TextureView sEmptyTextureView;

const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[static_cast<size_t>(id)]; }

void clear_copy_texture_cache() noexcept {
  g_gxState.copyTextures.clear();
  g_gxState.copyTextureCache.clear();
}

static inline wgpu::BlendFactor to_blend_factor(GXBlendFactor fac, bool isDst) {
  switch (fac) {
    DEFAULT_FATAL("invalid blend factor {}", underlying(fac));
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
  }
}

static inline wgpu::CompareFunction to_compare_function(GXCompare func) {
  switch (func) {
    DEFAULT_FATAL("invalid depth fn {}", underlying(func));
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
  }
}

static inline wgpu::BlendState to_blend_state(GXBlendMode mode, GXBlendFactor srcFac, GXBlendFactor dstFac,
                                              GXLogicOp op, u32 dstAlpha) {
  wgpu::BlendComponent colorBlendComponent;
  switch (mode) {
    DEFAULT_FATAL("unsupported blend mode {}", underlying(mode));
  case GX_BM_NONE:
    colorBlendComponent = {
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = wgpu::BlendFactor::One,
        .dstFactor = wgpu::BlendFactor::Zero,
    };
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
      DEFAULT_FATAL("unsupported logic op {}", underlying(op));
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
    }
    break;
  }
  wgpu::BlendComponent alphaBlendComponent;
  if (dstAlpha != UINT32_MAX) {
    alphaBlendComponent = wgpu::BlendComponent{
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = wgpu::BlendFactor::Constant,
        .dstFactor = wgpu::BlendFactor::Zero,
    };
  } else {
    alphaBlendComponent = colorBlendComponent;
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

static inline wgpu::PrimitiveState to_primitive_state(GXCullMode gx_cullMode) {
  auto cullMode = wgpu::CullMode::None;
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
      .topology = wgpu::PrimitiveTopology::TriangleList,
      .stripIndexFormat = wgpu::IndexFormat::Undefined,
      .frontFace = wgpu::FrontFace::CW,
      .cullMode = cullMode,
  };
}

wgpu::RenderPipeline build_pipeline(const PipelineConfig& config, ArrayRef<wgpu::VertexBufferLayout> vtxBuffers,
                                    wgpu::ShaderModule shader, const char* label) noexcept {
  ZoneScoped;
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
  auto layouts = build_bind_group_layouts(config.shaderConfig);
  const std::array bindGroupLayouts{
      layouts.uniformLayout,
      layouts.samplerLayout,
      layouts.textureLayout,
  };
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{
      .label = "GX Pipeline Layout",
      .bindGroupLayoutCount = bindGroupLayouts.size(),
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
      .primitive = to_primitive_state(config.cullMode),
      .depthStencil = &depthStencil,
      .multisample =
          wgpu::MultisampleState{
              .count = config.msaaSamples,
          },
      .fragment = &fragmentState,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}

u8 comp_type_size(GXAttr attr, GXCompType type) noexcept {
  switch (attr) {
  case GX_VA_PNMTXIDX:
  case GX_VA_TEX0MTXIDX:
  case GX_VA_TEX1MTXIDX:
  case GX_VA_TEX2MTXIDX:
  case GX_VA_TEX3MTXIDX:
  case GX_VA_TEX4MTXIDX:
  case GX_VA_TEX5MTXIDX:
  case GX_VA_TEX6MTXIDX:
  case GX_VA_TEX7MTXIDX:
    return 1;
  case GX_VA_CLR0:
  case GX_VA_CLR1:
    switch (type) {
    case GX_RGB565:
    case GX_RGBA4:
      return 2;
    case GX_RGB8:
    case GX_RGBA6:
      return 3;
    case GX_RGBX8:
    case GX_RGBA8:
      return 4;
    }
  default:
    switch (type) {
    case GX_U8:
    case GX_S8:
      return 1;
    case GX_U16:
    case GX_S16:
      return 2;
    case GX_F32:
      return 4;
    default:
      Log.fatal("comp_type_size: Unsupported component type {}", type);
    }
  }
}

u8 comp_cnt_count(GXAttr attr, GXCompCnt cnt) noexcept {
  switch (attr) {
  case GX_VA_PNMTXIDX:
  case GX_VA_TEX0MTXIDX:
  case GX_VA_TEX1MTXIDX:
  case GX_VA_TEX2MTXIDX:
  case GX_VA_TEX3MTXIDX:
  case GX_VA_TEX4MTXIDX:
  case GX_VA_TEX5MTXIDX:
  case GX_VA_TEX6MTXIDX:
  case GX_VA_TEX7MTXIDX:
    return 1;
  case GX_VA_POS:
    switch (cnt) {
    case GX_POS_XY:
      return 2;
    case GX_POS_XYZ:
      return 3;
    default:
      break;
    }
    break;
  case GX_VA_NRM:
    switch (cnt) {
    case GX_NRM_XYZ:
      return 3;
    default:
      break;
    }
    break;
  case GX_VA_CLR0:
  case GX_VA_CLR1:
    return 1;
  case GX_VA_TEX0:
  case GX_VA_TEX1:
  case GX_VA_TEX2:
  case GX_VA_TEX3:
  case GX_VA_TEX4:
  case GX_VA_TEX5:
  case GX_VA_TEX6:
  case GX_VA_TEX7:
    switch (cnt) {
    case GX_TEX_S:
      return 1;
    case GX_TEX_ST:
      return 2;
    default:
      break;
    }
    break;
  default:
    break;
  }
  Log.fatal("comp_cnt_count: Unsupported attr/cnt {} {}", attr, cnt);
}

void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept {
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  config.shaderConfig.fogType = g_gxState.fog.type;
  u8 vtxOffset = 0;
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    const auto attr = static_cast<GXAttr>(i);
    const auto type = g_gxState.vtxDesc[i];
    auto& mapping = config.shaderConfig.attrs[i];
    if (type == GX_NONE) {
      mapping = {};
      continue;
    }
    const auto& attrFmt = vtxFmt.attrs[i];
    const auto cnt = comp_cnt_count(attr, attrFmt.cnt);
    mapping = AttrConfig{
        .attrType = static_cast<u8>(type),
        .cnt = cnt,
        .compType = static_cast<u8>(attrFmt.type),
        .offset = vtxOffset,
        .stride = 0,
        .frac = attrFmt.frac,
        .le = false,
    };
    switch (type) {
    case GX_DIRECT: {
      vtxOffset += comp_type_size(attr, attrFmt.type) * cnt;
      break;
    }
    case GX_INDEX8:
      mapping.stride = g_gxState.arrays[i].stride;
      mapping.le = g_gxState.arrays[i].le;
      vtxOffset += 1;
      break;
    case GX_INDEX16:
      mapping.stride = g_gxState.arrays[i].stride;
      mapping.le = g_gxState.arrays[i].le;
      vtxOffset += 2;
      break;
    default:
      Log.fatal("populate_pipeline_config: Invalid vertex type {}", type);
    }
  }
  config.shaderConfig.vtxStride = vtxOffset;
  if (primitive == GX_LINES) {
    config.shaderConfig.lineMode = 1;
  } else if (primitive == GX_LINESTRIP) {
    config.shaderConfig.lineMode = 2;
  } else if (primitive == GX_POINTS) {
    config.shaderConfig.lineMode = 3;
  } else {
    config.shaderConfig.lineMode = 0;
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
  for (u8 i = 0; i < MaxColorChannels; ++i) {
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
  config = {
      .msaaSamples = gfx::get_sample_count(),
      .shaderConfig = config.shaderConfig,
      .depthFunc = g_gxState.depthFunc,
      .cullMode = config.shaderConfig.lineMode == 0 ? g_gxState.cullMode : GX_CULL_NONE,
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
static wgpu::BindGroupLayout sTextureBindGroupLayout;
static wgpu::BindGroupLayout sSamplerBindGroupLayout;

GXBindGroups build_bind_groups(const ShaderInfo& info, const ShaderConfig& config,
                               const BindGroupRanges& ranges) noexcept {
  const auto layouts = build_bind_group_layouts(config);

  // Using C WGPU types instead of C++ wrappers to avoid destructor overhead
  std::array<WGPUBindGroupEntry, MaxIndexAttr + 3> uniformEntries{};
  uniformEntries[0].binding = 0;
  uniformEntries[0].buffer = gfx::g_vertexBuffer.Get();
  uniformEntries[0].size = WGPU_WHOLE_SIZE;
  uniformEntries[1].binding = 1;
  uniformEntries[1].buffer = gfx::g_uniformBuffer.Get();
  uniformEntries[1].size = info.uniformSize;
  u32 uniformBindIdx = 2;
  for (u32 i = 0; i < MaxIndexAttr; ++i) {
    const gfx::Range& range = ranges.vaRanges[i];
    if (range.size <= 0) {
      continue;
    }
    WGPUBindGroupEntry& entry = uniformEntries[uniformBindIdx];
    entry.binding = uniformBindIdx;
    entry.buffer = gfx::g_storageBuffer.Get();
    entry.size = range.size;
    ++uniformBindIdx;
  }

  std::array<WGPUBindGroupEntry, MaxTextures> samplerEntries{};
  std::array<WGPUBindGroupEntry, MaxTextures> textureEntries{};
  u32 textureCount = 0;
  for (u32 i = 0; i < MaxTextures; ++i) {
    const auto& tex = g_gxState.textures[i];
    WGPUBindGroupEntry& samplerEntry = samplerEntries[textureCount];
    WGPUBindGroupEntry& textureEntry = textureEntries[textureCount];
    samplerEntry.binding = textureCount;
    textureEntry.binding = textureCount;
    if (tex && (info.sampledTextures[i] || info.sampledIndTextures[i])) {
      samplerEntry.sampler = gfx::sampler_ref(tex.get_descriptor()).Get();
      textureEntry.textureView = tex.texObj.ref->sampleTextureView.Get();
    } else {
      samplerEntry.sampler = sEmptySampler.Get();
      textureEntry.textureView = sEmptyTextureView.Get();
    }
    ++textureCount;
  }
  const WGPUBindGroupDescriptor uniformBindGroupDescriptor{
      .label = {"GX Uniform Bind Group", WGPU_STRLEN},
      .layout = layouts.uniformLayout.Get(),
      .entryCount = uniformBindIdx,
      .entries = uniformEntries.data(),
  };
  const WGPUBindGroupDescriptor samplerBindGroupDescriptor{
      .label = {"GX Sampler Bind Group", WGPU_STRLEN},
      .layout = layouts.samplerLayout.Get(),
      .entryCount = textureCount,
      .entries = samplerEntries.data(),
  };
  const WGPUBindGroupDescriptor textureBindGroupDescriptor{
      .label = {"GX Texture Bind Group", WGPU_STRLEN},
      .layout = layouts.textureLayout.Get(),
      .entryCount = textureCount,
      .entries = textureEntries.data(),
  };
  return {
      .uniformBindGroup = gfx::bind_group_ref(uniformBindGroupDescriptor),
      .samplerBindGroup = gfx::bind_group_ref(samplerBindGroupDescriptor),
      .textureBindGroup = gfx::bind_group_ref(textureBindGroupDescriptor),
  };
}

GXBindGroupLayouts build_bind_group_layouts(const ShaderConfig& config) noexcept {
  GXBindGroupLayouts out;

  Hasher uniformHasher;
  uniformHasher.update(config.attrs);
  const auto uniformLayoutHash = uniformHasher.digest();
  {
    std::lock_guard lock{sBindGroupLayoutMutex};
    auto it = sUniformBindGroupLayouts.find(uniformLayoutHash);
    if (it != sUniformBindGroupLayouts.end()) {
      out.uniformLayout = it->second;
    }
  }
  if (!out.uniformLayout) {
    std::array<wgpu::BindGroupLayoutEntry, MaxIndexAttr + 3> uniformLayoutEntries{
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                },
        },
        wgpu::BindGroupLayoutEntry{
            .binding = 1,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = true,
                },
        },
    };
    u32 bindIdx = 2;
    for (int i = GX_VA_POS; i <= GX_VA_TEX7; ++i) {
      const auto attrType = config.attrs[i].attrType;
      if (attrType == GX_INDEX8 || attrType == GX_INDEX16) {
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

  out.samplerLayout = sSamplerBindGroupLayout;
  out.textureLayout = sTextureBindGroupLayout;
  return out;
}

void initialize() noexcept {
  u32 numTextures = 0;
  std::array<wgpu::BindGroupLayoutEntry, MaxTextures> samplerEntries;
  std::array<wgpu::BindGroupLayoutEntry, MaxTextures> textureEntries;
  for (u32 i = 0; i < MaxTextures; ++i) {
    samplerEntries[numTextures] = {
        .binding = numTextures,
        .visibility = wgpu::ShaderStage::Fragment,
        .sampler = {.type = wgpu::SamplerBindingType::Filtering},
    };
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
  {
    const wgpu::BindGroupLayoutDescriptor descriptor{
        .label = "GX Sampler Bind Group Layout",
        .entryCount = numTextures,
        .entries = samplerEntries.data(),
    };
    sSamplerBindGroupLayout = g_device.CreateBindGroupLayout(&descriptor);
  }
  {
    const wgpu::BindGroupLayoutDescriptor descriptor{
        .label = "GX Texture Bind Group Layout",
        .entryCount = numTextures,
        .entries = textureEntries.data(),
    };
    sTextureBindGroupLayout = g_device.CreateBindGroupLayout(&descriptor);
  }
  {
    constexpr wgpu::SamplerDescriptor descriptor{.label = "Empty sampler"};
    sEmptySampler = gfx::sampler_ref(descriptor);
  }
  {
    constexpr wgpu::TextureDescriptor descriptor{
        .label = "Empty texture",
        .usage = wgpu::TextureUsage::TextureBinding,
        .size = {1, 1},
        .format = wgpu::TextureFormat::RGBA8Unorm,
    };
    sEmptyTexture = g_device.CreateTexture(&descriptor);
    sEmptyTextureView = sEmptyTexture.CreateView();
  }
}

// TODO this is awkward
extern std::mutex g_gxCachedShadersMutex;
extern absl::flat_hash_map<gfx::ShaderRef, std::pair<wgpu::ShaderModule, ShaderInfo>> g_gxCachedShaders;
void shutdown() noexcept {
  // TODO we should probably store this all in g_state.gx instead
  sSamplerBindGroupLayout = {};
  sTextureBindGroupLayout = {};
  {
    std::lock_guard lock{sBindGroupLayoutMutex};
    sUniformBindGroupLayouts.clear();
    sTextureBindGroupLayouts.clear();
  }
  for (auto& item : g_gxState.textures) {
    item.texObj.ref.reset();
  }
  for (auto& item : g_gxState.tluts) {
    item.ref.reset();
  }
  {
    std::lock_guard lock{g_gxCachedShadersMutex};
    g_gxCachedShaders.clear();
  }
  clear_copy_texture_cache();
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

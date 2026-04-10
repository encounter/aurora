#include "gx.hpp"

#include "pipeline.hpp"
#include "../webgpu/gpu.hpp"
#include "../internal.hpp"
#include "../gfx/common.hpp"
#include "../gfx/tex_palette_conv.hpp"
#include "../gfx/texture.hpp"
#include "../gfx/texture_convert.hpp"
#include "../gfx/texture_replacement.hpp"
#include "gx_fmt.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <cfloat>
#include <mutex>
#include <optional>

#include "tracy/Tracy.hpp"

static aurora::Module Log("aurora::gx");

namespace aurora::gx {
using webgpu::g_device;
using webgpu::g_graphicsConfig;

GXState g_gxState{};

static wgpu::Sampler sEmptySampler;
static wgpu::Texture sEmptyTexture;
static wgpu::TextureView sEmptyTextureView;

namespace {
struct DynamicPaletteKey {
  const void* sourceIdentity = nullptr;
  u32 width = 0;
  u32 height = 0;
  u32 format = 0;

  bool operator==(const DynamicPaletteKey& rhs) const = default;
  template <typename H>
  friend H AbslHashValue(H h, const DynamicPaletteKey& key) {
    return H::combine(std::move(h), key.sourceIdentity, key.width, key.height, key.format);
  }
};

struct DynamicPaletteEntry {
  gfx::TextureHandle handle;
  u32 sourceRevision = 0;
  u32 tlutDataVersion = 0;
};

struct CachedTextureEntry {
  gfx::TextureHandle handle;
  u32 texDataVersion = 0;
  u32 tlutObjId = 0;
  u32 tlutDataVersion = 0;
};

struct CachedTlutTextureEntry {
  gfx::TextureHandle handle;
  u32 tlutDataVersion = 0;
};

struct TlutObjectCache {
  CachedTlutTextureEntry tlutTexture;
  absl::flat_hash_map<DynamicPaletteKey, DynamicPaletteEntry> dynamicPaletteTextures;
  absl::flat_hash_set<u32> staticTextureUsers;
};

absl::flat_hash_map<u32, CachedTextureEntry> s_textureObjectCaches;
absl::flat_hash_map<u32, TlutObjectCache> s_tlutObjectCaches;

DynamicPaletteKey make_dynamic_palette_key(const GXTexObj_& obj, const GXState::CopyTextureRef& source) {
  return {
      .sourceIdentity = source.handle.get(),
      .width = obj.width(),
      .height = obj.height(),
      .format = obj.format(),
  };
}

void clear_texture_dependency(u32 texObjId, u32 tlutObjId) {
  if (texObjId == 0 || tlutObjId == 0) {
    return;
  }
  if (auto it = s_tlutObjectCaches.find(tlutObjId); it != s_tlutObjectCaches.end()) {
    it->second.staticTextureUsers.erase(texObjId);
    if (!it->second.tlutTexture.handle && it->second.dynamicPaletteTextures.empty() &&
        it->second.staticTextureUsers.empty()) {
      s_tlutObjectCaches.erase(it);
    }
  }
}

void store_cached_texture(const GXTexObj_& obj, gfx::TextureHandle handle, u32 tlutObjId = 0, u32 tlutDataVersion = 0) {
  if (obj.texObjId == 0) {
    return;
  }

  auto& entry = s_textureObjectCaches[obj.texObjId];
  if (entry.tlutObjId != tlutObjId) {
    clear_texture_dependency(obj.texObjId, entry.tlutObjId);
  }

  entry.handle = std::move(handle);
  entry.texDataVersion = obj.texDataVersion;
  entry.tlutObjId = tlutObjId;
  entry.tlutDataVersion = tlutDataVersion;

  if (tlutObjId != 0) {
    s_tlutObjectCaches[tlutObjId].staticTextureUsers.insert(obj.texObjId);
  }
}

u32 mip_count_for(const GXTexObj_& obj) {
  if (!obj.has_mips()) {
    return 1;
  }
  return std::max<u32>(static_cast<u32>(obj.max_lod()) + 1, 1u);
}

gfx::TextureHandle get_tlut_texture(const GXTlutObj_& tlut) {
  if (tlut.tlutObjId != 0) {
    auto& cache = s_tlutObjectCaches[tlut.tlutObjId];
    if (cache.tlutTexture.handle && cache.tlutTexture.tlutDataVersion == tlut.tlutDataVersion) {
      return cache.tlutTexture.handle;
    }
    cache.dynamicPaletteTextures.clear();
    for (const u32 texObjId : cache.staticTextureUsers) {
      s_textureObjectCaches.erase(texObjId);
    }
    cache.staticTextureUsers.clear();
  }

  const auto handle = gfx::new_static_texture_2d(
      tlut.numEntries, 1, 1, gfx::tlut_texture_format(tlut.format),
      {static_cast<const u8*>(tlut.data), static_cast<size_t>(tlut.numEntries) * sizeof(u16)}, true, "Loaded TLUT");
  if (tlut.tlutObjId != 0) {
    auto& cache = s_tlutObjectCaches[tlut.tlutObjId];
    cache.tlutTexture.handle = handle;
    cache.tlutTexture.tlutDataVersion = tlut.tlutDataVersion;
  }
  return handle;
}

std::optional<gfx::TextureHandle> try_resolve_replacement_handle(const GXTexObj_& obj) {
  return gfx::texture_replacement::find_replacement(obj);
}

gfx::TextureHandle resolve_static_texture(const GXTexObj_& obj) {
  if (const auto replacement = try_resolve_replacement_handle(obj); replacement.has_value()) {
    return *replacement;
  }

  if (obj.texObjId != 0) {
    if (const auto it = s_textureObjectCaches.find(obj.texObjId); it != s_textureObjectCaches.end()) {
      const auto& entry = it->second;
      if (entry.handle && entry.texDataVersion == obj.texDataVersion && entry.tlutObjId == 0) {
        return entry.handle;
      }
    }
  }

  const auto handle =
      gfx::new_static_texture_2d(obj.width(), obj.height(), mip_count_for(obj), obj.format(),
                                 {static_cast<const u8*>(obj.data), obj.dataSize}, false, "GX Static Texture");
  store_cached_texture(obj, handle);
  return handle;
}

gfx::TextureHandle resolve_static_palette_texture(const GXTexObj_& obj, const GXTlutObj_& tlut) {
  if (const auto replacement = try_resolve_replacement_handle(obj); replacement.has_value()) {
    return *replacement;
  }

  if (obj.texObjId != 0) {
    if (const auto it = s_textureObjectCaches.find(obj.texObjId); it != s_textureObjectCaches.end()) {
      const auto& entry = it->second;
      if (entry.handle && entry.texDataVersion == obj.texDataVersion && entry.tlutObjId == tlut.tlutObjId &&
          entry.tlutDataVersion == tlut.tlutDataVersion) {
        return entry.handle;
      }
    }
  }

  auto decoded = gfx::convert_texture_palette(
      obj.format(), obj.width(), obj.height(), mip_count_for(obj), {static_cast<const u8*>(obj.data), obj.dataSize},
      tlut.format, tlut.numEntries, {static_cast<const u8*>(tlut.data), static_cast<size_t>(tlut.numEntries) * 2});
  if (decoded.empty()) {
    return {};
  }
  const auto handle = gfx::new_static_texture_2d(obj.width(), obj.height(), mip_count_for(obj), GX_TF_RGBA8_PC,
                                                 {decoded.data(), decoded.size()}, false, "GX Static Palette Texture");
  store_cached_texture(obj, handle, tlut.tlutObjId, tlut.tlutDataVersion);
  return handle;
}

gfx::TextureHandle resolve_dynamic_palette_texture(const GXTexObj_& obj, const GXState::CopyTextureRef& source,
                                                   const GXTlutObj_& tlut) {
  const auto tlutHandle = get_tlut_texture(tlut);
  auto& tlutCache = s_tlutObjectCaches[tlut.tlutObjId];
  auto& entry = tlutCache.dynamicPaletteTextures[make_dynamic_palette_key(obj, source)];
  if (!entry.handle) {
    // Use source size instead of target (logical) size
    entry.handle = gfx::new_conv_texture(source.handle->size.width, source.handle->size.height, GX_TF_RGBA8,
                                         "GX Dynamic Palette Texture");
  }
  if (entry.sourceRevision != source.revision || entry.tlutDataVersion != tlut.tlutDataVersion) {
    gfx::queue_palette_conv({
        .variant = obj.format() == GX_TF_C4 ? gfx::tex_palette_conv::Variant::FromFloat4
                                            : gfx::tex_palette_conv::Variant::FromFloat8,
        .src = source.handle,
        .dst = entry.handle,
        .tlut = tlutHandle,
    });
    entry.sourceRevision = source.revision;
    entry.tlutDataVersion = tlut.tlutDataVersion;
  }
  return entry.handle;
}

u32 resolved_format_for_handle(const gfx::TextureHandle& handle) {
  if (!handle) {
    return GX_TF_RGBA8;
  }
  if (handle->gxFormat != gfx::InvalidTextureFormat) {
    return handle->gxFormat;
  }
  return GX_TF_RGBA8_PC;
}
} // namespace

const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[static_cast<size_t>(id)]; }

void evict_texture_object(u32 texObjId) noexcept {
  if (const auto it = s_textureObjectCaches.find(texObjId); it != s_textureObjectCaches.end()) {
    clear_texture_dependency(texObjId, it->second.tlutObjId);
    s_textureObjectCaches.erase(it);
  }
}

void evict_tlut_object(u32 tlutObjId) noexcept {
  if (const auto it = s_tlutObjectCaches.find(tlutObjId); it != s_tlutObjectCaches.end()) {
    for (const u32 texObjId : it->second.staticTextureUsers) {
      s_textureObjectCaches.erase(texObjId);
    }
    s_tlutObjectCaches.erase(it);
  }
}

void clear_copy_texture_cache() noexcept {
  g_gxState.copyTextures.clear();
  g_gxState.copyTextureCache.clear();
  for (auto& [_, cache] : s_tlutObjectCaches) {
    cache.dynamicPaletteTextures.clear();
  }
}

void resolve_sampled_textures(const ShaderInfo& info) noexcept {
  for (u32 i = 0; i < MaxTextures; ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }

    GXTexObj_ obj = g_gxState.loadedTextures[i];
    gfx::TextureHandle handle;
    const auto copyIt = g_gxState.copyTextures.find(obj.data);
    const GXState::CopyTextureRef* copyRef = copyIt != g_gxState.copyTextures.end() ? &copyIt->second : nullptr;
    if (is_palette_format(obj.format())) {
      const auto tlutIdx = static_cast<size_t>(obj.tlut);
      if (tlutIdx < g_gxState.loadedTluts.size()) {
        const auto& tlut = g_gxState.loadedTluts[tlutIdx];
        if (tlut.data != nullptr) {
          if (copyRef != nullptr) {
            handle = resolve_dynamic_palette_texture(obj, *copyRef, tlut);
          } else {
            handle = resolve_static_palette_texture(obj, tlut);
          }
        }
      }
    } else if (copyRef != nullptr) {
      handle = copyRef->handle;
    } else if (obj.data != nullptr) {
      handle = resolve_static_texture(obj);
    }

    obj.mFormat = resolved_format_for_handle(handle);
    g_gxState.textures[i] = gfx::TextureBind{obj, std::move(handle)};
  }
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
      textureEntry.textureView = tex.ref->sampleTextureView.Get();
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
    item.ref.reset();
  }
  {
    std::lock_guard lock{g_gxCachedShadersMutex};
    g_gxCachedShaders.clear();
  }
  s_textureObjectCaches.clear();
  s_tlutObjectCaches.clear();
  g_gxState.loadedTextures.fill({});
  g_gxState.loadedTluts.fill({});
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
  const auto [minFilter, mipFilter] = wgpu_filter_mode(texObj.min_filter());
  const auto [magFilter, _] = wgpu_filter_mode(texObj.mag_filter());
  return {
      .label = "Generated Filtering Sampler",
      .addressModeU = wgpu_address_mode(texObj.wrap_s()),
      .addressModeV = wgpu_address_mode(texObj.wrap_t()),
      .addressModeW = wgpu::AddressMode::Repeat,
      .magFilter = magFilter,
      .minFilter = minFilter,
      .mipmapFilter = mipFilter,
      .maxAnisotropy = wgpu_aniso(texObj.max_aniso()),
  };
} // namespace aurora::gx

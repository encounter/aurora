#include "gx.hpp"

#include "pipeline.hpp"
#include "../dolphin/vi/vi_internal.hpp"
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
#include <tracy/Tracy.hpp>

#include <cfloat>
#include <mutex>
#include <optional>

static aurora::Module Log("aurora::gx");

namespace aurora::gx {
using webgpu::g_device;
using webgpu::g_graphicsConfig;

GXState g_gxState{};

static wgpu::Sampler sEmptySampler;
static wgpu::Texture sEmptyTexture;
static wgpu::TextureView sEmptyTextureView;
static std::mutex sBindGroupLayoutMutex;
static absl::flat_hash_map<u32, wgpu::BindGroupLayout> sUniformBindGroupLayouts;
static absl::flat_hash_map<u32, std::pair<wgpu::BindGroupLayout, wgpu::BindGroupLayout>> sTextureBindGroupLayouts;
static wgpu::BindGroupLayout sTextureBindGroupLayout;
static wgpu::BindGroupLayout sSamplerBindGroupLayout;
static wgpu::PipelineLayout sPipelineLayout;
wgpu::BindGroup g_emptyTextureBindGroup;

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

gfx::TextureHandle resolve_static_texture(const GXTexObj_& obj) {
  ZoneScoped;

  if (obj.texObjId != 0) {
    if (const auto it = s_textureObjectCaches.find(obj.texObjId); it != s_textureObjectCaches.end()) {
      const auto& entry = it->second;
      if (entry.handle && entry.texDataVersion == obj.texDataVersion && entry.tlutObjId == 0) {
        return entry.handle;
      }
    }
  }

  gfx::TextureHandle handle;
  if (const auto replacement = gfx::texture_replacement::find_replacement(obj); replacement.has_value()) {
    handle = *replacement;
  } else {
    handle =
        gfx::new_static_texture_2d(obj.width(), obj.height(), obj.mip_count(), obj.format(),
                                   {static_cast<const uint8_t*>(obj.data), UINT32_MAX}, false, "GX Static Texture");
  }
  if (!obj.no_cache()) {
    store_cached_texture(obj, handle);
  }
  return handle;
}

gfx::TextureHandle resolve_static_palette_texture(const GXTexObj_& obj, const GXTlutObj_& tlut) {
  ZoneScoped;

  if (obj.texObjId != 0) {
    if (const auto it = s_textureObjectCaches.find(obj.texObjId); it != s_textureObjectCaches.end()) {
      const auto& entry = it->second;
      if (entry.handle && entry.texDataVersion == obj.texDataVersion && entry.tlutObjId == tlut.tlutObjId &&
          entry.tlutDataVersion == tlut.tlutDataVersion) {
        return entry.handle;
      }
    }
  }

  gfx::TextureHandle handle;
  if (const auto replacement = gfx::texture_replacement::find_replacement(obj); replacement.has_value()) {
    handle = *replacement;
  } else {
    auto converted = gfx::convert_texture_palette(
        obj.format(), obj.width(), obj.height(), obj.mip_count(), {static_cast<const u8*>(obj.data), UINT32_MAX},
        tlut.format, tlut.numEntries, {static_cast<const u8*>(tlut.data), static_cast<size_t>(tlut.numEntries) * 2});
    if (converted.data.empty()) {
      return {};
    }
    handle =
        gfx::new_static_texture_2d(obj.width(), obj.height(), obj.mip_count(), GX_TF_RGBA8_PC,
                                   {converted.data.data(), converted.data.size()}, false, "GX Static Palette Texture");
    handle->hasArbitraryMips = converted.hasArbitraryMips;
  }
  if (!obj.no_cache() && !tlut.no_cache()) {
    store_cached_texture(obj, handle, tlut.tlutObjId, tlut.tlutDataVersion);
  }
  return handle;
}

gfx::TextureHandle resolve_dynamic_palette_texture(const GXTexObj_& obj, const GXState::CopyTextureRef& source,
                                                   const GXTlutObj_& tlut) {
  ZoneScoped;

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

Vec2<uint32_t> logical_fb_size() noexcept {
  return gfx::is_offscreen() ? gfx::get_render_target_size() : vi::configured_fb_size();
}

std::tuple<float, float, float, float> calculate_inner_box(const int targetWidth, const int targetHeight,
                                                           const int logicalFbWidth, const int logicalFbHeight) noexcept {
  if (g_gxState.viewportPolicy == AURORA_VIEWPORT_STRETCH) {
    return {
        0.0f,
        0.0f,
        static_cast<float>(targetWidth),
        static_cast<float>(targetHeight),
    };
  } else {
    const auto scale = std::min(static_cast<float>(targetWidth) / static_cast<float>(logicalFbWidth),
                                static_cast<float>(targetHeight) / static_cast<float>(logicalFbHeight));
    const auto offsetX =
        std::floor((static_cast<float>(targetWidth) - static_cast<float>(logicalFbWidth) * scale) * 0.5f);
    const auto offsetY =
        std::floor((static_cast<float>(targetHeight) - static_cast<float>(logicalFbHeight) * scale) * 0.5f);
    return {
        offsetX,
        offsetY,
        static_cast<float>(targetWidth) - 2.0f * offsetX,
        static_cast<float>(targetHeight) - 2.0f * offsetY,
    };
  }
}

gfx::Viewport map_logical_viewport(const gfx::Viewport& logicalViewport) noexcept {
  if (g_gxState.viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    return logicalViewport;
  }

  const auto [logicalFbWidth, logicalFbHeight] = logical_fb_size();
  const auto [targetWidth, targetHeight] = gfx::get_render_target_size();
  if (logicalFbWidth == 0 || logicalFbHeight == 0 || targetWidth == 0 || targetHeight == 0) {
    return logicalViewport;
  }

  const auto [offsetX, offsetY, innerWidth, innerHeight] =
      calculate_inner_box(targetWidth, targetHeight, logicalFbWidth, logicalFbHeight);

  return {
      .left = offsetX + (logicalViewport.left / static_cast<float>(logicalFbWidth)) * innerWidth,
      .top = offsetY + (logicalViewport.top / static_cast<float>(logicalFbHeight)) * innerHeight,
      .width = (logicalViewport.width / static_cast<float>(logicalFbWidth)) * innerWidth,
      .height = (logicalViewport.height / static_cast<float>(logicalFbHeight)) * innerHeight,
      .znear = logicalViewport.znear,
      .zfar = logicalViewport.zfar,
  };
}

gfx::ClipRect map_logical_scissor(const gfx::ClipRect& logicalScissor) noexcept {
  if (g_gxState.viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    return logicalScissor;
  }

  const auto [logicalFbWidth, logicalFbHeight] = logical_fb_size();
  const auto [targetWidth, targetHeight] = gfx::get_render_target_size();
  if (logicalFbWidth == 0 || logicalFbHeight == 0 || targetWidth == 0 || targetHeight == 0) {
    return logicalScissor;
  }

  const auto [offsetX, offsetY, innerWidth, innerHeight] =
      calculate_inner_box(targetWidth, targetHeight, logicalFbWidth, logicalFbHeight);

  const float left = offsetX + (static_cast<float>(logicalScissor.x) / static_cast<float>(logicalFbWidth)) * innerWidth;
  const float top = offsetY + (static_cast<float>(logicalScissor.y) / static_cast<float>(logicalFbHeight)) * innerHeight;
  const float right = offsetX + (static_cast<float>(logicalScissor.x + logicalScissor.width) / static_cast<float>(logicalFbWidth)) * innerWidth;
  const float bottom = offsetY + (static_cast<float>(logicalScissor.y + logicalScissor.height) / static_cast<float>(logicalFbHeight)) * innerHeight;

  const auto mappedLeft = std::clamp(static_cast<int32_t>(std::floor(left)), 0, static_cast<int32_t>(targetWidth));
  const auto mappedTop = std::clamp(static_cast<int32_t>(std::floor(top)), 0, static_cast<int32_t>(targetHeight));
  const auto mappedRight =
      std::clamp(static_cast<int32_t>(std::ceil(right)), mappedLeft, static_cast<int32_t>(targetWidth));
  const auto mappedBottom =
      std::clamp(static_cast<int32_t>(std::ceil(bottom)), mappedTop, static_cast<int32_t>(targetHeight));

  return {
      .x = mappedLeft,
      .y = mappedTop,
      .width = mappedRight - mappedLeft,
      .height = mappedBottom - mappedTop,
  };
}

void set_logical_viewport(const gfx::Viewport& viewport) noexcept {
  g_gxState.logicalViewport = viewport;
  set_render_viewport(map_logical_viewport(viewport));
}

void set_render_viewport(const gfx::Viewport& viewport) noexcept {
  g_gxState.renderViewport = viewport;
  gfx::set_viewport(viewport);
}

void set_logical_scissor(const gfx::ClipRect& scissor) noexcept {
  g_gxState.logicalScissor = scissor;
  set_render_scissor(map_logical_scissor(g_gxState.logicalScissor));
}

void set_render_scissor(const gfx::ClipRect& scissor) noexcept {
  g_gxState.renderScissor = scissor;
  gfx::set_scissor(scissor);
}

const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[static_cast<size_t>(id)]; }

void evict_texture_object(u32 texObjId) noexcept {
  if (const auto it = s_textureObjectCaches.find(texObjId); it != s_textureObjectCaches.end()) {
    clear_texture_dependency(texObjId, it->second.tlutObjId);
    s_textureObjectCaches.erase(it);
  }
  // If there is a loaded slot with this ID, mark it as no_cache to avoid inserting it when it's resolved.
  // This also handles the case where the texture was created, loaded, and immediately destroyed before we resolved it.
  for (auto& obj : g_gxState.loadedTextures) {
    if (obj.texObjId == texObjId) {
      obj.set_no_cache(true);
    }
  }
}

void evict_tlut_object(u32 tlutObjId) noexcept {
  if (const auto it = s_tlutObjectCaches.find(tlutObjId); it != s_tlutObjectCaches.end()) {
    for (const u32 texObjId : it->second.staticTextureUsers) {
      s_textureObjectCaches.erase(texObjId);
    }
    s_tlutObjectCaches.erase(it);
  }
  // If there is a loaded slot with this ID, mark it as no_cache to avoid inserting it when it's resolved.
  // This also handles the case where the texture was created, loaded, and immediately destroyed before we resolved it.
  for (auto& obj : g_gxState.loadedTluts) {
    if (obj.tlutObjId == tlutObjId) {
      obj.set_no_cache(true);
    }
  }
}

void clear_copy_texture_cache() noexcept {
  g_gxState.copyTextures.clear();
  g_gxState.copyTextureCache.clear();
  for (auto& [_, cache] : s_tlutObjectCaches) {
    cache.dynamicPaletteTextures.clear();
  }
}

void evict_copy_texture(const void* dest) noexcept {
  g_gxState.copyTextures.erase(dest);
  for (auto it = g_gxState.copyTextureCache.begin(); it != g_gxState.copyTextureCache.end();) {
    if (it->first.dest == dest) {
      g_gxState.copyTextureCache.erase(it++);
    } else {
      ++it;
    }
  }
}

void resolve_sampled_textures(const ShaderInfo& info) noexcept {
  ZoneScoped;

  for (u32 i = 0; i < MaxTextures; ++i) {
    if (!info.sampledTextures.test(i)) {
      continue;
    }

    GXTexObj_ obj = g_gxState.loadedTextures[i];
    auto& textureBind = g_gxState.textures[i];
    if (obj.texObjId != 0 && obj.texObjId == textureBind.texObj.texObjId &&
        obj.texDataVersion == textureBind.texObj.texDataVersion) {
      // Texture bind unchanged
      continue;
    }

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
    textureBind = gfx::TextureBind{obj, std::move(handle)};
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
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = label,
      .layout = sPipelineLayout,
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
  ZoneScoped;

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

GXBindGroups build_bind_groups(const ShaderInfo& info) noexcept {
  ZoneScoped;

  if (!info.sampledTextures.any() && !info.sampledIndTextures.any()) {
    // Don't bother re-binding anything
    return {};
  }

  // Using C WGPU types instead of C++ wrappers to avoid destructor overhead
  std::array<WGPUBindGroupEntry, MaxTextures * 2> textureEntries{};
  for (u32 i = 0; i < MaxTextures; ++i) {
    const auto& tex = g_gxState.textures[i];
    WGPUBindGroupEntry& textureEntry = textureEntries[i * 2];
    WGPUBindGroupEntry& samplerEntry = textureEntries[i * 2 + 1];
    textureEntry.binding = i * 2;
    samplerEntry.binding = i * 2 + 1;
    if (tex && (info.sampledTextures[i] || info.sampledIndTextures[i])) {
      textureEntry.textureView = tex.ref->sampleTextureView.Get();
      samplerEntry.sampler = gfx::sampler_ref(tex.get_descriptor()).Get();
    } else {
      textureEntry.textureView = sEmptyTextureView.Get();
      samplerEntry.sampler = sEmptySampler.Get();
    }
  }
  const WGPUBindGroupDescriptor textureBindGroupDescriptor{
      .label = {"GX Texture Bind Group", WGPU_STRLEN},
      .layout = sTextureBindGroupLayout.Get(),
      .entryCount = textureEntries.size(),
      .entries = textureEntries.data(),
  };
  return {
      .textureBindGroup = gfx::bind_group_ref(textureBindGroupDescriptor),
  };
}

void initialize() noexcept {
  {
    std::array<wgpu::BindGroupLayoutEntry, MaxTextures * 2> textureEntries;
    for (u32 i = 0; i < MaxTextures; ++i) {
      textureEntries[i * 2] = {
          .binding = i * 2,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              {
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      };
      textureEntries[i * 2 + 1] = {
          .binding = i * 2 + 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler = {.type = wgpu::SamplerBindingType::Filtering},
      };
    }
    const wgpu::BindGroupLayoutDescriptor descriptor{
        .label = "GX Texture Bind Group Layout",
        .entryCount = textureEntries.size(),
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
  {
    std::array<wgpu::BindGroupEntry, MaxTextures * 2> entries;
    for (u32 i = 0; i < MaxTextures; ++i) {
      entries[i * 2] = {
          .binding = i * 2,
          .textureView = sEmptyTextureView,
      };
      entries[i * 2 + 1] = {
          .binding = i * 2 + 1,
          .sampler = sEmptySampler,
      };
    }
    const wgpu::BindGroupDescriptor desc{
        .label = "GX Empty Texture Bind Group",
        .layout = sTextureBindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_emptyTextureBindGroup = g_device.CreateBindGroup(&desc);
  }
  {
    const std::array layouts{
        gfx::g_staticBindGroupLayout,
        gfx::g_uniformBindGroupLayout,
        sTextureBindGroupLayout,
    };
    const wgpu::PipelineLayoutDescriptor desc{
        .label = "GX Pipeline Layout",
        .bindGroupLayoutCount = layouts.size(),
        .bindGroupLayouts = layouts.data(),
    };
    sPipelineLayout = g_device.CreatePipelineLayout(&desc);
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
    return {wgpu::FilterMode::Nearest, wgpu::MipmapFilterMode::Undefined};
  case GX_LINEAR:
    return {wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Undefined};
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
  case GX_MAX_ANISOTROPY:
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
  float minLod = texObj.min_lod();
  float maxLod = texObj.max_lod();
  if (mipFilter == wgpu::MipmapFilterMode::Undefined) {
    minLod = 0.f;
    maxLod = 0.f;
  }
  return {
      .label = "Generated Filtering Sampler",
      .addressModeU = wgpu_address_mode(texObj.wrap_s()),
      .addressModeV = wgpu_address_mode(texObj.wrap_t()),
      .addressModeW = wgpu::AddressMode::Repeat,
      .magFilter = magFilter,
      .minFilter = minFilter,
      .mipmapFilter = mipFilter,
      .lodMinClamp = minLod,
      .lodMaxClamp = maxLod,
      .maxAnisotropy = wgpu_aniso(texObj.max_aniso()),
  };
} // namespace aurora::gx

#include "common.hpp"

#include "clear.hpp"
#include "depth_peek.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "../gx/pipeline.hpp"
#include "pipeline_cache.hpp"
#include "tex_copy_conv.hpp"
#include "tex_palette_conv.hpp"
#include "texture_replacement.hpp"
#include "texture.hpp"
#include "../window.hpp"

#include <atomic>
#include <optional>
#include <ranges>

#include <absl/container/flat_hash_map.h>
#include <magic_enum.hpp>

#include "tracy/Tracy.hpp"

namespace aurora::gfx {
static Module Log("aurora::gfx");

using webgpu::g_device;
using webgpu::g_instance;
using webgpu::g_queue;

#ifdef AURORA_GFX_DEBUG_GROUPS
std::vector<std::string> g_debugGroupStack;
std::vector<std::string> g_debugMarkers;
#endif

constexpr uint64_t StagingBufferSize = UniformBufferSize + VertexBufferSize + IndexBufferSize + StorageBufferSize +
                                       (UseTextureBuffer ? TextureUploadSize : 0);

struct ShaderDrawCommand {
  ShaderType type;
  union {
    clear::DrawData clear;
    gx::DrawData gx;
  };
};
enum class CommandType {
  SetViewport,
  SetScissor,
  Draw,
  DebugMarker,
};
struct Command {
  CommandType type;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> debugGroupStack;
#endif
  union Data {
    Viewport setViewport;
    ClipRect setScissor;
    ShaderDrawCommand draw;
    size_t debugMarkerIndex;
  } data;
};
} // namespace aurora::gfx

namespace aurora {
// For types that we can't ensure are safe to hash with has_unique_object_representations,
// we create specialized methods to handle them. Note that these are highly dependent on
// the structure definition, which could easily change with Dawn updates.
template <>
inline HashType xxh3_hash(const WGPUBindGroupDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(WGPUBindGroupDescriptor, layout); // skip nextInChain, label
  const auto hash = xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                                sizeof(WGPUBindGroupDescriptor) - offset - sizeof(void*) /* skip entries */, seed);
  return xxh3_hash_s(input.entries, sizeof(WGPUBindGroupEntry) * input.entryCount, hash);
}
template <>
inline HashType xxh3_hash(const wgpu::SamplerDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(wgpu::SamplerDescriptor, addressModeU); // skip nextInChain, label
  return xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                     sizeof(wgpu::SamplerDescriptor) - offset - 2 /* skip padding */, seed);
}
} // namespace aurora

namespace aurora::gfx {
namespace {
struct CachedBindGroup {
  wgpu::BindGroup bindGroup;
  uint32_t lastUsedFrame = 0;
};

constexpr uint32_t BindGroupCacheRetainFrames = 32;
constexpr uint32_t BindGroupCacheSweepPeriod = 16;
} // namespace

static absl::flat_hash_map<BindGroupRef, CachedBindGroup> g_cachedBindGroups;
static absl::flat_hash_map<SamplerRef, wgpu::Sampler> g_cachedSamplers;

static ByteBuffer g_verts;
static ByteBuffer g_uniforms;
static ByteBuffer g_indices;
static ByteBuffer g_storage;
static ByteBuffer g_textureUpload;
wgpu::Buffer g_vertexBuffer;
wgpu::Buffer g_uniformBuffer;
wgpu::Buffer g_indexBuffer;
wgpu::Buffer g_storageBuffer;
static std::array<wgpu::Buffer, 3> g_stagingBuffers;
static size_t currentStagingBuffer = 0;
enum class BufferMapState {
  Unmapped,
  Mapping,
  Mapped,
};
static std::atomic s_mappingState{BufferMapState::Unmapped};
static wgpu::Limits g_cachedLimits;
static uint32_t g_frameIndex = UINT32_MAX;
static PipelineRef g_currentPipeline;
wgpu::BindGroupLayout g_staticBindGroupLayout;
wgpu::BindGroup g_staticBindGroup;
wgpu::BindGroupLayout g_uniformBindGroupLayout;
wgpu::BindGroup g_uniformBindGroup;

// for imgui debug
AuroraStats g_stats{};
uint32_t g_drawCallCount = 0;
uint32_t g_mergedDrawCallCount = 0;

using CommandList = std::vector<Command>;
struct RenderPass {
  wgpu::TextureView colorView;
  wgpu::TextureView resolveView; // MSAA resolve target; null if msaaSamples == 1
  wgpu::TextureView depthView;
  wgpu::Texture copySourceTexture;
  wgpu::TextureView copySourceView;
  wgpu::TextureView copySourceDepthView;
  wgpu::Extent3D targetSize;
  uint32_t msaaSamples = 1;

  TextureHandle resolveTarget;
  GXTexFmt resolveFormat = GX_TF_RGBA8;
  ClipRect resolveRect;
  Range resolveUniformRange;
  Vec4<float> clearColorValue{0.f, 0.f, 0.f, 0.f};
  float clearDepthValue = gx::UseReversedZ ? 0.f : 1.f;
  CommandList commands;
  bool clearColor = true;
  bool clearDepth = true;
  std::vector<tex_palette_conv::ConvRequest> paletteConvs;
};
static std::vector<RenderPass> g_renderPasses;
static u32 g_currentRenderPass = UINT32_MAX;
static bool g_inOffscreen = false;
static std::optional<RenderPass> g_suspendedEfbPass;
static Viewport g_suspendedEfbViewport;
static ClipRect g_suspendedEfbScissor;
static webgpu::TextureWithSampler g_offscreenColor;
static webgpu::TextureWithSampler g_offscreenDepth;

static void set_efb_targets(RenderPass& pass) {
  pass.colorView = webgpu::g_frameBuffer.view;
  pass.resolveView = webgpu::g_graphicsConfig.msaaSamples > 1 ? webgpu::g_frameBufferResolved.view : nullptr;
  pass.depthView = webgpu::g_depthBuffer.view;
  pass.copySourceTexture =
      webgpu::g_graphicsConfig.msaaSamples > 1 ? webgpu::g_frameBufferResolved.texture : webgpu::g_frameBuffer.texture;
  pass.copySourceView =
      webgpu::g_graphicsConfig.msaaSamples > 1 ? webgpu::g_frameBufferResolved.view : webgpu::g_frameBuffer.view;
  pass.copySourceDepthView = webgpu::g_depthBuffer.view;
  pass.targetSize = webgpu::g_frameBuffer.size;
  pass.msaaSamples = webgpu::g_graphicsConfig.msaaSamples;
}

struct OffscreenCacheKey {
  uint32_t width;
  uint32_t height;

  bool operator==(const OffscreenCacheKey& rhs) const { return width == rhs.width && height == rhs.height; }
  template <typename H>
  friend H AbslHashValue(H h, const OffscreenCacheKey& key) {
    return H::combine(std::move(h), key.width, key.height);
  }
};
struct OffscreenCacheEntry {
  webgpu::TextureWithSampler color;
  webgpu::TextureWithSampler depth;
};
static absl::flat_hash_map<OffscreenCacheKey, OffscreenCacheEntry> g_offscreenCache;
std::vector<TextureUpload> g_textureUploads;

static inline void push_command(CommandType type, const Command::Data& data) {
  if (g_currentRenderPass == UINT32_MAX)
    UNLIKELY {
      Log.warn("Dropping command {}", magic_enum::enum_name(type));
      return;
    }
  g_renderPasses[g_currentRenderPass].commands.push_back({
      .type = type,
#ifdef AURORA_GFX_DEBUG_GROUPS
      .debugGroupStack = g_debugGroupStack,
#endif
      .data = data,
  });
}

template <>
gx::DrawData* get_last_draw_command() {
  if (g_currentRenderPass >= g_renderPasses.size()) {
    return nullptr;
  }
  auto& last = g_renderPasses[g_currentRenderPass].commands.back();
  if (last.type != CommandType::Draw || last.data.draw.type != ShaderType::GX) {
    return nullptr;
  }
  return &last.data.draw.gx;
}

static void push_draw_command(ShaderDrawCommand data) {
  push_command(CommandType::Draw, Command::Data{.draw = data});
  ++g_drawCallCount;
}

Vec2<uint32_t> get_render_target_size() noexcept {
  if (g_currentRenderPass < g_renderPasses.size()) {
    const auto& size = g_renderPasses[g_currentRenderPass].targetSize;
    return {size.width, size.height};
  }
  const auto windowSize = window::get_window_size();
  return {windowSize.fb_width, windowSize.fb_height};
}

static Viewport g_cachedViewport;
void set_viewport(const Viewport& cmd) noexcept {
  if (cmd != g_cachedViewport) {
    push_command(CommandType::SetViewport, Command::Data{.setViewport = cmd});
    g_cachedViewport = cmd;
  }
}

static ClipRect g_cachedScissor;
void set_scissor(const ClipRect& cmd) noexcept {
  if (cmd != g_cachedScissor) {
    push_command(CommandType::SetScissor, Command::Data{.setScissor = cmd});
    g_cachedScissor = cmd;
  }
}

template <>
void push_draw_command(clear::DrawData data) {
  push_draw_command(ShaderDrawCommand{.type = ShaderType::Clear, .clear = data});
}

template <>
PipelineRef pipeline_ref(const clear::PipelineConfig& config) {
  return find_pipeline(ShaderType::Clear, config, [=] { return create_pipeline(config); });
}

void resolve_pass(TextureHandle texture, ClipRect rect, bool clearColor, bool clearAlpha, bool clearDepth,
                  Vec4<float> clearColorValue, float clearDepthValue, GXTexFmt resolveFormat) {
  // Resolve current render pass
  auto& prevPass = g_renderPasses[g_currentRenderPass];
  prevPass.resolveTarget = std::move(texture);
  prevPass.resolveRect = rect;
  prevPass.resolveFormat = resolveFormat;
  // Push UV transform uniform for tex_copy_conv (crop region in UV space)
  const auto srcW = static_cast<float>(prevPass.targetSize.width);
  const auto srcH = static_cast<float>(prevPass.targetSize.height);
  const std::array uvTransform{
      static_cast<float>(rect.x) / srcW,
      static_cast<float>(rect.y) / srcH,
      static_cast<float>(rect.width) / srcW,
      static_cast<float>(rect.height) / srcH,
  };
  prevPass.resolveUniformRange = push_uniform(uvTransform);

  // Populate new render pass from previous
  const auto msaaSamples = prevPass.msaaSamples;
  RenderPass newPass{
      .colorView = prevPass.colorView,
      .resolveView = prevPass.resolveView,
      .depthView = prevPass.depthView,
      .copySourceTexture = prevPass.copySourceTexture,
      .copySourceView = prevPass.copySourceView,
      .copySourceDepthView = prevPass.copySourceDepthView,
      .targetSize = prevPass.targetSize,
      .msaaSamples = msaaSamples,
      .clearColorValue = clearColorValue,
      .clearDepthValue = clearDepthValue,
      .clearColor = clearColor && clearAlpha,
      .clearDepth = clearDepth,
  };
  g_renderPasses.emplace_back(std::move(newPass));
  ++g_currentRenderPass;

  if (!newPass.clearColor && (clearColor || clearAlpha)) {
    // If we're only clearing color _or_ alpha, perform a clear draw
    push_draw_command(clear::DrawData{
        .pipeline = pipeline_ref(clear::PipelineConfig{
            .msaaSamples = msaaSamples,
            .clearColor = clearColor,
            .clearAlpha = clearAlpha,
            .clearDepth = false, // Depth cleared via render attachment
        }),
        .color =
            wgpu::Color{
                .r = clearColorValue.x(),
                .g = clearColorValue.y(),
                .b = clearColorValue.z(),
                .a = clearColorValue.w(),
            },
    });
  }
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
}

void queue_palette_conv(tex_palette_conv::ConvRequest req) {
  g_renderPasses[g_currentRenderPass].paletteConvs.push_back(std::move(req));
}

bool is_offscreen() noexcept { return g_inOffscreen; }

uint32_t get_sample_count() noexcept {
  CHECK(g_currentRenderPass != UINT32_MAX, "get_sample_count called outside of a frame");
  return g_renderPasses[g_currentRenderPass].msaaSamples;
}

void clear_caches() noexcept {
  g_offscreenCache.clear();
  g_cachedBindGroups.clear();
}

static OffscreenCacheEntry get_offscreen_textures(uint32_t width, uint32_t height) {
  OffscreenCacheKey key{width, height};
  if (const auto it = g_offscreenCache.find(key); it != g_offscreenCache.end()) {
    return it->second;
  }
  const auto colorFormat = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  const wgpu::Extent3D size{width, height, 1};
  const wgpu::TextureDescriptor colorDesc{
      .label = "Offscreen Color",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc |
               wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = colorFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  auto colorTexture = g_device.CreateTexture(&colorDesc);
  auto colorView = colorTexture.CreateView();
  webgpu::TextureWithSampler color{
      .texture = std::move(colorTexture),
      .view = std::move(colorView),
      .size = size,
      .format = colorFormat,
  };
  const auto depthFormat = webgpu::g_graphicsConfig.depthFormat;
  const wgpu::TextureDescriptor depthDesc{
      .label = "Offscreen Depth",
      .usage = wgpu::TextureUsage::RenderAttachment,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = depthFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  auto depthTexture = g_device.CreateTexture(&depthDesc);
  auto depthView = depthTexture.CreateView();
  webgpu::TextureWithSampler depth{
      .texture = std::move(depthTexture),
      .view = std::move(depthView),
      .size = size,
      .format = depthFormat,
  };
  OffscreenCacheEntry entry{
      .color = std::move(color),
      .depth = std::move(depth),
  };
  auto [insertIt, _] = g_offscreenCache.emplace(key, std::move(entry));
  return insertIt->second;
}

void begin_offscreen(uint32_t width, uint32_t height) {
  ZoneScoped;
  CHECK(g_currentRenderPass != UINT32_MAX, "begin_offscreen called outside of a frame");

  // If the current EFB pass has no resolve target, its output is unobservable.
  // Suspend it so that we can resume it after the offscreen pass.
  if (!g_inOffscreen) {
    auto& currentPass = g_renderPasses[g_currentRenderPass];
    if (!currentPass.resolveTarget) {
      g_suspendedEfbPass = std::move(currentPass);
      g_renderPasses.pop_back();
      --g_currentRenderPass;
    }
    g_suspendedEfbViewport = g_cachedViewport;
    g_suspendedEfbScissor = g_cachedScissor;
  }

  // Create offscreen textures
  auto offscreenEntry = get_offscreen_textures(width, height);
  g_offscreenColor = std::move(offscreenEntry.color);
  g_offscreenDepth = std::move(offscreenEntry.depth);

  // Start a new pass with offscreen targets
  RenderPass newPass{
      .colorView = g_offscreenColor.view,
      .depthView = g_offscreenDepth.view,
      .copySourceTexture = g_offscreenColor.texture,
      .copySourceView = g_offscreenColor.view,
      .copySourceDepthView = g_offscreenDepth.view,
      .targetSize = {width, height, 1},
      .msaaSamples = 1,
      .clearColorValue = {0.f, 0.f, 0.f, 0.f},
      .clearDepthValue = gx::UseReversedZ ? 0.f : 1.f,
      .clearColor = true,
      .clearDepth = true,
  };
  g_renderPasses.emplace_back(std::move(newPass));
  ++g_currentRenderPass;

  g_inOffscreen = true;

  g_cachedViewport = {0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
  g_cachedScissor = {0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)};
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
}

void end_offscreen() {
  ZoneScoped;
  CHECK(g_inOffscreen, "end_offscreen called without begin_offscreen");

  g_inOffscreen = false;
  g_offscreenColor = {};
  g_offscreenDepth = {};

  // Resume suspended EFB pass, or start a new one (load existing content)
  if (g_suspendedEfbPass) {
    g_renderPasses.emplace_back(std::move(*g_suspendedEfbPass));
    g_suspendedEfbPass.reset();
  } else {
    auto& pass = g_renderPasses.emplace_back();
    pass.clearColor = false;
    pass.clearDepth = false;
  }
  ++g_currentRenderPass;
  set_efb_targets(g_renderPasses[g_currentRenderPass]);

  g_cachedViewport = g_suspendedEfbViewport;
  g_cachedScissor = g_suspendedEfbScissor;
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
}

template <>
void push_draw_command(gx::DrawData data) {
  push_draw_command(ShaderDrawCommand{.type = ShaderType::GX, .gx = data});
}

template <>
PipelineRef pipeline_ref(const gx::PipelineConfig& config) {
  return find_pipeline(ShaderType::GX, config, [=] { return create_pipeline(config); });
}

void initialize() {
  g_frameIndex = 0;
  depth_peek::initialize();
  tex_copy_conv::initialize();
  tex_palette_conv::initialize();
  texture_replacement::initialize();

  // For uniform & storage buffer offset alignments
  g_device.GetLimits(&g_cachedLimits);

  const auto createBuffer = [](wgpu::Buffer& out, wgpu::BufferUsage usage, uint64_t size, const char* label) {
    if (size <= 0) {
      return;
    }
    const wgpu::BufferDescriptor descriptor{
        .label = label,
        .usage = usage,
        .size = size,
    };
    out = g_device.CreateBuffer(&descriptor);
  };
  createBuffer(g_uniformBuffer, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, UniformBufferSize,
               "Shared Uniform Buffer");
  createBuffer(g_vertexBuffer, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, VertexBufferSize,
               "Shared Vertex Buffer");
  createBuffer(g_indexBuffer, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, IndexBufferSize,
               "Shared Index Buffer");
  createBuffer(g_storageBuffer, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, StorageBufferSize,
               "Shared Storage Buffer");
  for (int i = 0; i < g_stagingBuffers.size(); ++i) {
    const auto label = fmt::format("Staging Buffer {}", i);
    createBuffer(g_stagingBuffers[i], wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc, StagingBufferSize,
                 label.c_str());
  }
  currentStagingBuffer = 0;
  s_mappingState.store(BufferMapState::Unmapped, std::memory_order_release);
  map_staging_buffer();

  {
    constexpr std::array layoutEntries{
        // Vertex data buffer
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                },
        },
        // Storage data buffer
        wgpu::BindGroupLayoutEntry{
            .binding = 1,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                },
        },
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Static bind group layout",
        .entryCount = layoutEntries.size(),
        .entries = layoutEntries.data(),
    };
    g_staticBindGroupLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    const std::array entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .buffer = g_vertexBuffer,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .buffer = g_storageBuffer,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .label = "Static bind group",
        .layout = g_staticBindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_staticBindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  }

  {
    constexpr std::array layoutEntries{
        // Uniform buffer (dynamic offset)
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = true,
                },
        },
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Uniform bind group layout",
        .entryCount = layoutEntries.size(),
        .entries = layoutEntries.data(),
    };
    g_uniformBindGroupLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    const std::array entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .buffer = g_uniformBuffer,
            .size = gx::MaxUniformSize,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .label = "Uniform bind group",
        .layout = g_uniformBindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_uniformBindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  }

  gx::initialize();
  initialize_pipeline_cache();
}

void shutdown() {
  shutdown_pipeline_cache();
  depth_peek::shutdown();
  tex_copy_conv::shutdown();
  tex_palette_conv::shutdown();
  texture_replacement::shutdown();
  gx::shutdown();

  g_textureUploads.clear();
  g_cachedBindGroups.clear();
  g_cachedSamplers.clear();
  g_vertexBuffer = {};
  g_uniformBuffer = {};
  g_indexBuffer = {};
  g_storageBuffer = {};
  g_stagingBuffers.fill({});
  g_renderPasses.clear();
  g_currentRenderPass = UINT32_MAX;
  g_offscreenCache.clear();
  g_offscreenColor = {};
  g_offscreenDepth = {};
  g_staticBindGroup = {};
  g_staticBindGroupLayout = {};
  g_uniformBindGroup = {};
  g_uniformBindGroupLayout = {};
  g_inOffscreen = false;
  g_frameIndex = UINT32_MAX;
  currentStagingBuffer = 0;
  s_mappingState.store(BufferMapState::Unmapped, std::memory_order_release);
}

void map_staging_buffer() {
  auto expected = BufferMapState::Unmapped;
  if (!s_mappingState.compare_exchange_strong(expected, BufferMapState::Mapping, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
    return;
  }

  g_stagingBuffers[currentStagingBuffer].MapAsync(
      wgpu::MapMode::Write, 0, StagingBufferSize, wgpu::CallbackMode::AllowSpontaneous,
      [](wgpu::MapAsyncStatus status, wgpu::StringView message) {
        if (status == wgpu::MapAsyncStatus::CallbackCancelled || status == wgpu::MapAsyncStatus::Aborted) {
          Log.warn("Buffer mapping {}: {}", magic_enum::enum_name(status), message);
          s_mappingState.store(BufferMapState::Unmapped, std::memory_order_release);
          return;
        }
        ASSERT(status == wgpu::MapAsyncStatus::Success, "Buffer mapping failed: {} {}", magic_enum::enum_name(status),
               message);
        s_mappingState.store(BufferMapState::Mapped, std::memory_order_release);
      });
}

bool begin_frame() {
  ZoneScoped;
  {
    ZoneScopedN("Wait for buffer map");
    map_staging_buffer();
    while (true) {
      const auto mappingState = s_mappingState.load(std::memory_order_acquire);
      if (mappingState == BufferMapState::Mapped) {
        break;
      }
      if (mappingState == BufferMapState::Unmapped) {
        return false;
      }
      g_instance.ProcessEvents();
    }
  }
  size_t bufferOffset = 0;
  const auto& stagingBuf = g_stagingBuffers[currentStagingBuffer];
  const auto mapBuffer = [&](ByteBuffer& buf, uint64_t size) {
    if (size <= 0) {
      return;
    }
    buf = ByteBuffer{static_cast<u8*>(stagingBuf.GetMappedRange(bufferOffset, size)), static_cast<size_t>(size)};
    bufferOffset += size;
  };
  mapBuffer(g_verts, VertexBufferSize);
  mapBuffer(g_uniforms, UniformBufferSize);
  mapBuffer(g_indices, IndexBufferSize);
  mapBuffer(g_storage, StorageBufferSize);
  if constexpr (UseTextureBuffer) {
    mapBuffer(g_textureUpload, TextureUploadSize);
  }

  g_drawCallCount = 0;
  g_mergedDrawCallCount = 0;
  g_suspendedEfbPass.reset();

  g_renderPasses.emplace_back();
  set_efb_targets(g_renderPasses[0]);
  g_renderPasses[0].clearColorValue = gx::g_gxState.clearColor;
  g_renderPasses[0].clearDepthValue = gx::clear_depth_value();
  g_currentRenderPass = 0;
  // Refresh render viewport/scissor from logical in case FB size changed
  g_cachedViewport = gx::map_logical_viewport(gx::g_gxState.logicalViewport);
  g_cachedScissor = gx::map_logical_scissor(gx::g_gxState.logicalScissor);
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
  begin_pipeline_frame();
  return true;
}

void end_frame(const wgpu::CommandEncoder& cmd) {
  ZoneScoped;
  ASSERT(!g_inOffscreen, "end_frame called while offscreen rendering is active");
  g_uniforms.append_zeroes(gx::MaxUniformSize); // Pad the end of the buffer
  uint64_t bufferOffset = 0;
  const auto writeBuffer = [&](ByteBuffer& buf, wgpu::Buffer& out, uint64_t size, std::string_view label) {
    const auto writeSize = buf.size(); // Only need to copy this many bytes
    if (writeSize > 0) {
      cmd.CopyBufferToBuffer(g_stagingBuffers[currentStagingBuffer], bufferOffset, out, 0, AURORA_ALIGN(writeSize, 4));
      buf.release();
    }
    bufferOffset += size;
    return writeSize;
  };
  g_stagingBuffers[currentStagingBuffer].Unmap();
  s_mappingState.store(BufferMapState::Unmapped, std::memory_order_release);
  g_stats.drawCallCount = g_drawCallCount;
  g_stats.mergedDrawCallCount = g_mergedDrawCallCount;
  g_stats.lastVertSize = writeBuffer(g_verts, g_vertexBuffer, VertexBufferSize, "Vertex");
  g_stats.lastUniformSize = writeBuffer(g_uniforms, g_uniformBuffer, UniformBufferSize, "Uniform");
  g_stats.lastIndexSize = writeBuffer(g_indices, g_indexBuffer, IndexBufferSize, "Index");
  g_stats.lastStorageSize = writeBuffer(g_storage, g_storageBuffer, StorageBufferSize, "Storage");
  if constexpr (UseTextureBuffer) {
    g_stats.lastTextureUploadSize = g_textureUpload.size();
    {
      // Perform texture copies
      for (const auto& item : g_textureUploads) {
        const wgpu::TexelCopyBufferInfo buf{
            .layout =
                wgpu::TexelCopyBufferLayout{
                    .offset = item.layout.offset + bufferOffset,
                    .bytesPerRow = AURORA_ALIGN(item.layout.bytesPerRow, 256),
                    .rowsPerImage = item.layout.rowsPerImage,
                },
            .buffer = g_stagingBuffers[currentStagingBuffer],
        };
        cmd.CopyBufferToTexture(&buf, &item.tex, &item.size);
      }
      g_textureUploads.clear();
      g_textureUpload.release();
    }
  }
  currentStagingBuffer = (currentStagingBuffer + 1) % g_stagingBuffers.size();
  map_staging_buffer();
  g_currentRenderPass = UINT32_MAX;
  for (auto& array : gx::g_gxState.arrays) {
    array.cachedRange = {};
  }
  end_pipeline_frame();
  ++g_frameIndex;
}

uint32_t current_frame() noexcept { return g_frameIndex; }

static void expire_cached_bind_groups() {
  if (g_cachedBindGroups.empty() || g_frameIndex == UINT32_MAX || g_frameIndex % BindGroupCacheSweepPeriod != 0) {
    return;
  }

  ZoneScoped;
  for (auto it = g_cachedBindGroups.begin(); it != g_cachedBindGroups.end();) {
    if (g_frameIndex - it->second.lastUsedFrame > BindGroupCacheRetainFrames) {
      g_cachedBindGroups.erase(it++);
    } else {
      ++it;
    }
  }
}

void render(wgpu::CommandEncoder& cmd) {
  ZoneScoped;
  for (u32 i = 0; i < g_renderPasses.size(); ++i) {
    const auto& passInfo = g_renderPasses[i];
    for (const auto& conv : passInfo.paletteConvs) {
      tex_palette_conv::run(cmd, conv);
    }
    if (i == g_renderPasses.size() - 1) {
      ASSERT(!passInfo.resolveTarget, "Final render pass must not have resolve target");
    } else if (!passInfo.resolveTarget) {
      // Skip intermediate render passes without resolve target
      continue;
    }

    const std::array attachments{
        wgpu::RenderPassColorAttachment{
            .view = passInfo.colorView,
            .resolveTarget = passInfo.resolveView,
            .loadOp = passInfo.clearColor ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue =
                {
                    .r = passInfo.clearColorValue.x(),
                    .g = passInfo.clearColorValue.y(),
                    .b = passInfo.clearColorValue.z(),
                    .a = passInfo.clearColorValue.w(),
                },
        },
    };
    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment{
        .view = passInfo.depthView,
        .depthLoadOp = passInfo.clearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = passInfo.clearDepthValue,
    };
    const auto label = fmt::format("Render pass {}", i);
    const wgpu::RenderPassDescriptor renderPassDescriptor{
        .label = label.c_str(),
        .colorAttachmentCount = attachments.size(),
        .colorAttachments = attachments.data(),
        .depthStencilAttachment = &depthStencilAttachment,
    };

    auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
    render_pass(pass, i);
    pass.End();

    if (i == g_renderPasses.size() - 1) {
      depth_peek::encode_frame_snapshot(cmd, passInfo.copySourceDepthView, passInfo.targetSize, passInfo.msaaSamples);
    }

    if (passInfo.resolveTarget) {
      const auto& dstSize = passInfo.resolveTarget->size;
      const bool needsConversion = tex_copy_conv::needs_conversion(passInfo.resolveFormat);
      const bool needsScaling = dstSize.width != static_cast<uint32_t>(passInfo.resolveRect.width) ||
                                dstSize.height != static_cast<uint32_t>(passInfo.resolveRect.height);
      const bool isDepth = gx::is_depth_format(passInfo.resolveFormat);
      if (isDepth && passInfo.msaaSamples > 1) {
        Log.fatal("Depth tex copies from multisampled EFB targets are not supported");
      }
      const tex_copy_conv::ConvRequest convReq{
          .fmt = passInfo.resolveFormat,
          .srcView = isDepth ? passInfo.copySourceDepthView : passInfo.copySourceView,
          .uniformRange = passInfo.resolveUniformRange,
          .dst = passInfo.resolveTarget,
          .sampleFilter = needsScaling ? tex_copy_conv::SampleFilter::Linear : tex_copy_conv::SampleFilter::Nearest,
      };
      if (needsConversion) {
        tex_copy_conv::run(cmd, convReq);
      } else if (needsScaling) {
        tex_copy_conv::blit(cmd, convReq);
      } else {
        const wgpu::TexelCopyTextureInfo src{
            .texture = passInfo.copySourceTexture,
            .origin =
                wgpu::Origin3D{
                    .x = static_cast<uint32_t>(passInfo.resolveRect.x),
                    .y = static_cast<uint32_t>(passInfo.resolveRect.y),
                },
        };
        const wgpu::TexelCopyTextureInfo dst{
            .texture = passInfo.resolveTarget->texture,
        };
        const wgpu::Extent3D size{
            .width = static_cast<uint32_t>(passInfo.resolveRect.width),
            .height = static_cast<uint32_t>(passInfo.resolveRect.height),
            .depthOrArrayLayers = 1,
        };
        cmd.CopyTextureToTexture(&src, &dst, &size);
      }
    }
  }
  g_renderPasses.clear();
  expire_cached_bind_groups();

#if defined(AURORA_GFX_DEBUG_GROUPS)
  if (!g_debugGroupStack.empty()) {
    for (auto& it : std::ranges::reverse_view(g_debugGroupStack)) {
      Log.warn("Debug group was not popped at end of frame: {}", it);
    }
    g_debugGroupStack.clear();
  }

  if (g_debugMarkers.size() > 0) {
    g_debugMarkers.clear();
  }
#endif
}

void after_submit() noexcept { depth_peek::after_submit(); }

void render_pass(const wgpu::RenderPassEncoder& pass, u32 idx) {
  g_currentPipeline = UINTPTR_MAX;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> lastDebugGroupStack;
#endif

  // Bind static bind group for the whole pass
  pass.SetBindGroup(0, g_staticBindGroup);
  pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);

  for (const auto& cmd : g_renderPasses[idx].commands) {
#ifdef AURORA_GFX_DEBUG_GROUPS
    {
      size_t firstDiff = lastDebugGroupStack.size();
      for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
        if (i >= cmd.debugGroupStack.size() || cmd.debugGroupStack[i] != lastDebugGroupStack[i]) {
          firstDiff = i;
          break;
        }
      }
      for (size_t i = firstDiff; i < lastDebugGroupStack.size(); ++i) {
        pass.PopDebugGroup();
      }
      for (size_t i = firstDiff; i < cmd.debugGroupStack.size(); ++i) {
        pass.PushDebugGroup(cmd.debugGroupStack[i].c_str());
      }
      lastDebugGroupStack = cmd.debugGroupStack;
    }
#endif
    switch (cmd.type) {
    case CommandType::SetViewport: {
      const auto& vp = cmd.data.setViewport;
      const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
      const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
      pass.SetViewport(vp.left, vp.top, vp.width, vp.height, minDepth, maxDepth);
    } break;
    case CommandType::SetScissor: {
      const auto& sc = cmd.data.setScissor;
      const auto& size = g_renderPasses[idx].targetSize;
      const auto x = std::clamp(static_cast<uint32_t>(sc.x), 0u, size.width);
      const auto y = std::clamp(static_cast<uint32_t>(sc.y), 0u, size.height);
      const auto w = std::clamp(static_cast<uint32_t>(sc.width), 0u, size.width - x);
      const auto h = std::clamp(static_cast<uint32_t>(sc.height), 0u, size.height - y);
      pass.SetScissorRect(x, y, w, h);
    } break;
    case CommandType::Draw: {
      const auto& draw = cmd.data.draw;
      switch (draw.type) {
      case ShaderType::Clear:
        clear::render(draw.clear, pass, g_renderPasses[idx].targetSize);
        break;
      case ShaderType::GX:
        gx::render(draw.gx, pass);
        break;
      }
    } break;
    case CommandType::DebugMarker: {
#if defined(AURORA_GFX_DEBUG_GROUPS)
      pass.InsertDebugMarker(wgpu::StringView(g_debugMarkers[cmd.data.debugMarkerIndex]));
#endif
    } break;
    }
  }

#ifdef AURORA_GFX_DEBUG_GROUPS
  for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
    pass.PopDebugGroup();
  }
#endif
}

bool bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass) {
  if (ref == g_currentPipeline) {
    return true;
  }
  wgpu::RenderPipeline pipeline;
  if (!get_pipeline(ref, pipeline)) {
    return false;
  }
  pass.SetPipeline(pipeline);
  g_currentPipeline = ref;
  return true;
}

static inline Range push(ByteBuffer& target, const uint8_t* data, size_t length, size_t alignment) {
  size_t padding = 0;
  if (alignment != 0) {
    const size_t remainder = length % alignment;
    if (remainder != 0) {
      padding = alignment - remainder;
    }
  }
  auto begin = target.size();
  if (length == 0) {
    length = alignment;
    target.append_zeroes(alignment);
  } else {
    target.append(data, length);
    if (padding > 0) {
      target.append_zeroes(padding);
    }
  }
  return {static_cast<uint32_t>(begin), static_cast<uint32_t>(length + padding)};
}
static inline Range map(ByteBuffer& target, size_t length, size_t alignment) {
  size_t padding = 0;
  if (alignment != 0) {
    const size_t remainder = length % alignment;
    if (remainder != 0) {
      padding = alignment - remainder;
    }
  }
  if (length == 0) {
    length = alignment;
  }
  auto begin = target.size();
  target.append_zeroes(length + padding);
  return {static_cast<uint32_t>(begin), static_cast<uint32_t>(length + padding)};
}
Range push_verts(const uint8_t* data, size_t length) { return push(g_verts, data, length, 0); }
Range push_indices(const uint8_t* data, size_t length) { return push(g_indices, data, length, 0); }
Range push_uniform(const uint8_t* data, size_t length) {
  return push(g_uniforms, data, length, g_cachedLimits.minUniformBufferOffsetAlignment);
}
Range push_storage(const uint8_t* data, size_t length) {
  return push(g_storage, data, length, g_cachedLimits.minStorageBufferOffsetAlignment);
}
Range push_texture_data(const uint8_t* data, size_t length, u32 bytesPerRow, u32 rowsPerImage) {
  // For CopyBufferToTexture, we need an alignment of 256 per row (see Dawn kTextureBytesPerRowAlignment)
  const auto copyBytesPerRow = AURORA_ALIGN(bytesPerRow, 256);
  const auto range = map(g_textureUpload, copyBytesPerRow * rowsPerImage, 0);
  u8* dst = g_textureUpload.data() + range.offset;
  for (u32 i = 0; i < rowsPerImage; ++i) {
    memcpy(dst, data, bytesPerRow);
    data += bytesPerRow;
    dst += copyBytesPerRow;
  }
  return range;
}
std::pair<ByteBuffer, Range> map_verts(size_t length) {
  const auto range = map(g_verts, length, 4);
  return {ByteBuffer{g_verts.data() + range.offset, range.size}, range};
}
std::pair<ByteBuffer, Range> map_indices(size_t length) {
  const auto range = map(g_indices, length, 4);
  return {ByteBuffer{g_indices.data() + range.offset, range.size}, range};
}
std::pair<ByteBuffer, Range> map_uniform(size_t length) {
  const auto range = map(g_uniforms, length, g_cachedLimits.minUniformBufferOffsetAlignment);
  return {ByteBuffer{g_uniforms.data() + range.offset, range.size}, range};
}
std::pair<ByteBuffer, Range> map_storage(size_t length) {
  const auto range = map(g_storage, length, g_cachedLimits.minStorageBufferOffsetAlignment);
  return {ByteBuffer{g_storage.data() + range.offset, range.size}, range};
}

BindGroupRef bind_group_ref(const WGPUBindGroupDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  const auto it = g_cachedBindGroups.find(id);
  if (it == g_cachedBindGroups.end()) {
    auto bg = wgpu::BindGroup::Acquire(wgpuDeviceCreateBindGroup(g_device.Get(), &descriptor));
    g_cachedBindGroups.emplace(id, CachedBindGroup{
                                       .bindGroup = std::move(bg),
                                       .lastUsedFrame = g_frameIndex,
                                   });
  } else {
    it->second.lastUsedFrame = g_frameIndex;
  }
  return id;
}

wgpu::BindGroup& find_bind_group(BindGroupRef id) {
  const auto it = g_cachedBindGroups.find(id);
  CHECK(it != g_cachedBindGroups.end(), "get_bind_group: failed to locate {:x}", id);
  return it->second.bindGroup;
}

wgpu::Sampler& sampler_ref(const wgpu::SamplerDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  auto it = g_cachedSamplers.find(id);
  if (it == g_cachedSamplers.end()) {
    it = g_cachedSamplers.try_emplace(id, g_device.CreateSampler(&descriptor)).first;
  }
  return it->second;
}

uint32_t align_uniform(uint32_t value) { return AURORA_ALIGN(value, g_cachedLimits.minUniformBufferOffsetAlignment); }

void insert_debug_marker(std::string label) {
#if defined(AURORA_GFX_DEBUG_GROUPS)
  auto idx = g_debugMarkers.size();
  g_debugMarkers.emplace_back(std::move(label));
  push_command(CommandType::DebugMarker, {.debugMarkerIndex = idx});
#endif
}

} // namespace aurora::gfx

void aurora::gfx::push_debug_group(std::string label) {
#if defined(AURORA_GFX_DEBUG_GROUPS)
  g_debugGroupStack.push_back(std::move(label));
#endif
}
void push_debug_group(const char* label) {
#ifdef AURORA_GFX_DEBUG_GROUPS
  aurora::gfx::g_debugGroupStack.emplace_back(label);
#endif
}
void pop_debug_group() {
#ifdef AURORA_GFX_DEBUG_GROUPS
  if (aurora::gfx::g_debugGroupStack.empty()) {
    aurora::gfx::Log.error("Debug group stack underflowed!");
    return;
  }

  aurora::gfx::g_debugGroupStack.pop_back();
#endif
}

const AuroraStats* aurora_get_stats() { return &aurora::gfx::g_stats; }

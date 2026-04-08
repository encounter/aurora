#include "common.hpp"

#include "clear.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "../gx/pipeline.hpp"
#include "tex_copy_conv.hpp"
#include "tex_palette_conv.hpp"
#include "texture_replacement.hpp"
#include "texture.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <ranges>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
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

constexpr uint64_t StagingBufferSize =
    UniformBufferSize + VertexBufferSize + IndexBufferSize + StorageBufferSize + TextureUploadSize;

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
struct SetScissorCommand {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;

  bool operator==(const SetScissorCommand& rhs) const { return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h; }
  bool operator!=(const SetScissorCommand& rhs) const { return !(*this == rhs); }
};
struct Command {
  CommandType type;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> debugGroupStack;
#endif
  union Data {
    Viewport setViewport;
    SetScissorCommand setScissor;
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
inline HashType xxh3_hash(const wgpu::BindGroupDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(wgpu::BindGroupDescriptor, layout); // skip nextInChain, label
  const auto hash = xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                                sizeof(wgpu::BindGroupDescriptor) - offset - sizeof(void*) /* skip entries */, seed);
  return xxh3_hash_s(input.entries, sizeof(wgpu::BindGroupEntry) * input.entryCount, hash);
}
template <>
inline HashType xxh3_hash(const wgpu::SamplerDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(wgpu::SamplerDescriptor, addressModeU); // skip nextInChain, label
  return xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                     sizeof(wgpu::SamplerDescriptor) - offset - 2 /* skip padding */, seed);
}
} // namespace aurora

namespace aurora::gfx {
using NewPipelineCallback = std::function<wgpu::RenderPipeline()>;
std::mutex g_pipelineMutex;
static bool g_hasPipelineThread = false;
static size_t g_pipelinesPerFrame = 0;
#ifdef NDEBUG
constexpr size_t BuildPipelinesPerFrame = 5;
#else
constexpr size_t BuildPipelinesPerFrame = 1;
#endif
static std::thread g_pipelineThread;
static std::atomic_bool g_pipelineThreadEnd;
static std::condition_variable g_pipelineCv;
static absl::flat_hash_map<PipelineRef, wgpu::RenderPipeline> g_pipelines;
static std::deque<std::pair<PipelineRef, NewPipelineCallback>> g_priorityPipelines;
static std::deque<std::pair<PipelineRef, NewPipelineCallback>> g_backgroundPipelines;
static absl::flat_hash_set<PipelineRef> g_pendingPipelines;
static absl::flat_hash_map<BindGroupRef, wgpu::BindGroup> g_cachedBindGroups;
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
static wgpu::Limits g_cachedLimits;

static PipelineRef g_currentPipeline;

// for imgui debug
AuroraStats g_stats{};
std::atomic_ref queuedPipelines{g_stats.queuedPipelines};
std::atomic_ref createdPipelines{g_stats.createdPipelines};

using CommandList = std::vector<Command>;
struct RenderPass {
  wgpu::TextureView colorView;
  wgpu::TextureView resolveView; // MSAA resolve target; null if msaaSamples == 1
  wgpu::TextureView depthView;
  wgpu::Texture copySourceTexture;
  wgpu::TextureView copySourceView;
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

static ByteBuffer g_serializedPipelines{};
static u32 g_serializedPipelineCount = 0;

template <typename PipelineConfig>
static void serialize_pipeline_config(ShaderType type, const PipelineConfig& config) {
  static_assert(std::has_unique_object_representations_v<PipelineConfig>);
  g_serializedPipelines.append(type);
  g_serializedPipelines.append<u32>(sizeof(config));
  g_serializedPipelines.append(config);
  ++g_serializedPipelineCount;
}

template <typename PipelineConfig>
static PipelineRef find_pipeline(ShaderType type, const PipelineConfig& config, NewPipelineCallback&& cb,
                                 bool serialize = true) {
  PipelineRef hash = xxh3_hash(config, static_cast<HashType>(type));
  bool found = false;
  {
    std::scoped_lock guard{g_pipelineMutex};
    found = g_pipelines.contains(hash);
    if (!found && g_pendingPipelines.contains(hash)) {
      found = true;
      // Promote from background to priority if requested during a frame
      if (g_currentRenderPass != UINT32_MAX) {
        auto it = std::find_if(g_backgroundPipelines.begin(), g_backgroundPipelines.end(),
                               [=](const auto& v) { return v.first == hash; });
        if (it != g_backgroundPipelines.end()) {
          g_priorityPipelines.emplace_back(std::move(*it));
          g_backgroundPipelines.erase(it);
        }
      }
    }
    if (!found) {
      if (!g_hasPipelineThread && g_pipelinesPerFrame < BuildPipelinesPerFrame) {
        g_pipelines.try_emplace(hash, cb());
        if (serialize) {
          serialize_pipeline_config(type, config);
        }
        ++g_pipelinesPerFrame;
        createdPipelines++;
        found = true;
      } else {
        bool isFrameRequest = g_currentRenderPass != UINT32_MAX;
        auto& targetQueue = isFrameRequest ? g_priorityPipelines : g_backgroundPipelines;
        targetQueue.emplace_back(std::pair{hash, std::move(cb)});
        g_pendingPipelines.insert(hash);
        if (serialize) {
          serialize_pipeline_config(type, config);
        }
      }
    }
  }
  if (!found) {
    g_pipelineCv.notify_one();
    queuedPipelines++;
  }
  return hash;
}

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
  ++g_stats.drawCallCount;
}

static Viewport g_cachedViewport;
const Viewport& get_viewport() noexcept { return g_cachedViewport; }

void set_viewport(float left, float top, float width, float height, float znear, float zfar) noexcept {
  Viewport cmd{left, top, width, height, znear, zfar};
  if (cmd != g_cachedViewport) {
    push_command(CommandType::SetViewport, Command::Data{.setViewport = cmd});
    g_cachedViewport = cmd;
  }
}

static SetScissorCommand g_cachedScissor;
void set_scissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) noexcept {
  SetScissorCommand cmd{x, y, w, h};
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
PipelineRef pipeline_ref(clear::PipelineConfig config) {
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

void clear_offscreen_cache() { g_offscreenCache.clear(); }

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

  push_command(CommandType::SetViewport, Command::Data{.setViewport = {0.f, 0.f, static_cast<float>(width),
                                                                       static_cast<float>(height), 0.f, 1.f}});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = {0, 0, width, height}});
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

  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
}

template <>
void push_draw_command(gx::DrawData data) {
  push_draw_command(ShaderDrawCommand{.type = ShaderType::GX, .gx = data});
}

template <>
PipelineRef pipeline_ref(gx::PipelineConfig config) {
  return find_pipeline(ShaderType::GX, config, [=] { return create_pipeline(config); });
}

static void pipeline_worker() {
#ifdef TRACY_ENABLE
  tracy::SetThreadName("Pipeline compilation thread");
#endif

  bool hasMore = false;
  while (g_hasPipelineThread || g_pipelinesPerFrame < BuildPipelinesPerFrame) {
    std::pair<PipelineRef, NewPipelineCallback> cb;
    {
      std::unique_lock lock{g_pipelineMutex};
      if (g_hasPipelineThread) {
        if (!hasMore) {
          g_pipelineCv.wait(lock, [] {
            return !g_priorityPipelines.empty() || !g_backgroundPipelines.empty() || g_pipelineThreadEnd;
          });
        }
      } else if (g_priorityPipelines.empty() && g_backgroundPipelines.empty()) {
        return;
      }
      if (g_pipelineThreadEnd) {
        break;
      }
      auto& source = !g_priorityPipelines.empty() ? g_priorityPipelines : g_backgroundPipelines;
      cb = std::move(source.front());
      source.pop_front();
    }
    auto result = cb.second();
    {
      std::lock_guard lock{g_pipelineMutex};
      g_pipelines.try_emplace(cb.first, std::move(result));
      g_pendingPipelines.erase(cb.first);
      hasMore = !g_priorityPipelines.empty() || !g_backgroundPipelines.empty();
    }
    if (!g_hasPipelineThread) {
      ++g_pipelinesPerFrame;
    }
    ++createdPipelines;
    --queuedPipelines;
  }
}

// Load serialized pipeline cache
void load_pipeline_cache() {
  ByteBuffer pipelineCache;
  u32 pipelineCacheCount = 0;

  {
    std::string path = std::string{g_config.configPath} + "/pipeline_cache.bin";
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (file) {
      const auto size = file.tellg();
      file.seekg(0, std::ios::beg);
      constexpr size_t headerSize = sizeof(pipelineCacheCount);
      if (size != -1 && size > headerSize) {
        pipelineCache.append_zeroes(size_t(size) - headerSize);
        file.read(reinterpret_cast<char*>(&pipelineCacheCount), headerSize);
        file.read(reinterpret_cast<char*>(pipelineCache.data()), size_t(size) - headerSize);
      }
    }
  }

  if (pipelineCacheCount > 0) {
    size_t offset = 0;
    while (offset < pipelineCache.size()) {
      ShaderType type = *reinterpret_cast<const ShaderType*>(pipelineCache.data() + offset);
      offset += sizeof(ShaderType);
      u32 size = *reinterpret_cast<const u32*>(pipelineCache.data() + offset);
      offset += sizeof(u32);
      switch (type) {
      case ShaderType::Clear: {
        if (size != sizeof(clear::PipelineConfig)) {
          break;
        }
        const auto config = *reinterpret_cast<const clear::PipelineConfig*>(pipelineCache.data() + offset);
        if (config.version != clear::ClearPipelineConfigVersion) {
          break;
        }
        find_pipeline(type, config, [=] { return clear::create_pipeline(config); }, true);
        break;
      }
      case ShaderType::GX: {
        if (size != sizeof(gx::PipelineConfig)) {
          break;
        }
        const auto config = *reinterpret_cast<const gx::PipelineConfig*>(pipelineCache.data() + offset);
        if (config.version != gx::GXPipelineConfigVersion) {
          break;
        }
        find_pipeline(type, config, [=] { return gx::create_pipeline(config); }, true);
        break;
      }
      default:
        Log.warn("Unknown pipeline type {}", underlying(type));
        break;
      }
      offset += size;
    }
  }
}

// Write serialized pipelines to file
void save_pipeline_cache() {
  const auto path = std::string{g_config.configPath} + "/pipeline_cache.bin";
  std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (file) {
    file.write(reinterpret_cast<const char*>(&g_serializedPipelineCount), sizeof(g_serializedPipelineCount));
    file.write(reinterpret_cast<const char*>(g_serializedPipelines.data()), g_serializedPipelines.size());
  }
  g_serializedPipelines.clear();
  g_serializedPipelineCount = 0;
}

void initialize() {
  tex_copy_conv::initialize();
  tex_palette_conv::initialize();
  texture_replacement::initialize();

  // No async pipelines for OpenGL (ES)
  if (webgpu::g_backendType == wgpu::BackendType::OpenGL || webgpu::g_backendType == wgpu::BackendType::OpenGLES ||
      webgpu::g_backendType == wgpu::BackendType::WebGPU) {
    g_hasPipelineThread = false;
  } else {
    g_pipelineThreadEnd = false;
    g_hasPipelineThread = true;
    g_pipelineThread = std::thread(pipeline_worker);
  }

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
  map_staging_buffer();

  gx::initialize();

  load_pipeline_cache();
}

void shutdown() {
  if (g_hasPipelineThread) {
    g_pipelineThreadEnd = true;
    g_pipelineCv.notify_all();
    g_pipelineThread.join();
  }

  save_pipeline_cache();

  tex_copy_conv::shutdown();
  tex_palette_conv::shutdown();
  texture_replacement::shutdown();
  gx::shutdown();

  g_textureUploads.clear();
  g_cachedBindGroups.clear();
  g_cachedSamplers.clear();
  g_pipelines.clear();
  g_priorityPipelines.clear();
  g_backgroundPipelines.clear();
  g_pendingPipelines.clear();
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
  g_inOffscreen = false;

  queuedPipelines = 0;
  createdPipelines = 0;
}

static size_t currentStagingBuffer = 0;
static bool bufferMapped = false;
void map_staging_buffer() {
  bufferMapped = false;
  g_stagingBuffers[currentStagingBuffer].MapAsync(
      wgpu::MapMode::Write, 0, StagingBufferSize, wgpu::CallbackMode::AllowSpontaneous,
      [](wgpu::MapAsyncStatus status, wgpu::StringView message) {
        if (status == wgpu::MapAsyncStatus::CallbackCancelled || status == wgpu::MapAsyncStatus::Aborted) {
          Log.warn("Buffer mapping {}: {}", magic_enum::enum_name(status), message);
          return;
        }
        ASSERT(status == wgpu::MapAsyncStatus::Success, "Buffer mapping failed: {} {}", magic_enum::enum_name(status),
               message);
        bufferMapped = true;
      });
}

void begin_frame() {
  ZoneScoped;
  {
    ZoneScopedN("Wait for buffer map");
    while (!bufferMapped) {
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
  mapBuffer(g_textureUpload, TextureUploadSize);

  g_stats.drawCallCount = 0;
  g_stats.mergedDrawCallCount = 0;
  g_suspendedEfbPass.reset();

  g_renderPasses.emplace_back();
  set_efb_targets(g_renderPasses[0]);
  g_renderPasses[0].clearColorValue = gx::g_gxState.clearColor;
  g_renderPasses[0].clearDepthValue = gx::clear_depth_value();
  g_currentRenderPass = 0;
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});

  if (!g_hasPipelineThread) {
    g_pipelinesPerFrame = 0;
  }
}

void end_frame(const wgpu::CommandEncoder& cmd) {
  ZoneScoped;
  ASSERT(!g_inOffscreen, "end_frame called while offscreen rendering is active");
  uint64_t bufferOffset = 0;
  const auto writeBuffer = [&](ByteBuffer& buf, wgpu::Buffer& out, uint64_t size, std::string_view label) {
    const auto writeSize = buf.size(); // Only need to copy this many bytes
    if (writeSize > 0) {
      cmd.CopyBufferToBuffer(g_stagingBuffers[currentStagingBuffer], bufferOffset, out, 0, ALIGN(writeSize, 4));
      buf.clear();
    }
    bufferOffset += size;
    return writeSize;
  };
  g_stagingBuffers[currentStagingBuffer].Unmap();
  g_stats.lastVertSize = writeBuffer(g_verts, g_vertexBuffer, VertexBufferSize, "Vertex");
  g_stats.lastUniformSize = writeBuffer(g_uniforms, g_uniformBuffer, UniformBufferSize, "Uniform");
  g_stats.lastIndexSize = writeBuffer(g_indices, g_indexBuffer, IndexBufferSize, "Index");
  g_stats.lastStorageSize = writeBuffer(g_storage, g_storageBuffer, StorageBufferSize, "Storage");
  g_stats.lastTextureUploadSize = g_textureUpload.size();
  {
    // Perform texture copies
    for (const auto& item : g_textureUploads) {
      const wgpu::TexelCopyBufferInfo buf{
          .layout =
              wgpu::TexelCopyBufferLayout{
                  .offset = item.layout.offset + bufferOffset,
                  .bytesPerRow = ALIGN(item.layout.bytesPerRow, 256),
                  .rowsPerImage = item.layout.rowsPerImage,
              },
          .buffer = g_stagingBuffers[currentStagingBuffer],
      };
      cmd.CopyBufferToTexture(&buf, &item.tex, &item.size);
    }
    g_textureUploads.clear();
    g_textureUpload.clear();
  }
  currentStagingBuffer = (currentStagingBuffer + 1) % g_stagingBuffers.size();
  map_staging_buffer();
  g_currentRenderPass = UINT32_MAX;
  for (auto& array : gx::g_gxState.arrays) {
    array.cachedRange = {};
  }

  if (!g_hasPipelineThread) {
    pipeline_worker();
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

    if (passInfo.resolveTarget) {
      const auto& dstSize = passInfo.resolveTarget->size;
      const bool needsConversion = tex_copy_conv::needs_conversion(passInfo.resolveFormat);
      const bool needsScaling = dstSize.width != static_cast<uint32_t>(passInfo.resolveRect.width) ||
                                dstSize.height != static_cast<uint32_t>(passInfo.resolveRect.height);
      const tex_copy_conv::ConvRequest convReq{
          .fmt = passInfo.resolveFormat,
          .srcView = passInfo.copySourceView,
          .uniformRange = passInfo.resolveUniformRange,
          .dst = passInfo.resolveTarget,
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
  g_cachedBindGroups.clear();

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

void render_pass(const wgpu::RenderPassEncoder& pass, u32 idx) {
  g_currentPipeline = UINTPTR_MAX;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> lastDebugGroupStack;
#endif

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
      const auto x = std::clamp(sc.x, 0u, size.width);
      const auto y = std::clamp(sc.y, 0u, size.height);
      const auto w = std::clamp(sc.w, 0u, size.width - x);
      const auto h = std::clamp(sc.h, 0u, size.height - y);
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
  std::lock_guard guard{g_pipelineMutex};
  const auto it = g_pipelines.find(ref);
  if (it == g_pipelines.end()) {
    return false;
  }
  pass.SetPipeline(it->second);
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
  const auto copyBytesPerRow = ALIGN(bytesPerRow, 256);
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

// TODO: should we avoid caching bind groups altogether?
BindGroupRef bind_group_ref(const wgpu::BindGroupDescriptor& descriptor) {
#ifdef EMSCRIPTEN
  const auto bg = g_device.CreateBindGroup(&descriptor);
  BindGroupRef id = reinterpret_cast<BindGroupRef>(bg.Get());
  g_cachedBindGroups.try_emplace(id, bg);
#else
  const auto id = xxh3_hash(descriptor);
  if (!g_cachedBindGroups.contains(id)) {
    g_cachedBindGroups.try_emplace(id, g_device.CreateBindGroup(&descriptor));
  }
#endif
  return id;
}

wgpu::BindGroup find_bind_group(BindGroupRef id) {
#ifdef EMSCRIPTEN
  return g_cachedBindGroups[id];
#else
  const auto it = g_cachedBindGroups.find(id);
  CHECK(it != g_cachedBindGroups.end(), "get_bind_group: failed to locate {:x}", id);
  return it->second;
#endif
}

wgpu::Sampler sampler_ref(const wgpu::SamplerDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  auto it = g_cachedSamplers.find(id);
  if (it == g_cachedSamplers.end()) {
    it = g_cachedSamplers.try_emplace(id, g_device.CreateSampler(&descriptor)).first;
  }
  return it->second;
}

uint32_t align_uniform(uint32_t value) { return ALIGN(value, g_cachedLimits.minUniformBufferOffsetAlignment); }

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

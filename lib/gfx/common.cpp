#include "common.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "model/shader.hpp"
#include "stream/shader.hpp"
#include "texture.hpp"

#include <absl/container/flat_hash_map.h>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <thread>
#include <mutex>
#include <magic_enum.hpp>

namespace aurora::gfx {
static Module Log("aurora::gfx");

using webgpu::g_device;
using webgpu::g_instance;
using webgpu::g_queue;

#ifdef AURORA_GFX_DEBUG_GROUPS
std::vector<std::string> g_debugGroupStack;
#endif

constexpr uint64_t UniformBufferSize = 3145728;  // 3mb
constexpr uint64_t VertexBufferSize = 3145728;   // 3mb
constexpr uint64_t IndexBufferSize = 1048576;    // 1mb
constexpr uint64_t StorageBufferSize = 8388608;  // 8mb
constexpr uint64_t TextureUploadSize = 25165824; // 24mb

constexpr uint64_t StagingBufferSize =
    UniformBufferSize + VertexBufferSize + IndexBufferSize + StorageBufferSize + TextureUploadSize;

struct ShaderState {
  stream::State stream;
  model::State model;
};
struct ShaderDrawCommand {
  ShaderType type;
  union {
    stream::DrawData stream;
    model::DrawData model;
  };
};
enum class CommandType {
  SetViewport,
  SetScissor,
  Draw,
};
struct Command {
  CommandType type;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> debugGroupStack;
#endif
  union Data {
    struct SetViewportCommand {
      float left;
      float top;
      float width;
      float height;
      float znear;
      float zfar;

      bool operator==(const SetViewportCommand& rhs) const {
        return left == rhs.left && top == rhs.top && width == rhs.width && height == rhs.height && znear == rhs.znear &&
               zfar == rhs.zfar;
      }
      bool operator!=(const SetViewportCommand& rhs) const { return !(*this == rhs); }
    } setViewport;
    struct SetScissorCommand {
      uint32_t x;
      uint32_t y;
      uint32_t w;
      uint32_t h;

      bool operator==(const SetScissorCommand& rhs) const {
        return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h;
      }
      bool operator!=(const SetScissorCommand& rhs) const { return !(*this == rhs); }
    } setScissor;
    ShaderDrawCommand draw;
  } data;
};
} // namespace aurora::gfx

namespace aurora {
// For types that we can't ensure are safe to hash with has_unique_object_representations,
// we create specialized methods to handle them. Note that these are highly dependent on
// the structure definition, which could easily change with Dawn updates.
template <>
inline HashType xxh3_hash(const wgpu::BindGroupDescriptor& input, HashType seed) {
  constexpr auto offset = sizeof(void*) * 2; // skip nextInChain, label
  const auto hash = xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                                sizeof(wgpu::BindGroupDescriptor) - offset - sizeof(void*) /* skip entries */, seed);
  return xxh3_hash_s(input.entries, sizeof(wgpu::BindGroupEntry) * input.entryCount, hash);
}
template <>
inline HashType xxh3_hash(const wgpu::SamplerDescriptor& input, HashType seed) {
  constexpr auto offset = sizeof(void*) * 2; // skip nextInChain, label
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
static std::deque<std::pair<PipelineRef, NewPipelineCallback>> g_queuedPipelines;
static absl::flat_hash_map<BindGroupRef, wgpu::BindGroup> g_cachedBindGroups;
static absl::flat_hash_map<SamplerRef, wgpu::Sampler> g_cachedSamplers;
std::atomic_uint32_t queuedPipelines;
std::atomic_uint32_t createdPipelines;

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
static wgpu::SupportedLimits g_cachedLimits;

static ShaderState g_state;
static PipelineRef g_currentPipeline;

// for imgui debug
size_t g_drawCallCount;
size_t g_mergedDrawCallCount;
size_t g_lastVertSize;
size_t g_lastUniformSize;
size_t g_lastIndexSize;
size_t g_lastStorageSize;

using CommandList = std::vector<Command>;
struct RenderPass {
  TextureHandle resolveTarget;
  ClipRect resolveRect;
  Vec4<float> clearColor{0.f, 0.f, 0.f, 0.f};
  CommandList commands;
  bool clear = true;
};
static std::vector<RenderPass> g_renderPasses;
static u32 g_currentRenderPass = UINT32_MAX;
std::vector<TextureUpload> g_textureUploads;

static ByteBuffer g_serializedPipelines{};
static u32 g_serializedPipelineCount = 0;

template <typename PipelineConfig>
static void serialize_pipeline_config(ShaderType type, const PipelineConfig& config) {
  static_assert(std::has_unique_object_representations_v<PipelineConfig>);
  g_serializedPipelines.append(&type, sizeof(type));
  const u32 configSize = sizeof(config);
  g_serializedPipelines.append(&configSize, sizeof(configSize));
  g_serializedPipelines.append(&config, configSize);
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
    if (!found) {
      const auto ref =
          std::find_if(g_queuedPipelines.begin(), g_queuedPipelines.end(), [=](auto v) { return v.first == hash; });
      if (g_hasPipelineThread) {
        if (ref != g_queuedPipelines.end()) {
          found = true;
        }
      } else {
        if (ref != g_queuedPipelines.end()) {
          found = true;
        } else if (g_pipelinesPerFrame < BuildPipelinesPerFrame) {
          g_pipelines.try_emplace(hash, cb());
          if (serialize) {
            serialize_pipeline_config(type, config);
          }
          ++g_pipelinesPerFrame;
          createdPipelines++;
          found = true;
        }
      }
    }
    if (!found) {
      g_queuedPipelines.emplace_back(std::pair{hash, std::move(cb)});
      if (serialize) {
        serialize_pipeline_config(type, config);
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
      Log.report(LOG_WARNING, FMT_STRING("Dropping command {}"), magic_enum::enum_name(type));
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
static inline Command& get_last_draw_command(ShaderType type) {
  CHECK(g_currentRenderPass != UINT32_MAX, "No last command");
  auto& last = g_renderPasses[g_currentRenderPass].commands.back();
  if (last.type != CommandType::Draw || last.data.draw.type != type)
    UNLIKELY {
      FATAL("Last command invalid: {} {}, expected {} {}", magic_enum::enum_name(last.type),
            magic_enum::enum_name(last.data.draw.type), magic_enum::enum_name(CommandType::Draw),
            magic_enum::enum_name(type));
    }
  return last;
}

static void push_draw_command(ShaderDrawCommand data) {
  push_command(CommandType::Draw, Command::Data{.draw = data});
  ++g_drawCallCount;
}

static Command::Data::SetViewportCommand g_cachedViewport;
void set_viewport(float left, float top, float width, float height, float znear, float zfar) noexcept {
  Command::Data::SetViewportCommand cmd{left, top, width, height, znear, zfar};
  if (cmd != g_cachedViewport) {
    push_command(CommandType::SetViewport, Command::Data{.setViewport = cmd});
    g_cachedViewport = cmd;
  }
}
static Command::Data::SetScissorCommand g_cachedScissor;
void set_scissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) noexcept {
  Command::Data::SetScissorCommand cmd{x, y, w, h};
  if (cmd != g_cachedScissor) {
    push_command(CommandType::SetScissor, Command::Data{.setScissor = cmd});
    g_cachedScissor = cmd;
  }
}

void resolve_pass(TextureHandle texture, ClipRect rect, bool clear, Vec4<float> clearColor) {
  auto& currentPass = aurora::gfx::g_renderPasses[g_currentRenderPass];
  currentPass.resolveTarget = std::move(texture);
  currentPass.resolveRect = rect;
  auto& newPass = g_renderPasses.emplace_back();
  newPass.clearColor = clearColor;
  newPass.clear = clear;
  ++g_currentRenderPass;
}

template <>
const stream::State& get_state() {
  return g_state.stream;
}
template <>
void push_draw_command(stream::DrawData data) {
  push_draw_command(ShaderDrawCommand{.type = ShaderType::Stream, .stream = data});
}
template <>
void merge_draw_command(stream::DrawData data) {
  auto& last = get_last_draw_command(ShaderType::Stream).data.draw.stream;
  CHECK(last.vertRange.offset + last.vertRange.size == data.vertRange.offset, "Invalid vertex merge range: {} -> {}",
        last.vertRange.offset + last.vertRange.size, data.vertRange.offset);
  CHECK(last.indexRange.offset + last.indexRange.size == data.indexRange.offset, "Invalid index merge range: {} -> {}",
        last.indexRange.offset + last.indexRange.size, data.indexRange.offset);
  last.vertRange.size += data.vertRange.size;
  last.indexRange.size += data.indexRange.size;
  last.indexCount += data.indexCount;
  ++g_mergedDrawCallCount;
}
template <>
PipelineRef pipeline_ref(stream::PipelineConfig config) {
  return find_pipeline(ShaderType::Stream, config, [=]() { return create_pipeline(g_state.stream, config); });
}

template <>
void push_draw_command(model::DrawData data) {
  push_draw_command(ShaderDrawCommand{.type = ShaderType::Model, .model = data});
}
template <>
PipelineRef pipeline_ref(model::PipelineConfig config) {
  return find_pipeline(ShaderType::Model, config, [=]() { return create_pipeline(g_state.model, config); });
}

static void pipeline_worker() {
  bool hasMore = false;
  while (g_hasPipelineThread || g_pipelinesPerFrame < BuildPipelinesPerFrame) {
    std::pair<PipelineRef, NewPipelineCallback> cb;
    {
      std::unique_lock lock{g_pipelineMutex};
      if (g_hasPipelineThread) {
        if (!hasMore) {
          g_pipelineCv.wait(lock, [] { return !g_queuedPipelines.empty() || g_pipelineThreadEnd; });
        }
      } else if (g_queuedPipelines.empty()) {
        return;
      }
      if (g_pipelineThreadEnd) {
        break;
      }
      cb = std::move(g_queuedPipelines.front());
    }
    auto result = cb.second();
    // std::this_thread::sleep_for(std::chrono::milliseconds{1500});
    {
      std::scoped_lock lock{g_pipelineMutex};
      ASSERT(g_pipelines.try_emplace(cb.first, std::move(result)).second, "Duplicate pipeline {}", cb.first);
      g_queuedPipelines.pop_front();
      hasMore = !g_queuedPipelines.empty();
    }
    if (!g_hasPipelineThread) {
      ++g_pipelinesPerFrame;
    }
    createdPipelines++;
    queuedPipelines--;
  }
}

void initialize() {
  // No async pipelines for OpenGL (ES)
  if (webgpu::g_backendType == wgpu::BackendType::OpenGL || webgpu::g_backendType == wgpu::BackendType::OpenGLES ||
      webgpu::g_backendType == wgpu::BackendType::WebGPU) {
    g_hasPipelineThread = false;
  } else {
    g_pipelineThreadEnd = false;
    g_pipelineThread = std::thread(pipeline_worker);
    g_hasPipelineThread = true;
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
  createBuffer(g_vertexBuffer, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst, VertexBufferSize,
               "Shared Vertex Buffer");
  createBuffer(g_indexBuffer, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, IndexBufferSize,
               "Shared Index Buffer");
  createBuffer(g_storageBuffer, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, StorageBufferSize,
               "Shared Storage Buffer");
  for (int i = 0; i < g_stagingBuffers.size(); ++i) {
    const auto label = fmt::format(FMT_STRING("Staging Buffer {}"), i);
    createBuffer(g_stagingBuffers[i], wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc, StagingBufferSize,
                 label.c_str());
  }
  map_staging_buffer();

  g_state.stream = stream::construct_state();
  g_state.model = model::construct_state();

  {
    // Load serialized pipeline cache
    std::string path = std::string{g_config.configPath} + "/pipeline_cache.bin";
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (file) {
      const auto size = file.tellg();
      file.seekg(0, std::ios::beg);
      constexpr size_t headerSize = sizeof(g_serializedPipelineCount);
      if (size != -1 && size > headerSize) {
        g_serializedPipelines.append_zeroes(size_t(size) - headerSize);
        file.read(reinterpret_cast<char*>(&g_serializedPipelineCount), headerSize);
        file.read(reinterpret_cast<char*>(g_serializedPipelines.data()), size_t(size) - headerSize);
      }
    }
  }
  if (g_serializedPipelineCount > 0) {
    size_t offset = 0;
    while (offset < g_serializedPipelines.size()) {
      ShaderType type = *reinterpret_cast<const ShaderType*>(g_serializedPipelines.data() + offset);
      offset += sizeof(ShaderType);
      u32 size = *reinterpret_cast<const u32*>(g_serializedPipelines.data() + offset);
      offset += sizeof(u32);
      switch (type) {
      case ShaderType::Stream: {
        if (size != sizeof(stream::PipelineConfig)) {
          break;
        }
        const auto config = *reinterpret_cast<const stream::PipelineConfig*>(g_serializedPipelines.data() + offset);
        if (config.version != gx::GXPipelineConfigVersion) {
          break;
        }
        find_pipeline(
            type, config, [=]() { return stream::create_pipeline(g_state.stream, config); }, false);
      } break;
      case ShaderType::Model: {
        if (size != sizeof(model::PipelineConfig)) {
          break;
        }
        const auto config = *reinterpret_cast<const model::PipelineConfig*>(g_serializedPipelines.data() + offset);
        if (config.version != gx::GXPipelineConfigVersion) {
          break;
        }
        find_pipeline(
            type, config, [=]() { return model::create_pipeline(g_state.model, config); }, false);
      } break;
      default:
        Log.report(LOG_WARNING, FMT_STRING("Unknown pipeline type {}"), static_cast<int>(type));
        break;
      }
      offset += size;
    }
  }
}

void shutdown() {
  if (g_hasPipelineThread) {
    g_pipelineThreadEnd = true;
    g_pipelineCv.notify_all();
    g_pipelineThread.join();
  }

  {
    // Write serialized pipelines to file
    const auto path = std::string{g_config.configPath} + "pipeline_cache.bin";
    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (file) {
      file.write(reinterpret_cast<const char*>(&g_serializedPipelineCount), sizeof(g_serializedPipelineCount));
      file.write(reinterpret_cast<const char*>(g_serializedPipelines.data()), g_serializedPipelines.size());
    }
    g_serializedPipelines.clear();
    g_serializedPipelineCount = 0;
  }

  gx::shutdown();

  g_textureUploads.clear();
  g_cachedBindGroups.clear();
  g_cachedSamplers.clear();
  g_pipelines.clear();
  g_queuedPipelines.clear();
  g_vertexBuffer = {};
  g_uniformBuffer = {};
  g_indexBuffer = {};
  g_storageBuffer = {};
  g_stagingBuffers.fill({});
  g_renderPasses.clear();
  g_currentRenderPass = UINT32_MAX;

  g_state = {};

  queuedPipelines = 0;
  createdPipelines = 0;
}

static size_t currentStagingBuffer = 0;
static bool bufferMapped = false;
void map_staging_buffer() {
  bufferMapped = false;
  g_stagingBuffers[currentStagingBuffer].MapAsync(
      wgpu::MapMode::Write, 0, StagingBufferSize,
      [](WGPUBufferMapAsyncStatus status, void* userdata) {
        if (status == WGPUBufferMapAsyncStatus_DestroyedBeforeCallback) {
          return;
        }
        ASSERT(status == WGPUBufferMapAsyncStatus_Success, "Buffer mapping failed: {}", static_cast<int>(status));
        *static_cast<bool*>(userdata) = true;
      },
      &bufferMapped);
}

void begin_frame() {
  while (!bufferMapped) {
#ifdef EMSCRIPTEN
    emscripten_sleep(0);
#else
    g_device.Tick();
#endif
  }
  size_t bufferOffset = 0;
  auto& stagingBuf = g_stagingBuffers[currentStagingBuffer];
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

  g_drawCallCount = 0;
  g_mergedDrawCallCount = 0;

  g_renderPasses.emplace_back();
  g_renderPasses[0].clearColor = gx::g_gxState.clearColor;
  g_currentRenderPass = 0;
  // push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  // push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});

  if (!g_hasPipelineThread) {
    g_pipelinesPerFrame = 0;
  }
}

void end_frame(const wgpu::CommandEncoder& cmd) {
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
  g_lastVertSize = writeBuffer(g_verts, g_vertexBuffer, VertexBufferSize, "Vertex");
  g_lastUniformSize = writeBuffer(g_uniforms, g_uniformBuffer, UniformBufferSize, "Uniform");
  g_lastIndexSize = writeBuffer(g_indices, g_indexBuffer, IndexBufferSize, "Index");
  g_lastStorageSize = writeBuffer(g_storage, g_storageBuffer, StorageBufferSize, "Storage");
  {
    // Perform texture copies
    for (const auto& item : g_textureUploads) {
      const wgpu::ImageCopyBuffer buf{
          .layout =
              wgpu::TextureDataLayout{
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

  if (!g_hasPipelineThread) {
    pipeline_worker();
  }
}

void render(wgpu::CommandEncoder& cmd) {
  for (u32 i = 0; i < g_renderPasses.size(); ++i) {
    const auto& passInfo = g_renderPasses[i];
    if (i == g_renderPasses.size() - 1) {
      ASSERT(!passInfo.resolveTarget, "Final render pass must not have resolve target");
    }
    const std::array attachments{
        wgpu::RenderPassColorAttachment{
            .view = webgpu::g_frameBuffer.view,
            .resolveTarget = webgpu::g_graphicsConfig.msaaSamples > 1 ? webgpu::g_frameBufferResolved.view : nullptr,
            .loadOp = passInfo.clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue =
                {
                    .r = passInfo.clearColor.x(),
                    .g = passInfo.clearColor.y(),
                    .b = passInfo.clearColor.z(),
                    .a = passInfo.clearColor.w(),
                },
        },
    };
    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment{
        .view = webgpu::g_depthBuffer.view,
        .depthLoadOp = passInfo.clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = 1.f,
    };
    const auto label = fmt::format(FMT_STRING("Render pass {}"), i);
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
      wgpu::ImageCopyTexture src{
          .origin =
              wgpu::Origin3D{
                  .x = static_cast<uint32_t>(passInfo.resolveRect.x),
                  .y = static_cast<uint32_t>(passInfo.resolveRect.y),
              },
      };
      if (webgpu::g_graphicsConfig.msaaSamples > 1) {
        src.texture = webgpu::g_frameBufferResolved.texture;
      } else {
        src.texture = webgpu::g_frameBuffer.texture;
      }
      const wgpu::ImageCopyTexture dst{
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
  g_renderPasses.clear();
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
      pass.SetViewport(vp.left, vp.top, vp.width, vp.height, vp.znear, vp.zfar);
    } break;
    case CommandType::SetScissor: {
      const auto& sc = cmd.data.setScissor;
      pass.SetScissorRect(sc.x, sc.y, sc.w, sc.h);
    } break;
    case CommandType::Draw: {
      const auto& draw = cmd.data.draw;
      switch (draw.type) {
      case ShaderType::Stream:
        stream::render(g_state.stream, draw.stream, pass);
        break;
      case ShaderType::Model:
        model::render(g_state.model, draw.model, pass);
        break;
      }
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
    padding = alignment - length % alignment;
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
    padding = alignment - length % alignment;
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
  return push(g_uniforms, data, length, g_cachedLimits.limits.minUniformBufferOffsetAlignment);
}
Range push_storage(const uint8_t* data, size_t length) {
  return push(g_storage, data, length, g_cachedLimits.limits.minStorageBufferOffsetAlignment);
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
  const auto range = map(g_uniforms, length, g_cachedLimits.limits.minUniformBufferOffsetAlignment);
  return {ByteBuffer{g_uniforms.data() + range.offset, range.size}, range};
}
std::pair<ByteBuffer, Range> map_storage(size_t length) {
  const auto range = map(g_storage, length, g_cachedLimits.limits.minStorageBufferOffsetAlignment);
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
const wgpu::BindGroup& find_bind_group(BindGroupRef id) {
#ifdef EMSCRIPTEN
  return g_cachedBindGroups[id];
#else
  const auto it = g_cachedBindGroups.find(id);
  CHECK(it != g_cachedBindGroups.end(), "get_bind_group: failed to locate {:x}", id);
  return it->second;
#endif
}

const wgpu::Sampler& sampler_ref(const wgpu::SamplerDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  auto it = g_cachedSamplers.find(id);
  if (it == g_cachedSamplers.end()) {
    it = g_cachedSamplers.try_emplace(id, g_device.CreateSampler(&descriptor)).first;
  }
  return it->second;
}

uint32_t align_uniform(uint32_t value) { return ALIGN(value, g_cachedLimits.limits.minUniformBufferOffsetAlignment); }
} // namespace aurora::gfx

void push_debug_group(const char* label) {
#ifdef AURORA_GFX_DEBUG_GROUPS
  aurora::gfx::g_debugGroupStack.emplace_back(label);
#endif
}
void pop_debug_group() {
#ifdef AURORA_GFX_DEBUG_GROUPS
  aurora::gfx::g_debugGroupStack.pop_back();
#endif
}

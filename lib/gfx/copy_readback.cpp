#include "copy_readback.hpp"

#include "copy_readback_layout.hpp"

#include "../gx/fifo.hpp"
#include "../gx/gx.hpp"
#include "../webgpu/gpu.hpp"
#include "texture.hpp"

#include <aurora/gfx.hpp>

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

#include <magic_enum.hpp>
#include <tracy/Tracy.hpp>

namespace aurora::gfx::copy_readback {
namespace {
Module Log("aurora::gfx::copy_readback");

using webgpu::g_device;

constexpr size_t SlotCount = 3;
constexpr uint32_t WorkgroupSize = 64;
// A slot that has not completed after this many requests for its source is
// assumed lost (e.g. its frame was dropped) and is recycled.
constexpr uint64_t StaleSlotRequests = 60;

struct Params {
  uint32_t logicalWidth = 0;
  uint32_t logicalHeight = 0;
  uint32_t srcWidth = 0;
  uint32_t srcHeight = 0;
  uint32_t paddedWidth = 0;
  uint32_t wordCount = 0;
  uint32_t _pad0 = 0;
  uint32_t _pad1 = 0;
};
static_assert(sizeof(Params) == 32);

constexpr std::string_view ShaderSource = R"(
struct Params {
    logicalSize: vec2u,
    srcSize: vec2u,
    paddedWidth: u32,
    wordCount: u32,
    _pad: vec2u,
};

@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var<storage, read_write> out_words: array<u32>;
@group(0) @binding(2) var<uniform> params: Params;

fn texel_byte(x: u32, y: u32) -> u32 {
    if (x >= params.logicalSize.x || y >= params.logicalSize.y) {
        return 0u;
    }
    let sx = min(u32((f32(x) + 0.5) * f32(params.srcSize.x) / f32(params.logicalSize.x)), params.srcSize.x - 1u);
    let sy = min(u32((f32(y) + 0.5) * f32(params.srcSize.y) / f32(params.logicalSize.y)), params.srcSize.y - 1u);
    let v = textureLoad(src, vec2u(sx, sy), 0).r;
    return u32(clamp(v, 0.0, 1.0) * 255.0 + 0.5);
}

@compute @workgroup_size(64, 1, 1)
fn cs_main(@builtin(global_invocation_id) id: vec3u) {
    let word = id.x;
    if (word >= params.wordCount) {
        return;
    }
    let tilesPerRow = params.paddedWidth / 8u;
    var packed = 0u;
    for (var i = 0u; i < 4u; i++) {
        let offset = word * 4u + i;
        let tile = offset / 32u;
        let within = offset % 32u;
        let x = (tile % tilesPerRow) * 8u + within % 8u;
        let y = (tile / tilesPerRow) * 4u + within / 8u;
        packed |= texel_byte(x, y) << (8u * i);
    }
    out_words[word] = packed;
}
)";

enum class SlotState : uint8_t {
  Available,
  Requested,
  Submitted,
  Mapping,
};

struct Slot {
  wgpu::Buffer storageBuffer;
  wgpu::Buffer readbackBuffer;
  wgpu::Buffer paramsBuffer;
  SlotState state = SlotState::Available;
  uint64_t generation = 0;
  uint64_t serial = 0;
  uint64_t requestTick = 0;
  std::weak_ptr<TextureRef> source;
};

struct Entry {
  uint16_t width = 0;
  uint16_t height = 0;
  GXTexFmt fmt = GX_TF_I8;
  uint32_t byteSize = 0;
  uint64_t epoch = 0;
  uint64_t requestTick = 0;
  uint64_t nextSerial = 1;
  std::array<Slot, SlotCount> slots;
  std::vector<uint8_t> latest;
  uint64_t latestSerial = 0;
  uint64_t deliveredSerial = 0;
  std::weak_ptr<TextureRef> latestSource;
};

struct SlotRef {
  const void* src = nullptr;
  uint64_t epoch = 0;
  size_t slotIdx = 0;
  uint64_t generation = 0;
};

struct PendingRequest {
  SlotRef ref;
  TextureHandle source;
  Params params;
};

struct TaskPayload {
  uint64_t requestId = 0;
};

bool g_enabled = false;
EncoderTaskId g_taskType = InvalidEncoderTask;
wgpu::BindGroupLayout g_bindGroupLayout;
wgpu::ComputePipeline g_pipeline;
std::mutex g_mutex;
absl::flat_hash_map<const void*, Entry> g_entries;
absl::flat_hash_map<uint64_t, PendingRequest> g_pending;
absl::flat_hash_map<uint64_t, SlotRef> g_submitted;
uint64_t g_nextRequestId = 1;
uint64_t g_nextEpoch = 1;

wgpu::BindGroupLayout create_bind_group_layout() {
  constexpr std::array entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Compute,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Storage,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Uniform,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor descriptor{
      .label = "Copy Readback Bind Group Layout",
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return g_device.CreateBindGroupLayout(&descriptor);
}

wgpu::ComputePipeline create_pipeline() {
  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{
      .code = ShaderSource.data(),
  }};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = "Copy Readback Shader",
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);
  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::ComputePipelineDescriptor pipelineDescriptor{
      .label = "Copy Readback Pipeline",
      .layout = pipelineLayout,
      .compute =
          wgpu::ComputeState{
              .module = module,
              .entryPoint = "cs_main",
          },
  };
  return g_device.CreateComputePipeline(&pipelineDescriptor);
}

bool ensure_slot_buffers(Slot& slot, uint32_t byteSize) {
  if (slot.storageBuffer && slot.storageBuffer.GetSize() == byteSize) {
    return true;
  }
  const wgpu::BufferDescriptor storageDescriptor{
      .label = "Copy Readback Storage Buffer",
      .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc,
      .size = byteSize,
  };
  slot.storageBuffer = g_device.CreateBuffer(&storageDescriptor);
  const wgpu::BufferDescriptor readbackDescriptor{
      .label = "Copy Readback Map Buffer",
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
      .size = byteSize,
  };
  slot.readbackBuffer = g_device.CreateBuffer(&readbackDescriptor);
  const wgpu::BufferDescriptor paramsDescriptor{
      .label = "Copy Readback Params Buffer",
      .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
      .size = sizeof(Params),
  };
  slot.paramsBuffer = g_device.CreateBuffer(&paramsDescriptor);
  return slot.storageBuffer && slot.readbackBuffer && slot.paramsBuffer;
}

Entry& entry_for(const void* src, uint16_t width, uint16_t height, GXTexFmt fmt) {
  auto& entry = g_entries[src];
  if (entry.epoch == 0 || entry.width != width || entry.height != height || entry.fmt != fmt) {
    entry = Entry{
        .width = width,
        .height = height,
        .fmt = fmt,
        .byteSize = byte_size_8bit(width, height),
        .epoch = g_nextEpoch++,
    };
  }
  return entry;
}

Slot* find_slot(Entry& entry, size_t& slotIdx) {
  for (size_t i = 0; i < entry.slots.size(); ++i) {
    auto& slot = entry.slots[i];
    if (slot.state != SlotState::Available && entry.requestTick - slot.requestTick > StaleSlotRequests) {
      Log.warn("Recycling a copy readback slot that never completed");
      slot.state = SlotState::Available;
      ++slot.generation;
    }
    if (slot.state == SlotState::Available && ensure_slot_buffers(slot, entry.byteSize)) {
      slotIdx = i;
      return &slot;
    }
  }
  return nullptr;
}

Slot* resolve_slot(const SlotRef& ref) {
  const auto it = g_entries.find(ref.src);
  if (it == g_entries.end() || it->second.epoch != ref.epoch) {
    return nullptr;
  }
  auto& slot = it->second.slots[ref.slotIdx];
  return slot.generation == ref.generation ? &slot : nullptr;
}

void complete_map(const SlotRef& ref, const wgpu::Buffer& buffer, wgpu::MapAsyncStatus status,
                  wgpu::StringView message) {
  std::lock_guard lock{g_mutex};
  auto* slot = resolve_slot(ref);
  if (status == wgpu::MapAsyncStatus::Success) {
    if (slot != nullptr) {
      auto& entry = g_entries.find(ref.src)->second;
      if (slot->serial > entry.latestSerial) {
        const auto* mapped = static_cast<const uint8_t*>(buffer.GetConstMappedRange(0, entry.byteSize));
        if (mapped != nullptr) {
          entry.latest.assign(mapped, mapped + entry.byteSize);
          entry.latestSerial = slot->serial;
          entry.latestSource = slot->source;
        }
      }
    }
    buffer.Unmap();
  } else if (status != wgpu::MapAsyncStatus::CallbackCancelled && status != wgpu::MapAsyncStatus::Aborted) {
    Log.warn("Copy readback mapping failed {}: {}", magic_enum::enum_name(status), message);
  }
  if (slot != nullptr) {
    slot->state = SlotState::Available;
    slot->source.reset();
  }
}

void encode_task(const EncoderTaskContext& ctx, const wgpu::CommandEncoder& cmd, const void* payload,
                 size_t payloadSize, void*) {
  ZoneScoped;
  TaskPayload task;
  std::memcpy(&task, payload, std::min(payloadSize, sizeof(task)));

  PendingRequest request;
  wgpu::Buffer storageBuffer;
  wgpu::Buffer readbackBuffer;
  wgpu::Buffer paramsBuffer;
  uint32_t byteSize = 0;
  {
    std::lock_guard lock{g_mutex};
    const auto it = g_pending.find(task.requestId);
    if (it == g_pending.end()) {
      return;
    }
    request = std::move(it->second);
    g_pending.erase(it);
    auto* slot = resolve_slot(request.ref);
    if (slot == nullptr || slot->state != SlotState::Requested) {
      return;
    }
    slot->state = SlotState::Submitted;
    storageBuffer = slot->storageBuffer;
    readbackBuffer = slot->readbackBuffer;
    paramsBuffer = slot->paramsBuffer;
    byteSize = static_cast<uint32_t>(storageBuffer.GetSize());
    g_submitted.emplace(task.requestId, request.ref);
  }

  ctx.queue.WriteBuffer(paramsBuffer, 0, &request.params, sizeof(Params));

  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = request.source->sampleTextureView,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .buffer = storageBuffer,
          .size = byteSize,
      },
      wgpu::BindGroupEntry{
          .binding = 2,
          .buffer = paramsBuffer,
          .size = sizeof(Params),
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .label = "Copy Readback Bind Group",
      .layout = g_bindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);

  const wgpu::ComputePassDescriptor passDescriptor{
      .label = "Copy Readback Pass",
  };
  const auto pass = cmd.BeginComputePass(&passDescriptor);
  pass.SetPipeline(g_pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.DispatchWorkgroups((request.params.wordCount + WorkgroupSize - 1) / WorkgroupSize);
  pass.End();

  cmd.CopyBufferToBuffer(storageBuffer, 0, readbackBuffer, 0, byteSize);
}

void after_submit_task(const EncoderTaskCompletionContext&, const void* payload, size_t payloadSize, void*) {
  TaskPayload task;
  std::memcpy(&task, payload, std::min(payloadSize, sizeof(task)));

  SlotRef ref;
  wgpu::Buffer readbackBuffer;
  uint64_t byteSize = 0;
  {
    std::lock_guard lock{g_mutex};
    const auto it = g_submitted.find(task.requestId);
    if (it == g_submitted.end()) {
      return;
    }
    ref = it->second;
    g_submitted.erase(it);
    auto* slot = resolve_slot(ref);
    if (slot == nullptr || slot->state != SlotState::Submitted) {
      return;
    }
    slot->state = SlotState::Mapping;
    readbackBuffer = slot->readbackBuffer;
    byteSize = readbackBuffer.GetSize();
  }

  readbackBuffer.MapAsync(wgpu::MapMode::Read, 0, byteSize, wgpu::CallbackMode::AllowSpontaneous,
                          [ref, readbackBuffer](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                            complete_map(ref, readbackBuffer, status, message);
                          });
}
} // namespace

void initialize() {
  if (!webgpu::g_hasCoreFeatures) {
    return;
  }
  g_bindGroupLayout = create_bind_group_layout();
  g_pipeline = create_pipeline();
  g_enabled = true;
}

void shutdown() {
  if (g_taskType != InvalidEncoderTask) {
    unregister_encoder_task_type(g_taskType);
    g_taskType = InvalidEncoderTask;
  }
  {
    std::lock_guard lock{g_mutex};
    g_entries.clear();
    g_pending.clear();
    g_submitted.clear();
  }
  g_pipeline = {};
  g_bindGroupLayout = {};
  g_enabled = false;
}

bool is_supported_format(GXTexFmt fmt) noexcept {
  switch (fmt) {
  case GX_TF_I8:
  case GX_CTF_A8:
  case GX_CTF_R8:
  case GX_CTF_G8:
  case GX_CTF_B8:
    return true;
  default:
    return false;
  }
}

bool request(const void* src, void* dst, uint16_t width, uint16_t height, GXTexFmt fmt) {
  ZoneScoped;
  if (!g_enabled || src == nullptr || dst == nullptr || width == 0 || height == 0) {
    return false;
  }
  if (!is_supported_format(fmt)) {
    Log.warn("GXAuroraReadback: unsupported format {:#x}", static_cast<uint32_t>(fmt));
    return false;
  }
  if (g_taskType == InvalidEncoderTask) {
    g_taskType = register_encoder_task_type(EncoderTaskDescriptor{
        .label = "Copy Readback",
        .callback = encode_task,
        .afterSubmit = after_submit_task,
    });
  }

  gx::fifo::drain();
  TextureHandle source;
  if (const auto it = gx::g_gxState.copyTextures.find(src); it != gx::g_gxState.copyTextures.end()) {
    source = it->second.handle;
  }

  bool delivered = false;
  uint64_t requestId = 0;
  {
    std::lock_guard lock{g_mutex};
    auto& entry = entry_for(src, width, height, fmt);
    ++entry.requestTick;

    if (!entry.latest.empty() && entry.latestSerial > entry.deliveredSerial && source &&
        entry.latestSource.lock() == source) {
      std::memcpy(dst, entry.latest.data(), entry.latest.size());
      entry.deliveredSerial = entry.latestSerial;
      delivered = true;
    }

    size_t slotIdx = 0;
    if (source) {
      if (auto* slot = find_slot(entry, slotIdx)) {
        slot->state = SlotState::Requested;
        slot->serial = entry.nextSerial++;
        slot->requestTick = entry.requestTick;
        slot->source = source;
        requestId = g_nextRequestId++;
        g_pending.emplace(requestId,
                          PendingRequest{
                              .ref = SlotRef{.src = src,
                                             .epoch = entry.epoch,
                                             .slotIdx = slotIdx,
                                             .generation = slot->generation},
                              .source = source,
                              .params =
                                  Params{
                                      .logicalWidth = width,
                                      .logicalHeight = height,
                                      .srcWidth = source->size.width,
                                      .srcHeight = source->size.height,
                                      .paddedWidth = padded_width_8bit(width),
                                      .wordCount = entry.byteSize / 4,
                                  },
                          });
      }
    }
  }

  if (requestId != 0) {
    const TaskPayload payload{.requestId = requestId};
    if (!push_encoder_task(g_taskType, &payload, sizeof(payload))) {
      std::lock_guard lock{g_mutex};
      if (const auto it = g_pending.find(requestId); it != g_pending.end()) {
        if (auto* slot = resolve_slot(it->second.ref)) {
          slot->state = SlotState::Available;
          slot->source.reset();
        }
        g_pending.erase(it);
      }
    }
  }
  return delivered;
}

} // namespace aurora::gfx::copy_readback

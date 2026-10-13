#include "gpu.hpp"

#include "../frame_packet.hpp"
#include "../../webgpu/gpu.hpp"
#include "../../webgpu/gpu_prof.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

#include <magic_enum.hpp>
#include <tracy/Tracy.hpp>

namespace aurora::gfx::readback::gpu {
namespace {
constexpr Module Log{"aurora::gfx::readback"};
constexpr uint64_t PoolLimit = 16ull << 20;

std::mutex g_poolMutex;
std::vector<std::pair<uint64_t, wgpu::Buffer>> g_freeBuffers;
uint64_t g_freeBytes = 0;

// Render worker only
wgpu::Buffer g_scratch;
uint64_t g_scratchBytes = 0;

wgpu::Buffer acquire_buffer(uint64_t bytes) {
  {
    std::lock_guard lock{g_poolMutex};
    const auto it = std::ranges::find(g_freeBuffers, bytes, &std::pair<uint64_t, wgpu::Buffer>::first);
    if (it != g_freeBuffers.end()) {
      auto buffer = std::move(it->second);
      g_freeBytes -= bytes;
      g_freeBuffers.erase(it);
      return buffer;
    }
  }
  const wgpu::BufferDescriptor descriptor{
      .label = "Readback Staging Buffer",
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
      .size = bytes,
  };
  return webgpu::g_device.CreateBuffer(&descriptor);
}

const wgpu::Buffer& ensure_scratch(uint64_t bytes) {
  if (g_scratchBytes < bytes) {
    g_scratchBytes = staging_bucket(bytes);
    const wgpu::BufferDescriptor descriptor{
        .label = "Readback Pack Buffer",
        .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc,
        .size = g_scratchBytes,
    };
    g_scratch = webgpu::g_device.CreateBuffer(&descriptor);
  }
  return g_scratch;
}

void on_mapped(Job& job, wgpu::Buffer buffer, wgpu::MapAsyncStatus status, wgpu::StringView message) {
  if (status != wgpu::MapAsyncStatus::Success) {
    fail_job(job, fmt::format("buffer mapping {}: {}", magic_enum::enum_name(status), message));
    return;
  }
  const auto* data = static_cast<const uint8_t*>(buffer.GetConstMappedRange(0, job.outputBytes));
  if (data != nullptr) {
    complete_job(job, {data, job.outputBytes});
  } else {
    fail_job(job, "mapped range unavailable");
  }
  buffer.Unmap();
  release_buffer(std::move(buffer), job.bufferBytes);
}

void map_job(const std::shared_ptr<Job>& job) {
  auto buffer = std::move(job->buffer);
  buffer.MapAsync(wgpu::MapMode::Read, 0, job->outputBytes, wgpu::CallbackMode::AllowSpontaneous,
                  [job, buffer](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                    on_mapped(*job, buffer, status, message);
                  });
}
} // namespace

void initialize() {
  if (!webgpu::g_hasCoreFeatures) {
    Log.info("Asynchronous readback is unavailable without core WebGPU features");
    return;
  }
  if (webgpu::g_graphicsConfig.msaaSamples > 1) {
    Log.info("Asynchronous readback is unavailable with MSAA");
    return;
  }
  initialize_efb_gather();
  initialize_copy_pack();
  set_supported(true);
}

void shutdown() {
  if (supported()) {
    set_supported(false);
    shutdown_efb_gather();
    shutdown_copy_pack();
  }
  g_scratch = {};
  g_scratchBytes = 0;
  std::lock_guard lock{g_poolMutex};
  g_freeBuffers.clear();
  g_freeBytes = 0;
}

void release_buffer(wgpu::Buffer buffer, uint64_t bytes) noexcept {
  if (!buffer || !supported()) {
    return;
  }
  std::lock_guard lock{g_poolMutex};
  if (g_freeBytes + bytes > PoolLimit) {
    return;
  }
  g_freeBytes += bytes;
  g_freeBuffers.emplace_back(bytes, std::move(buffer));
}

void encode_job(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const std::shared_ptr<Job>& job) {
  ZoneScoped;
  if (!supported()) {
    fail_job(*job, "readback GPU resources are not initialized");
    return;
  }
  const auto& scratch = ensure_scratch(job->outputBytes);
  job->bufferBytes = staging_bucket(job->outputBytes);
  job->buffer = acquire_buffer(job->bufferBytes);
  switch (job->kind) {
  case JobKind::Efb:
    encode_efb_gather(cmd, *job, scratch);
    break;
  case JobKind::Copy:
    encode_copy_pack(cmd, *job, scratch);
    break;
  }
  {
    const webgpu::gpu_prof::Zone zone{cmd, "Readback transfer"};
    cmd.CopyBufferToBuffer(scratch, 0, job->buffer, 0, job->outputBytes);
  }
  frame.afterSubmitCallbacks.emplace_back([job] { map_job(job); });
}

} // namespace aurora::gfx::readback::gpu

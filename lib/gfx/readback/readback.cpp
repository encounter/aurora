#include "readback.hpp"

#include "gpu.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

#include <tracy/Tracy.hpp>

namespace aurora::gfx::readback {
namespace {
constexpr Module Log{"aurora::gfx::readback"};
struct Stream {
  uint32_t generation = 1;
  bool live = false;
  uint64_t nextSequence = 1;
  std::shared_ptr<const Packet> latest;
};

std::mutex g_mutex;
std::vector<Stream> g_streams;
std::vector<uint32_t> g_freeStreams;
std::atomic_bool g_supported{false};

constexpr uint32_t handle_index(StreamHandle handle) noexcept {
  return static_cast<uint32_t>(handle & 0xFFFFFFFFu) - 1;
}
constexpr uint32_t handle_generation(StreamHandle handle) noexcept { return static_cast<uint32_t>(handle >> 32); }
constexpr StreamHandle make_handle(uint32_t index, uint32_t generation) noexcept {
  return static_cast<StreamHandle>(generation) << 32 | (index + 1);
}

Stream* find_stream_locked(StreamHandle handle) noexcept {
  if (handle == 0 || (handle & 0xFFFFFFFFu) == 0) {
    return nullptr;
  }
  const uint32_t index = handle_index(handle);
  if (index >= g_streams.size()) {
    return nullptr;
  }
  auto& stream = g_streams[index];
  if (!stream.live || stream.generation != handle_generation(handle)) {
    return nullptr;
  }
  return &stream;
}

void clear() {
  std::vector<Stream> streams;
  std::lock_guard lock{g_mutex};
  streams = std::exchange(g_streams, {});
  g_freeStreams.clear();
}
} // namespace

const Output* Packet::find(uint64_t key) const noexcept {
  const auto it = std::ranges::lower_bound(outputs, key, {}, &Output::key);
  return it != outputs.end() && it->key == key ? &*it : nullptr;
}

Job::~Job() {
  if (buffer) {
    gpu::release_buffer(std::move(buffer), bufferBytes);
  }
}

void initialize() {
  clear();
  gpu::initialize();
}

void shutdown() {
  gpu::shutdown();
  clear();
}

bool supported() noexcept { return g_supported.load(std::memory_order_acquire); }

void set_supported(bool supported) noexcept { g_supported.store(supported, std::memory_order_release); }

StreamHandle create_stream() {
  std::lock_guard lock{g_mutex};
  uint32_t index;
  if (!g_freeStreams.empty()) {
    index = g_freeStreams.back();
    g_freeStreams.pop_back();
  } else {
    index = static_cast<uint32_t>(g_streams.size());
    g_streams.emplace_back();
  }
  auto& stream = g_streams[index];
  const uint32_t generation = stream.generation;
  stream = Stream{
      .generation = generation,
      .live = true,
  };
  return make_handle(index, generation);
}

void destroy_stream(StreamHandle handle) {
  std::shared_ptr<const Packet> released;
  std::lock_guard lock{g_mutex};
  auto* stream = find_stream_locked(handle);
  if (stream == nullptr) {
    return;
  }
  released = std::move(stream->latest);
  stream->live = false;
  ++stream->generation;
  if (stream->generation == 0) {
    stream->generation = 1;
  }
  g_freeStreams.push_back(handle_index(handle));
}

bool valid_stream(StreamHandle handle) noexcept {
  std::lock_guard lock{g_mutex};
  return find_stream_locked(handle) != nullptr;
}

uint64_t next_sequence(StreamHandle handle) noexcept {
  std::lock_guard lock{g_mutex};
  auto* stream = find_stream_locked(handle);
  return stream != nullptr ? stream->nextSequence++ : 0;
}

std::shared_ptr<const Packet> latest(StreamHandle handle) {
  std::lock_guard lock{g_mutex};
  const auto* stream = find_stream_locked(handle);
  return stream != nullptr ? stream->latest : nullptr;
}

std::shared_ptr<Job> create_job(StreamHandle stream, uint64_t sequence, JobKind kind, std::vector<Output> outputs,
                                uint32_t payloadBytes) {
  std::ranges::sort(outputs, {}, &Output::key);
  auto packet = std::make_shared<Packet>();
  packet->stream = stream;
  packet->sequence = sequence;
  packet->recordedAt = Clock::now();
  packet->outputs = std::move(outputs);
  packet->payload.resize(payloadBytes);

  auto job = std::make_shared<Job>();
  job->packet = std::move(packet);
  job->kind = kind;
  job->outputBytes = payloadBytes;
  return job;
}

void complete_job(Job& job, std::span<const uint8_t> data) noexcept {
  auto& packet = *job.packet;
  if (data.size() != packet.payload.size()) {
    fail_job(job, "mapped size does not match the output");
    return;
  }
  std::memcpy(packet.payload.data(), data.data(), data.size());

  std::shared_ptr<const Packet> released;
  std::lock_guard lock{g_mutex};
  auto* stream = find_stream_locked(packet.stream);
  if (stream == nullptr || (stream->latest && stream->latest->sequence >= packet.sequence)) {
    return; // destroyed, or a newer capture completed first
  }
  released = std::exchange(stream->latest, job.packet);
  const double latencyMs = std::chrono::duration<double, std::milli>(Clock::now() - packet.recordedAt).count();
  TracyPlot("aurora: readbackLatencyMs", latencyMs);
}

void fail_job(Job& job, std::string_view reason) noexcept { Log.warn("capture failed: {}", reason); }

} // namespace aurora::gfx::readback

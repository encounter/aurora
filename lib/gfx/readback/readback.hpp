#pragma once

#include "../types.hpp"

#include <dolphin/gx/GXAurora.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

// Asynchronous GPU readback service
namespace aurora::gfx::readback {

using StreamHandle = AuroraReadbackStream;
using Clock = std::chrono::steady_clock;

enum class JobKind : uint8_t {
  Efb,
  Copy,
};

struct Output {
  uint64_t key = 0;
  uint32_t payloadOffset = 0;
  uint32_t payloadSize = 0;
};

// Immutable packet with CPU payload and key-sorted output table.
struct Packet {
  StreamHandle stream = 0;
  uint64_t sequence = 0;
  Clock::time_point recordedAt;
  std::vector<Output> outputs;
  std::vector<uint8_t> payload;

  const Output* find(uint64_t key) const noexcept;
  std::span<const uint8_t> payload_of(const Output& output) const noexcept {
    return {payload.data() + output.payloadOffset, output.payloadSize};
  }
};

struct alignas(16) EfbGatherJob {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
  uint32_t outWord;
  uint32_t color;
  uint32_t alphaMode;
  uint32_t pixelFormat;
};
static_assert(sizeof(EfbGatherJob) == 32);

struct alignas(16) EfbGatherParams {
  uint32_t jobCount = 0;
  uint32_t totalWords = 0;
  uint32_t _pad0 = 0;
  uint32_t _pad1 = 0;
  // Logical sample -> render target pixel: floor((2x + 1) * num / (2 * den))
  uint32_t scaleNumX = 1;
  uint32_t scaleDenX = 1;
  uint32_t scaleNumY = 1;
  uint32_t scaleDenY = 1;
  uint32_t targetWidth = 0;
  uint32_t targetHeight = 0;
  uint32_t _pad2 = 0;
  uint32_t _pad3 = 0;
};
static_assert(sizeof(EfbGatherParams) == 48);

struct alignas(16) CopyPackParams {
  uint32_t family = 0;
  uint32_t encoding = 0;
  uint32_t tileWidth = 0;
  uint32_t tileHeight = 0;
  uint32_t tileBytes = 0;
  uint32_t tilesX = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t srcWidth = 0;
  uint32_t srcHeight = 0;
  uint32_t wordCount = 0;
  uint32_t outWord = 0;
  uint32_t quantizeRgba6 = 0;
  uint32_t _pad0 = 0;
  uint32_t _pad1 = 0;
  uint32_t _pad2 = 0;
};
static_assert(sizeof(CopyPackParams) == 64);

struct CopySource {
  TextureHandle texture;
  Range params;
  uint32_t wordCount = 0;
};

struct Job {
  std::shared_ptr<Packet> packet;
  JobKind kind = JobKind::Efb;
  uint32_t outputBytes = 0;

  // EFB gather inputs
  wgpu::TextureView depthView;
  wgpu::TextureView colorView;
  Range gatherJobs;
  Range gatherParams;
  uint32_t gatherWords = 0;

  // GX copy inputs
  std::vector<CopySource> copies;

  // GPU staging
  wgpu::Buffer buffer;
  uint64_t bufferBytes = 0;

  Job() = default;
  Job(const Job&) = delete;
  Job& operator=(const Job&) = delete;
  ~Job();
};

void initialize();
void shutdown();
bool supported() noexcept;
void set_supported(bool supported) noexcept;

StreamHandle create_stream();
void destroy_stream(StreamHandle stream);
bool valid_stream(StreamHandle stream) noexcept;
uint64_t next_sequence(StreamHandle stream) noexcept; // 0 if invalid

// Newest successful packet for the stream, or null.
std::shared_ptr<const Packet> latest(StreamHandle stream);

std::shared_ptr<Job> create_job(StreamHandle stream, uint64_t sequence, JobKind kind, std::vector<Output> outputs,
                                uint32_t payloadBytes);

// Completion: copies the mapped bytes into the job's packet and publishes it if still wanted.
void complete_job(Job& job, std::span<const uint8_t> data) noexcept;
void fail_job(Job& job, std::string_view reason) noexcept;

} // namespace aurora::gfx::readback

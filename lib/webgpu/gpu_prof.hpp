#pragma once

#include <string_view>

#include <webgpu/webgpu_cpp.h>

namespace aurora::webgpu::gpu_prof {

// GPU frame profiling via WebGPU timestamp queries, surfaced in Tracy as a
// GPU timeline track (one root zone per command buffer, nested zones for
// every render/compute pass and for encoder-level work between passes) plus
// a few per-frame plots. Compiles to no-ops unless TRACY_ENABLE; disabled at
// runtime when the device lacks wgpu::FeatureName::TimestampQuery.
//
// All recording functions must be called on the thread encoding the frame,
// in encode order, against the frame's command encoder:
//   frame_begin(encoder);
//   ... pass_writes() / Zone while encoding ...
//   frame_end(encoder);   // after all passes, right before encoder.Finish()
//   after_submit();       // right after Queue::Submit
//
// Pass zones use pass timestampWrites (safe API). The frame zone and encoder
// zones use CommandEncoder::WriteTimestamp, which Dawn gates behind
// allow_unsafe_apis (enabled by gpu.cpp in Tracy builds); when unavailable,
// the frame zone falls back to the first/last pass timestamps and encoder
// zones become no-ops.

void initialize();
void shutdown();

void frame_begin(const wgpu::CommandEncoder& encoder);
void frame_end(const wgpu::CommandEncoder& encoder);
void after_submit();

// Timestamp writes for a render or compute pass zone named `name`, or
// nullptr when profiling is unavailable. Pass straight into the pass
// descriptor; the pointer is reused after a few more pass_writes calls.
const wgpu::PassTimestampWrites* pass_writes(std::string_view name);

// Zone for encoder-level work between passes (buffer/texture copies, query
// resolves, ...). May contain nested zones and passes.
class Zone {
public:
  Zone(const wgpu::CommandEncoder& encoder, std::string_view name);
  ~Zone();
  Zone(const Zone&) = delete;
  Zone& operator=(const Zone&) = delete;

private:
  // Null when the zone is inactive (profiling disabled or budget exhausted).
  const wgpu::CommandEncoder* m_encoder = nullptr;
  uint32_t m_endQuery = 0;
};

} // namespace aurora::webgpu::gpu_prof

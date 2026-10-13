#pragma once

#include "readback.hpp"

#include <algorithm>
#include <bit>

namespace aurora::gfx::detail {
struct FramePacket;
}

namespace aurora::gfx::readback::gpu {

void initialize();
void shutdown();

inline uint64_t staging_bucket(uint64_t bytes) noexcept { return std::bit_ceil(std::max<uint64_t>(bytes, 256)); }
void release_buffer(wgpu::Buffer buffer, uint64_t bytes) noexcept;

void encode_job(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const std::shared_ptr<Job>& job);

// EFB gather: writes EFB regions into `out` as tightly packed u32 words
void initialize_efb_gather();
void shutdown_efb_gather();
void encode_efb_gather(const wgpu::CommandEncoder& cmd, const Job& job, const wgpu::Buffer& out);

// Texture copies: writes each copy texture into `out` in native (tiled) GX layout
void initialize_copy_pack();
void shutdown_copy_pack();
void encode_copy_pack(const wgpu::CommandEncoder& cmd, const Job& job, const wgpu::Buffer& out);

inline void dispatch_linear(const wgpu::ComputePassEncoder& pass, uint32_t workgroups) {
  constexpr uint32_t MaxX = 65535;
  const uint32_t x = std::min(workgroups, MaxX);
  const uint32_t y = (workgroups + MaxX - 1) / MaxX;
  pass.DispatchWorkgroups(std::max(x, 1u), std::max(y, 1u));
}

} // namespace aurora::gfx::readback::gpu

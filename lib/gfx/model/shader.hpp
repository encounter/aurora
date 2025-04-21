#pragma once

#include "../common.hpp"
#include "../gx.hpp"

namespace aurora::gfx::model {
struct DrawData {
  PipelineRef pipeline;
  Range vertRange;
  Range idxRange;
  gx::BindGroupRanges dataRanges;
  Range uniformRange;
  uint32_t indexCount;
  gx::GXBindGroups bindGroups;
  u32 dstAlpha;
};

struct PipelineConfig : gx::PipelineConfig {};

struct State {};

State construct_state();
wgpu::RenderPipeline create_pipeline(const State& state, [[maybe_unused]] const PipelineConfig& config);
void render(const State& state, const DrawData& data, const wgpu::RenderPassEncoder& pass);

void queue_surface(const u8* dlStart, u32 dlSize, bool bigEndian) noexcept;
} // namespace aurora::gfx::model

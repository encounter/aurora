#pragma once

#include "common.hpp"

#include <webgpu/webgpu_cpp.h>

namespace aurora::gfx::clear {
struct DrawData {
  PipelineRef pipeline;
  wgpu::Color color;
  float depth = 0.f;
};

constexpr uint8_t ClearPipelineConfigVersion = 1;
struct PipelineConfig {
  uint8_t version = ClearPipelineConfigVersion;
  bool clearColor : 1 = true;
  bool clearAlpha : 1 = true;
  bool clearDepth : 1 = true;
  uint8_t _pad : 5 = 0;
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config);
void render(const DrawData& data, const wgpu::RenderPassEncoder& pass);
} // namespace aurora::gfx::clear
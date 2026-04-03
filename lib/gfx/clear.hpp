#pragma once

#include "common.hpp"

#include <webgpu/webgpu_cpp.h>

namespace aurora::gfx::clear {
struct DrawData {
  PipelineRef pipeline;
  wgpu::Color color;
  float depth = 0.f;
};

constexpr uint32_t ClearPipelineConfigVersion = 2;
struct PipelineConfig {
  uint32_t version = ClearPipelineConfigVersion;
  uint32_t msaaSamples = 1;
  bool clearColor : 1 = true;
  bool clearAlpha : 1 = true;
  bool clearDepth : 1 = true;
  uint32_t _pad : 29 = 0;
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config);
void render(const DrawData& data, const wgpu::RenderPassEncoder& pass, const wgpu::Extent3D& targetSize);
} // namespace aurora::gfx::clear

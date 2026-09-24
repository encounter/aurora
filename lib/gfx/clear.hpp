#pragma once

#include <aurora/gfx.hpp>

#include "types.hpp"

#include <array>

#include <webgpu/webgpu_cpp.h>

namespace aurora::gfx::clear {
struct DrawData {
  PipelineRef pipeline;
  wgpu::Color color;
  float depth = 0.f;
  ClipRect rect{};
};

constexpr uint32_t ClearPipelineConfigVersion = 5;
struct PipelineConfig {
  uint32_t version = ClearPipelineConfigVersion;
  bool clearColor = true;
  bool clearAlpha = true;
  bool clearDepth = true;
  uint8_t _pad = 0;
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config, const RenderTargetLayout& layout);
void render(const DrawData& data, const wgpu::RenderPassEncoder& pass, const wgpu::Extent3D& targetSize);
} // namespace aurora::gfx::clear

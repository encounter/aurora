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
};

constexpr uint32_t ClearPipelineConfigVersion = 4;
struct PipelineConfig {
  uint64_t targetLayoutKey = 0;
  std::array<wgpu::TextureFormat, MaxColorAttachments> colorFormats{};
  wgpu::TextureFormat depthStencilFormat = wgpu::TextureFormat::Undefined;
  uint32_t version = ClearPipelineConfigVersion;
  uint32_t colorAttachmentCount = 0;
  uint32_t msaaSamples = 1;
  bool clearColor = true;
  bool clearAlpha = true;
  bool clearDepth = true;
  std::array<uint8_t, 5> _pad{};
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

PipelineConfig make_pipeline_config(const RenderTargetLayout& layout, bool clearColor, bool clearAlpha,
                                    bool clearDepth) noexcept;
wgpu::RenderPipeline create_pipeline(const PipelineConfig& config);
void render(const DrawData& data, const wgpu::RenderPassEncoder& pass, const wgpu::Extent3D& targetSize);
} // namespace aurora::gfx::clear

#pragma once

#include "../gfx/common.hpp"
#include "gx.hpp"

namespace aurora::gx {
struct DrawData {
  gfx::PipelineRef pipeline;
  gfx::Range vertRange;
  gfx::Range idxRange;
  BindGroupRanges dataRanges;
  gfx::Range uniformRange;
  uint32_t vtxCount;
  uint32_t indexCount;
  uint32_t instanceCount;
  GXBindGroups bindGroups;
  u32 dstAlpha;
};

constexpr u32 GXPipelineConfigVersion = 10;
struct PipelineConfig {
  u32 version = GXPipelineConfigVersion;
  ShaderConfig shaderConfig;
  GXCompare depthFunc;
  GXCullMode cullMode;
  GXBlendMode blendMode;
  GXBlendFactor blendFacSrc, blendFacDst;
  GXLogicOp blendOp;
  u32 dstAlpha;
  bool depthCompare, depthUpdate, alphaUpdate, colorUpdate;
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

wgpu::RenderPipeline create_pipeline([[maybe_unused]] const PipelineConfig& config);
void render(const DrawData& data, const wgpu::RenderPassEncoder& pass);

void queue_surface(const u8* dlStart, u32 dlSize, bool bigEndian) noexcept;
} // namespace aurora::gx

#pragma once

#include "../gfx/common.hpp"
#include "WebGPURenderInterface.hpp"

#include <array>
#include <cstdint>

#include <webgpu/webgpu_cpp.h>

namespace aurora::rmlui {

constexpr uint32_t RmlPipelineConfigVersion = 2;

enum class PipelineKind : uint32_t {
  Geometry,
  Gradient,
  Blit,
  OpaqueBlit,
  SeedResample,
  SimpleFilter,
  Blur,
  RegionBlit,
  DropShadow,
  MaskImage,
};

enum class VertexLayoutKind : uint32_t {
  Geometry,
  Fullscreen,
  BlurFullscreen,
};

enum class StencilMode : uint32_t {
  None,
  AlwaysKeep,
  EqualKeep,
  ClipReplace,
  ClipIntersect,
};

enum class BlendMode : uint32_t {
  None,
  Premultiplied,
};

enum class DrawKind : uint32_t {
  Geometry,
  Fullscreen,
};

struct PipelineConfig {
  uint32_t version = RmlPipelineConfigVersion;
  uint32_t kind = static_cast<uint32_t>(PipelineKind::Geometry);
  uint32_t colorFormat = static_cast<uint32_t>(wgpu::TextureFormat::Undefined);
  uint32_t sampleCount = 1;
  uint32_t vertexLayout = static_cast<uint32_t>(VertexLayoutKind::Geometry);
  uint32_t stencilFormat = static_cast<uint32_t>(wgpu::TextureFormat::Undefined);
  uint32_t stencilMode = static_cast<uint32_t>(StencilMode::None);
  uint32_t blendMode = static_cast<uint32_t>(BlendMode::Premultiplied);
  uint32_t colorWriteMask = static_cast<uint32_t>(wgpu::ColorWriteMask::All);
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

struct SeedResampleUniformBlock {
  uint32_t samplerMode = 0;
  float frameWidth = 0.f;
  float frameHeight = 0.f;
  uint32_t _pad = 0;
};

struct DrawData {
  gfx::PipelineRef pipeline = 0;
  gfx::Range vertexRange;
  gfx::Range indexRange;
  gfx::Range uniformRange;
  gfx::BindGroupRef bindGroup1 = 0;
  gfx::BindGroupRef bindGroup2 = 0;
  uint32_t bindGroup1DynamicOffset = 0;
  uint32_t bindGroup2DynamicOffset = 0;
  uint32_t dynamicBindGroupMask = 0;
  uint32_t drawKind = static_cast<uint32_t>(DrawKind::Geometry);
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  uint32_t stencilRef = 0;
  std::array<float, 4> blendConstant{};
  uint32_t hasBlendConstant = 0;
};
static_assert(std::is_trivially_copyable_v<DrawData>);

void initialize_pipeline();
void shutdown_pipeline();

gfx::BindGroupRef texture_bind_group_ref(const wgpu::TextureView& view);
gfx::BindGroupRef common_bind_group_ref();
gfx::BindGroupRef uniform_bind_group_ref();

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config);
void render(const DrawData& data, const wgpu::RenderPassEncoder& pass);

uint32_t sampler_mode() noexcept;

} // namespace aurora::rmlui

#include "shader.hpp"

#include "../../webgpu/gpu.hpp"
#include "../gx_fmt.hpp"
#include "../display_list.hpp"
#include "../shader_info.hpp"

#include <absl/container/flat_hash_map.h>

namespace aurora::gfx::model {
static Module Log("aurora::gfx::model");

void queue_surface(const u8* dlStart, u32 dlSize) noexcept {
  const auto result = aurora::gfx::gx::process_display_list(dlStart, dlSize);

  gx::BindGroupRanges ranges{};
  for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
    if (gx::g_gxState.vtxDesc[i] != GX_INDEX8 && gx::g_gxState.vtxDesc[i] != GX_INDEX16) {
      continue;
    }
    auto& array = gx::g_gxState.arrays[i];
    if (array.cachedRange.size > 0) {
      // Use the currently cached range
      ranges.vaRanges[i] = array.cachedRange;
    } else {
      // Push array data to storage and cache range
      const auto range = push_storage(static_cast<const uint8_t*>(array.data), array.size);
      ranges.vaRanges[i] = range;
      array.cachedRange = range;
    }
  }

  model::PipelineConfig config{};
  populate_pipeline_config(config, GX_TRIANGLES, result.fmt);
  const auto info = gx::build_shader_info(config.shaderConfig);
  const auto bindGroups = gx::build_bind_groups(info, config.shaderConfig, ranges);
  const auto pipeline = pipeline_ref(config);

  push_draw_command(model::DrawData{
      .pipeline = pipeline,
      .vertRange = result.vertRange,
      .idxRange = result.idxRange,
      .dataRanges = ranges,
      .uniformRange = build_uniform(info),
      .indexCount = result.numIndices,
      .bindGroups = bindGroups,
      .dstAlpha = gx::g_gxState.dstAlpha,
  });
}

State construct_state() { return {}; }

wgpu::RenderPipeline create_pipeline(const State& state, const PipelineConfig& config) {
  const auto info = build_shader_info(config.shaderConfig); // TODO remove
  const auto shader = build_shader(config.shaderConfig, info);

  std::array<wgpu::VertexAttribute, gx::MaxVtxAttr> vtxAttrs{};
  auto [num4xAttr, rem] = std::div(config.shaderConfig.indexedAttributeCount, 4);
  u32 num2xAttr = 0;
  if (rem > 2) {
    ++num4xAttr;
  } else if (rem > 0) {
    ++num2xAttr;
  }

  u32 offset = 0;
  u32 shaderLocation = 0;

  // Indexed attributes
  for (u32 i = 0; i < num4xAttr; ++i) {
    vtxAttrs[shaderLocation] = {
        .format = wgpu::VertexFormat::Uint16x4,
        .offset = offset,
        .shaderLocation = shaderLocation,
    };
    offset += 8;
    ++shaderLocation;
  }
  for (u32 i = 0; i < num2xAttr; ++i) {
    vtxAttrs[shaderLocation] = {
        .format = wgpu::VertexFormat::Uint16x2,
        .offset = offset,
        .shaderLocation = shaderLocation,
    };
    offset += 4;
    ++shaderLocation;
  }

  // Direct attributes
  for (int i = 0; i < gx::MaxVtxAttr; ++i) {
    const auto attrType = config.shaderConfig.vtxAttrs[i];
    if (attrType != GX_DIRECT) {
      continue;
    }
    const auto attr = static_cast<GXAttr>(i);
    switch (attr) {
      DEFAULT_FATAL("unhandled direct attr {}", i);
    case GX_VA_POS:
    case GX_VA_NRM:
      vtxAttrs[shaderLocation] = wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x3,
          .offset = offset,
          .shaderLocation = shaderLocation,
      };
      offset += 12;
      break;
    case GX_VA_CLR0:
    case GX_VA_CLR1:
      vtxAttrs[shaderLocation] = wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x4,
          .offset = offset,
          .shaderLocation = shaderLocation,
      };
      offset += 16;
      break;
    case GX_VA_TEX0:
    case GX_VA_TEX1:
    case GX_VA_TEX2:
    case GX_VA_TEX3:
    case GX_VA_TEX4:
    case GX_VA_TEX5:
    case GX_VA_TEX6:
    case GX_VA_TEX7:
      vtxAttrs[shaderLocation] = wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x2,
          .offset = offset,
          .shaderLocation = shaderLocation,
      };
      offset += 8;
      break;
    }
    ++shaderLocation;
  }

  const std::array vtxBuffers{wgpu::VertexBufferLayout{
      .stepMode = wgpu::VertexStepMode::Vertex,
      .arrayStride = offset,
      .attributeCount = shaderLocation,
      .attributes = vtxAttrs.data(),
  }};

  return build_pipeline(config, info, vtxBuffers, shader, "GX Pipeline");
}

void render(const State& state, const DrawData& data, const wgpu::RenderPassEncoder& pass) {
  if (!bind_pipeline(data.pipeline, pass)) {
    return;
  }

  std::array<uint32_t, GX_VA_MAX_ATTR + 1> offsets{data.uniformRange.offset};
  uint32_t bindIdx = 1;
  for (uint32_t i = 0; i < GX_VA_MAX_ATTR; ++i) {
    const auto& range = data.dataRanges.vaRanges[i];
    if (range.size <= 0) {
      continue;
    }
    offsets[bindIdx] = range.offset;
    ++bindIdx;
  }
  pass.SetBindGroup(0, find_bind_group(data.bindGroups.uniformBindGroup), bindIdx, offsets.data());
  if (data.bindGroups.samplerBindGroup && data.bindGroups.textureBindGroup) {
    pass.SetBindGroup(1, find_bind_group(data.bindGroups.samplerBindGroup));
    pass.SetBindGroup(2, find_bind_group(data.bindGroups.textureBindGroup));
  }
  pass.SetVertexBuffer(0, g_vertexBuffer, data.vertRange.offset, data.vertRange.size);
  pass.SetIndexBuffer(g_indexBuffer, wgpu::IndexFormat::Uint16, data.idxRange.offset, data.idxRange.size);
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  pass.DrawIndexed(data.indexCount);
}
} // namespace aurora::gfx::model

static absl::flat_hash_map<aurora::HashType, aurora::gfx::Range> sCachedRanges;

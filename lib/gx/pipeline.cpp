#include "pipeline.hpp"

#include "../webgpu/gpu.hpp"
#include "gx_fmt.hpp"
#include "command_processor.hpp"
#include "shader_info.hpp"

#include <absl/container/flat_hash_map.h>

namespace aurora::gx {
static Module Log("aurora::gx");

State construct_state() { return {}; }

wgpu::RenderPipeline create_pipeline(const State& state, const PipelineConfig& config) {
  const auto info = build_shader_info(config.shaderConfig); // TODO remove
  const auto shader = build_shader(config.shaderConfig, info);
  return build_pipeline(config, info, {}, shader, "GX Pipeline");
}

void render(const State& state, const DrawData& data, const wgpu::RenderPassEncoder& pass) {
  if (!gfx::bind_pipeline(data.pipeline, pass)) {
    return;
  }

  std::array<uint32_t, MaxIndexAttr + 2> offsets{data.uniformRange.offset};
  uint32_t bindIdx = 1;
  for (uint32_t i = 0; i < MaxIndexAttr; ++i) {
    const auto& range = data.dataRanges.vaRanges[i];
    if (range.size <= 0) {
      continue;
    }
    offsets[bindIdx] = range.offset;
    ++bindIdx;
  }
  pass.SetBindGroup(0, gfx::find_bind_group(data.bindGroups.uniformBindGroup), bindIdx, offsets.data());
  if (data.bindGroups.samplerBindGroup && data.bindGroups.textureBindGroup) {
    pass.SetBindGroup(1, gfx::find_bind_group(data.bindGroups.samplerBindGroup));
    pass.SetBindGroup(2, gfx::find_bind_group(data.bindGroups.textureBindGroup));
  }
  pass.SetIndexBuffer(gfx::g_indexBuffer, wgpu::IndexFormat::Uint16, data.idxRange.offset, data.idxRange.size);
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  pass.DrawIndexed(data.indexCount, data.instanceCount);
}
} // namespace aurora::gx

#include "pipeline.hpp"

#include "../webgpu/gpu.hpp"
#include "gx_fmt.hpp"
#include "shader_info.hpp"
#include "tracy/Tracy.hpp"

namespace aurora::gx {
static Module Log("aurora::gx");

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  ZoneScoped;
  const auto shader = build_shader(config.shaderConfig);
  return build_pipeline(config, {}, shader, "GX Pipeline");
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass) {
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

#include "shader.hpp"

#include "../../webgpu/gpu.hpp"

namespace aurora::gfx::stream {
static Module Log("aurora::gfx::stream");

using webgpu::g_device;

WGPURenderPipeline create_pipeline(const State& state, [[maybe_unused]] const PipelineConfig& config) {
  const auto info = build_shader_info(config.shaderConfig); // TODO remove
  const auto shader = build_shader(config.shaderConfig, info);

  std::array<WGPUVertexAttribute, 4> attributes{};
  attributes[0] = WGPUVertexAttribute{
      .format = WGPUVertexFormat_Float32x3,
      .offset = 0,
      .shaderLocation = 0,
  };
  uint64_t offset = 12;
  uint32_t shaderLocation = 1;
  if (config.shaderConfig.vtxAttrs[GX_VA_NRM] == GX_DIRECT) {
    attributes[shaderLocation] = WGPUVertexAttribute{
        .format = WGPUVertexFormat_Float32x3,
        .offset = offset,
        .shaderLocation = shaderLocation,
    };
    offset += 12;
    shaderLocation++;
  }
  if (config.shaderConfig.vtxAttrs[GX_VA_CLR0] == GX_DIRECT) {
    attributes[shaderLocation] = WGPUVertexAttribute{
        .format = WGPUVertexFormat_Float32x4,
        .offset = offset,
        .shaderLocation = shaderLocation,
    };
    offset += 16;
    shaderLocation++;
  }
  for (int i = GX_VA_TEX0; i < GX_VA_TEX7; ++i) {
    if (config.shaderConfig.vtxAttrs[i] != GX_DIRECT) {
      continue;
    }
    attributes[shaderLocation] = WGPUVertexAttribute{
        .format = WGPUVertexFormat_Float32x2,
        .offset = offset,
        .shaderLocation = shaderLocation,
    };
    offset += 8;
    shaderLocation++;
  }
  const std::array vertexBuffers{WGPUVertexBufferLayout{
      .arrayStride = offset,
      .attributeCount = shaderLocation,
      .attributes = attributes.data(),
  }};

  return build_pipeline(config, info, vertexBuffers, shader, "Stream Pipeline");
}

State construct_state() { return {}; }

void render(const State& state, const DrawData& data, const WGPURenderPassEncoder& pass) {
  if (!bind_pipeline(data.pipeline, pass)) {
    return;
  }

  const std::array offsets{data.uniformRange.offset};
  wgpuRenderPassEncoderSetBindGroup(pass, 0, find_bind_group(data.bindGroups.uniformBindGroup), offsets.size(),
                                    offsets.data());
  if (data.bindGroups.samplerBindGroup && data.bindGroups.textureBindGroup) {
    wgpuRenderPassEncoderSetBindGroup(pass, 1, find_bind_group(data.bindGroups.samplerBindGroup), 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, find_bind_group(data.bindGroups.textureBindGroup), 0, nullptr);
  }
  wgpuRenderPassEncoderSetVertexBuffer(pass, 0, g_vertexBuffer, data.vertRange.offset, data.vertRange.size);
  wgpuRenderPassEncoderSetIndexBuffer(pass, g_indexBuffer, WGPUIndexFormat_Uint16, data.indexRange.offset,
                                      data.indexRange.size);
  if (data.dstAlpha != UINT32_MAX) {
    const WGPUColor color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    wgpuRenderPassEncoderSetBlendConstant(pass, &color);
  }
  wgpuRenderPassEncoderDrawIndexed(pass, data.indexCount, 1, 0, 0, 0);
}
} // namespace aurora::gfx::stream

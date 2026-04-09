#include "clear.hpp"

#include "../webgpu/gpu.hpp"
#include "tracy/Tracy.hpp"

namespace {
wgpu::ColorWriteMask clear_write_mask(bool clearColor, bool clearAlpha) {
  auto writeMask = wgpu::ColorWriteMask::None;
  if (clearColor) {
    writeMask |= wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green | wgpu::ColorWriteMask::Blue;
  }
  if (clearAlpha) {
    writeMask |= wgpu::ColorWriteMask::Alpha;
  }
  return writeMask;
}
} // namespace

namespace aurora::gfx::clear {

using webgpu::g_device;
using webgpu::g_graphicsConfig;

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  ZoneScoped;
  wgpu::ShaderSourceWGSL sourceDescriptor{};
  sourceDescriptor.code = R"""(
struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);

@vertex
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[vtxIdx], 0.0, 1.0);
    return out;
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0);
}
)""";
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &sourceDescriptor,
      .label = "EFB Clear Module",
  };
  auto module = g_device.CreateShaderModule(&moduleDescriptor);
  constexpr wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 0,
      .bindGroupLayouts = nullptr,
  };
  auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  constexpr wgpu::BlendState blendState{
      .color =
          wgpu::BlendComponent{
              .operation = wgpu::BlendOperation::Add,
              .srcFactor = wgpu::BlendFactor::Constant,
              .dstFactor = wgpu::BlendFactor::Zero,
          },
      .alpha =
          wgpu::BlendComponent{
              .operation = wgpu::BlendOperation::Add,
              .srcFactor = wgpu::BlendFactor::Constant,
              .dstFactor = wgpu::BlendFactor::Zero,
          },
  };
  const wgpu::ColorTargetState colorTarget{
      .format = g_graphicsConfig.surfaceConfiguration.format,
      .blend = &blendState,
      .writeMask = clear_write_mask(config.clearColor, config.clearAlpha),
  };
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = 1,
      .targets = &colorTarget,
  };
  const wgpu::DepthStencilState depthStencil{
      .format = g_graphicsConfig.depthFormat,
      .depthWriteEnabled = config.clearDepth,
      .depthCompare = wgpu::CompareFunction::Always,
  };
  const auto label = fmt::format("EFB Clear Pipeline (color {}, alpha {}, depth {})", config.clearColor,
                                 config.clearAlpha, config.clearDepth);
  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .label = label.c_str(),
      .layout = pipelineLayout,
      .vertex =
          wgpu::VertexState{
              .module = module,
              .entryPoint = "vs_main",
          },
      .primitive =
          wgpu::PrimitiveState{
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .depthStencil = &depthStencil,
      .multisample =
          wgpu::MultisampleState{
              .count = config.msaaSamples,
          },
      .fragment = &fragmentState,
  };
  return g_device.CreateRenderPipeline(&pipelineDescriptor);
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass, const wgpu::Extent3D& targetSize) {
  if (!bind_pipeline(data.pipeline, pass)) {
    return;
  }

  pass.SetBlendConstant(&data.color);
  pass.SetViewport(0.f, 0.f, static_cast<float>(targetSize.width), static_cast<float>(targetSize.height), data.depth,
                   data.depth);
  pass.SetScissorRect(0, 0, targetSize.width, targetSize.height);
  pass.Draw(3);
}
} // namespace aurora::gfx::clear

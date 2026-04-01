#include "tex_palette_conv.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "texture.hpp"

#include <vector>

namespace aurora::gfx::tex_palette_conv {
static Module Log("aurora::gfx::tex_palette_conv");

using webgpu::g_device;

static constexpr std::string_view ShaderPreambleVtx = R"(
struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

var<private> positions: array<vec2f, 3> = array(
    vec2f(-1.0, 1.0),
    vec2f(-1.0, -3.0),
    vec2f(3.0, 1.0),
);
var<private> uvs: array<vec2f, 3> = array(
    vec2f(0.0, 0.0),
    vec2f(0.0, 2.0),
    vec2f(2.0, 0.0),
);

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4f(positions[vi], 0.0, 1.0);
    out.uv = uvs[vi];
    return out;
}

fn intensity(rgb: vec3f) -> f32 {
    // ITU-R BT.601 luma coefficients
    return dot(rgb, vec3f(0.257, 0.504, 0.098)) + 16.0 / 255.0;
}
)"sv;

// Direct: R16Sint index texture + TLUT -> RGBA8
static constexpr std::string_view ShaderDirect = R"(
@group(0) @binding(0) var src_samp: sampler;
@group(0) @binding(1) var src: texture_2d<i32>;
@group(0) @binding(2) var tlut: texture_2d<f32>;

@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let texSize = vec2f(textureDimensions(src));
    let coord = vec2i(floor(in.uv * texSize));
    let idx = textureLoad(src, coord, 0).r;
    return textureLoad(tlut, vec2i(idx, 0), 0);
}
)"sv;

// FromFloat8: f32 texture (R8Unorm) -> 8-bit index -> TLUT -> RGBA8
static constexpr std::string_view ShaderFromFloat8 = R"(
@group(0) @binding(0) var src_samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var tlut: texture_2d<f32>;

@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let texSize = vec2f(textureDimensions(src));
    let coord = vec2i(floor(in.uv * texSize));
    let r = textureLoad(src, coord, 0).r;
    return textureLoad(tlut, vec2i(i32(r * 255.0), 0), 0);
}
)"sv;

// FromFloat4: f32 texture (R8Unorm) -> 4-bit index -> TLUT -> RGBA8
static constexpr std::string_view ShaderFromFloat4 = R"(
@group(0) @binding(0) var src_samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var tlut: texture_2d<f32>;

@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let texSize = vec2f(textureDimensions(src));
    let coord = vec2i(floor(in.uv * texSize));
    let r = textureLoad(src, coord, 0).r;
    return textureLoad(tlut, vec2i(i32(r * 15.0), 0), 0);
}
)"sv;

struct PipelineInfo {
  wgpu::RenderPipeline pipeline;
  wgpu::BindGroupLayout bindGroupLayout;
};

static PipelineInfo g_directPipeline;
static PipelineInfo g_fromFloat8Pipeline;
static PipelineInfo g_fromFloat4Pipeline;
static wgpu::Sampler g_sampler;

static PipelineInfo create_pipeline(std::string_view fragBindingsAndShader, wgpu::TextureSampleType srcSampleType,
                                    const char* label) {
  std::string shaderSource;
  shaderSource.reserve(ShaderPreambleVtx.size() + fragBindingsAndShader.size());
  shaderSource += ShaderPreambleVtx;
  shaderSource += fragBindingsAndShader;

  wgpu::ShaderSourceWGSL wgslSource{};
  wgslSource.code = shaderSource.c_str();
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = label,
  };
  auto module = g_device.CreateShaderModule(&moduleDescriptor);

  const std::array bindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler =
              wgpu::SamplerBindingLayout{
                  .type = wgpu::SamplerBindingType::NonFiltering,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = srcSampleType,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
  };
  const auto bindGroupLayoutLabel = fmt::format("{} Bind Group Layout", label);
  const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
      .label = bindGroupLayoutLabel.c_str(),
      .entryCount = bindGroupLayoutEntries.size(),
      .entries = bindGroupLayoutEntries.data(),
  };
  auto bindGroupLayout = g_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &bindGroupLayout,
  };
  auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);

  constexpr std::array colorTargets{wgpu::ColorTargetState{
      .format = wgpu::TextureFormat::RGBA8Unorm,
  }};
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .label = label,
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
      .fragment = &fragmentState,
  };

  return PipelineInfo{
      .pipeline = g_device.CreateRenderPipeline(&pipelineDescriptor),
      .bindGroupLayout = std::move(bindGroupLayout),
  };
}

static const PipelineInfo& pipeline_for_variant(Variant variant) {
  switch (variant) {
  case Variant::Direct:
    return g_directPipeline;
  case Variant::FromFloat8:
    return g_fromFloat8Pipeline;
  case Variant::FromFloat4:
    return g_fromFloat4Pipeline;
  }
  FATAL("invalid palette conv variant {}", static_cast<int>(variant));
}

void initialize() {
  g_directPipeline = create_pipeline(ShaderDirect, wgpu::TextureSampleType::Sint, "TexPaletteConv Direct");
  g_fromFloat8Pipeline =
      create_pipeline(ShaderFromFloat8, wgpu::TextureSampleType::UnfilterableFloat, "TexPaletteConv FromFloat8");
  g_fromFloat4Pipeline =
      create_pipeline(ShaderFromFloat4, wgpu::TextureSampleType::UnfilterableFloat, "TexPaletteConv FromFloat4");
  constexpr wgpu::SamplerDescriptor samplerDesc{
      .label = "TexPaletteConv Sampler",
      .magFilter = wgpu::FilterMode::Nearest,
      .minFilter = wgpu::FilterMode::Nearest,
  };
  g_sampler = g_device.CreateSampler(&samplerDesc);
}

void shutdown() {
  g_directPipeline = {};
  g_fromFloat8Pipeline = {};
  g_fromFloat4Pipeline = {};
  g_sampler = {};
}

void run(const wgpu::CommandEncoder& cmd, const ConvRequest& req) {
  const auto& [pipeline, bindGroupLayout] = pipeline_for_variant(req.variant);

  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .sampler = g_sampler,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .textureView = req.src->sampleTextureView,
      },
      wgpu::BindGroupEntry{
          .binding = 2,
          .textureView = req.tlut->sampleTextureView,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = bindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);

  const std::array colorAttachments{
      wgpu::RenderPassColorAttachment{
          .view = req.dst->attachmentTextureView,
          .loadOp = wgpu::LoadOp::Clear,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {0.0, 0.0, 0.0, 0.0},
      },
  };
  const wgpu::RenderPassDescriptor renderPassDescriptor{
      .label = "TexPaletteConv Pass",
      .colorAttachmentCount = colorAttachments.size(),
      .colorAttachments = colorAttachments.data(),
  };
  const auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.Draw(3);
  pass.End();
}

} // namespace aurora::gfx::tex_palette_conv

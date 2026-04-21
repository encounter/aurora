#include "tex_copy_conv.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "texture.hpp"
#include "../gx/gx_fmt.hpp"

#include <vector>

#include <absl/container/flat_hash_map.h>

#include "texture_convert.hpp"

namespace aurora::gfx::tex_copy_conv {
static Module Log("aurora::gfx::tex_copy_conv");

using webgpu::g_device;

static constexpr std::string_view ShaderPreamble = R"(
@group(0) @binding(0) var src_samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;

struct UVTransform {
    offset: vec2f,
    scale: vec2f,
};
@group(0) @binding(2) var<uniform> uv_xf: UVTransform;

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
    out.uv = uvs[vi] * uv_xf.scale + uv_xf.offset;
    return out;
}

fn intensity(rgb: vec3f) -> f32 {
    // ITU-R BT.601 luma coefficients
    return dot(rgb, vec3f(0.257, 0.504, 0.098)) + 16.0 / 255.0;
}

fn quantize4(v: f32) -> f32 {
    return floor(v * 16.0) / 15.0;
}
)"sv;

// Passthrough blit (for scaling)
static constexpr std::string_view FragPassthrough = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(src, src_samp, in.uv);
}
)"sv;

// GX_TF_I4: 4-bit intensity -> R8Unorm (quantized)
static constexpr std::string_view FragI4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let rgb = textureSample(src, src_samp, in.uv).rgb;
    let i = quantize4(intensity(rgb));
    return vec4f(i, i, i, i);
}
)"sv;

// GX_TF_I8: 8-bit intensity -> R8Unorm
static constexpr std::string_view FragI8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let rgb = textureSample(src, src_samp, in.uv).rgb;
    let i = intensity(rgb);
    return vec4f(i, i, i, i);
}
)"sv;

// GX_TF_IA4: 4-bit intensity + 4-bit alpha -> RG8Unorm
static constexpr std::string_view FragIA4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    let i = quantize4(intensity(c.rgb));
    let a = quantize4(c.a);
    return vec4f(i, i, i, a);
}
)"sv;

// GX_TF_IA8: 8-bit intensity + 8-bit alpha -> RG8Unorm
static constexpr std::string_view FragIA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    let i = intensity(c.rgb);
    return vec4f(i, i, i, c.a);
}
)"sv;

// GX_TF_RGB565: Blit alpha to 1.0
static constexpr std::string_view FragRGB565 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(c.rgb, 1.0);
}
)"sv;

// GX_CTF_R4: 4-bit red -> R8Unorm
static constexpr std::string_view FragR4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let r = quantize4(textureSample(src, src_samp, in.uv).r);
    return vec4f(r, r, r, r);
}
)"sv;

// GX_CTF_RA4: 4-bit red + 4-bit alpha -> RG8Unorm
static constexpr std::string_view FragRA4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    let r = quantize4(c.r);
    return vec4f(r, r, r, quantize4(c.a));
}
)"sv;

// GX_CTF_RA8: 8-bit red + 8-bit alpha -> RG8Unorm
static constexpr std::string_view FragRA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(c.r, c.r, c.r, c.a);
}
)"sv;

// GX_CTF_A8: 8-bit alpha -> R8Unorm
static constexpr std::string_view FragA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let a = textureSample(src, src_samp, in.uv).a;
    return vec4f(a, a, a, a);
}
)"sv;

// GX_CTF_R8: 8-bit red -> R8Unorm
static constexpr std::string_view FragR8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let r = textureSample(src, src_samp, in.uv).r;
    return vec4f(r, r, r, r);
}
)"sv;

// GX_CTF_G8: 8-bit green -> R8Unorm
static constexpr std::string_view FragG8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let g = textureSample(src, src_samp, in.uv).g;
    return vec4f(g, g, g, g);
}
)"sv;

// GX_CTF_B8: 8-bit blue -> R8Unorm
static constexpr std::string_view FragB8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let b = textureSample(src, src_samp, in.uv).b;
    return vec4f(b, b, b, b);
}
)"sv;

// GX_CTF_RG8: 8-bit red + 8-bit green -> RG8Unorm
static constexpr std::string_view FragRG8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(c.r, c.r, c.r, c.g);
}
)"sv;

// GX_CTF_GB8: 8-bit green + 8-bit blue -> RG8Unorm
static constexpr std::string_view FragGB8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(c.g, c.g, c.g, c.b);
}
)"sv;

struct ConvPipeline {
  GXTexFmt fmt;
  std::string_view fragShader;
  wgpu::TextureFormat outputFormat;
  const char* label;
};

static constexpr std::array ConvPipelines{
    ConvPipeline{GX_TF_I4, FragI4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv I4"},
    ConvPipeline{GX_TF_I8, FragI8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv I8"},
    ConvPipeline{GX_TF_IA4, FragIA4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv IA4"},
    ConvPipeline{GX_TF_IA8, FragIA8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv IA8"},
    ConvPipeline{GX_TF_RGB565, FragRGB565, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RGB565"},
    ConvPipeline{GX_CTF_R4, FragR4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv R4"},
    ConvPipeline{GX_CTF_RA4, FragRA4, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RA4"},
    ConvPipeline{GX_CTF_RA8, FragRA8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RA8"},
    ConvPipeline{GX_CTF_A8, FragA8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv A8"},
    ConvPipeline{GX_CTF_R8, FragR8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv R8"},
    ConvPipeline{GX_CTF_G8, FragG8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv G8"},
    ConvPipeline{GX_CTF_B8, FragB8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv B8"},
    ConvPipeline{GX_CTF_RG8, FragRG8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv RG8"},
    ConvPipeline{GX_CTF_GB8, FragGB8, wgpu::TextureFormat::RGBA8Unorm, "TexCopyConv GB8"},
};

static wgpu::BindGroupLayout g_bindGroupLayout;
static wgpu::Sampler g_nearestSampler;
static wgpu::Sampler g_linearSampler;
static absl::flat_hash_map<GXTexFmt, wgpu::RenderPipeline> g_pipelines;
static wgpu::RenderPipeline g_blitPipeline;

static wgpu::RenderPipeline create_pipeline(const ConvPipeline& conv) {
  std::string shaderSource;
  shaderSource.reserve(ShaderPreamble.size() + conv.fragShader.size());
  shaderSource += ShaderPreamble;
  shaderSource += conv.fragShader;

  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{
      .code = shaderSource.c_str(),
  }};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = conv.label,
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);

  const std::array colorTargets{wgpu::ColorTargetState{
      .format = conv.outputFormat,
  }};
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };

  constexpr wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);

  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .label = conv.label,
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
  return g_device.CreateRenderPipeline(&pipelineDescriptor);
}

bool needs_conversion(const GXTexFmt fmt) { return g_pipelines.contains(fmt); }

void initialize() {
  constexpr std::array bindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler =
              wgpu::SamplerBindingLayout{
                  .type = wgpu::SamplerBindingType::Filtering,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Vertex,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Uniform,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
      .label = "TexCopyConv Bind Group Layout",
      .entryCount = bindGroupLayoutEntries.size(),
      .entries = bindGroupLayoutEntries.data(),
  };
  g_bindGroupLayout = g_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

  g_blitPipeline = create_pipeline(
      {GX_TF_RGBA8, FragPassthrough, webgpu::g_graphicsConfig.surfaceConfiguration.format, "TexCopyConv Blit"});
  for (const auto& conv : ConvPipelines) {
    g_pipelines[conv.fmt] = create_pipeline(conv);
    if (conv.outputFormat != to_wgpu(conv.fmt)) {
      Log.fatal("Output format mismatch for {}", conv.fmt);
    }
  }

  constexpr wgpu::SamplerDescriptor nearestSamplerDescriptor{
      .label = "TexCopyConv Nearest Sampler",
      .magFilter = wgpu::FilterMode::Nearest,
      .minFilter = wgpu::FilterMode::Nearest,
  };
  g_nearestSampler = g_device.CreateSampler(&nearestSamplerDescriptor);

  constexpr wgpu::SamplerDescriptor linearSamplerDescriptor{
      .label = "TexCopyConv Linear Sampler",
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
  };
  g_linearSampler = g_device.CreateSampler(&linearSamplerDescriptor);
}

void shutdown() {
  g_pipelines.clear();
  g_blitPipeline = {};
  g_bindGroupLayout = {};
  g_nearestSampler = {};
  g_linearSampler = {};
}

static void execute(const wgpu::CommandEncoder& cmd, const ConvRequest& req, const wgpu::RenderPipeline& pipeline) {
  const auto& sampler = req.sampleFilter == SampleFilter::Linear ? g_linearSampler : g_nearestSampler;
  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .sampler = sampler,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .textureView = req.srcView,
      },
      wgpu::BindGroupEntry{
          .binding = 2,
          .buffer = g_uniformBuffer,
          .offset = req.uniformRange.offset,
          .size = req.uniformRange.size,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = g_bindGroupLayout,
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
      .label = "TexCopyConv Pass",
      .colorAttachmentCount = colorAttachments.size(),
      .colorAttachments = colorAttachments.data(),
  };
  const auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.Draw(3);
  pass.End();
}

void run(const wgpu::CommandEncoder& cmd, const ConvRequest& req) {
  const auto it = g_pipelines.find(req.fmt);
  if (it == g_pipelines.end()) {
    Log.fatal("No copy conversion pipeline for format {}", static_cast<int>(req.fmt));
  }
  execute(cmd, req, it->second);
}

void blit(const wgpu::CommandEncoder& cmd, const ConvRequest& req) { execute(cmd, req, g_blitPipeline); }

} // namespace aurora::gfx::tex_copy_conv

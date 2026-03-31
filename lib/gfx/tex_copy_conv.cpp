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

fn quantize4(v: f32) -> f32 {
    return floor(v * 16.0) / 15.0;
}
)";

// GX_TF_I4: 4-bit intensity -> R8Unorm (quantized)
static constexpr std::string_view FragI4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let rgb = textureSample(src, src_samp, in.uv).rgb;
    let i = quantize4(intensity(rgb));
    return vec4f(i, 0.0, 0.0, 1.0);
}
)";

// GX_TF_I8: 8-bit intensity -> R8Unorm
static constexpr std::string_view FragI8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let rgb = textureSample(src, src_samp, in.uv).rgb;
    let i = intensity(rgb);
    return vec4f(i, 0.0, 0.0, 1.0);
}
)";

// GX_TF_IA4: 4-bit intensity + 4-bit alpha -> RG8Unorm
static constexpr std::string_view FragIA4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    let i = quantize4(intensity(c.rgb));
    let a = quantize4(c.a);
    return vec4f(i, a, 0.0, 1.0);
}
)";

// GX_TF_IA8: 8-bit intensity + 8-bit alpha -> RG8Unorm
static constexpr std::string_view FragIA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    let i = intensity(c.rgb);
    return vec4f(i, c.a, 0.0, 1.0);
}
)";

// GX_CTF_R4: 4-bit red -> R8Unorm
static constexpr std::string_view FragR4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let r = quantize4(textureSample(src, src_samp, in.uv).r);
    return vec4f(r, 0.0, 0.0, 1.0);
}
)";

// GX_CTF_RA4: 4-bit red + 4-bit alpha -> RG8Unorm
static constexpr std::string_view FragRA4 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(quantize4(c.r), quantize4(c.a), 0.0, 1.0);
}
)";

// GX_CTF_RA8: 8-bit red + 8-bit alpha -> RG8Unorm
static constexpr std::string_view FragRA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(c.ra, 0.0, 1.0);
}
)";

// GX_CTF_A8: 8-bit alpha -> R8Unorm
static constexpr std::string_view FragA8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let a = textureSample(src, src_samp, in.uv).a;
    return vec4f(a, 0.0, 0.0, 1.0);
}
)";

// GX_CTF_R8: 8-bit red -> R8Unorm
static constexpr std::string_view FragR8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let r = textureSample(src, src_samp, in.uv).r;
    return vec4f(r, 0.0, 0.0, 1.0);
}
)";

// GX_CTF_G8: 8-bit green -> R8Unorm
static constexpr std::string_view FragG8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let g = textureSample(src, src_samp, in.uv).g;
    return vec4f(g, 0.0, 0.0, 1.0);
}
)";

// GX_CTF_B8: 8-bit blue -> R8Unorm
static constexpr std::string_view FragB8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let b = textureSample(src, src_samp, in.uv).b;
    return vec4f(b, 0.0, 0.0, 1.0);
}
)";

// GX_CTF_RG8: 8-bit red + 8-bit green -> RG8Unorm
static constexpr std::string_view FragRG8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(c.rg, 0.0, 1.0);
}
)";

// GX_CTF_GB8: 8-bit green + 8-bit blue -> RG8Unorm
static constexpr std::string_view FragGB8 = R"(
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSample(src, src_samp, in.uv);
    return vec4f(c.gb, 0.0, 1.0);
}
)";

struct ConvPipeline {
  GXTexFmt fmt;
  std::string_view fragShader;
  wgpu::TextureFormat outputFormat;
  const char* label;
};

static constexpr std::array ConvPipelines{
    ConvPipeline{GX_TF_I4, FragI4, wgpu::TextureFormat::R8Unorm, "TexCopyConv I4"},
    ConvPipeline{GX_TF_I8, FragI8, wgpu::TextureFormat::R8Unorm, "TexCopyConv I8"},
    ConvPipeline{GX_TF_IA4, FragIA4, wgpu::TextureFormat::RG8Unorm, "TexCopyConv IA4"},
    ConvPipeline{GX_TF_IA8, FragIA8, wgpu::TextureFormat::RG8Unorm, "TexCopyConv IA8"},
    ConvPipeline{GX_CTF_R4, FragR4, wgpu::TextureFormat::R8Unorm, "TexCopyConv R4"},
    ConvPipeline{GX_CTF_RA4, FragRA4, wgpu::TextureFormat::RG8Unorm, "TexCopyConv RA4"},
    ConvPipeline{GX_CTF_RA8, FragRA8, wgpu::TextureFormat::RG8Unorm, "TexCopyConv RA8"},
    ConvPipeline{GX_CTF_A8, FragA8, wgpu::TextureFormat::R8Unorm, "TexCopyConv A8"},
    ConvPipeline{GX_CTF_R8, FragR8, wgpu::TextureFormat::R8Unorm, "TexCopyConv R8"},
    ConvPipeline{GX_CTF_G8, FragG8, wgpu::TextureFormat::R8Unorm, "TexCopyConv G8"},
    ConvPipeline{GX_CTF_B8, FragB8, wgpu::TextureFormat::R8Unorm, "TexCopyConv B8"},
    ConvPipeline{GX_CTF_RG8, FragRG8, wgpu::TextureFormat::RG8Unorm, "TexCopyConv RG8"},
    ConvPipeline{GX_CTF_GB8, FragGB8, wgpu::TextureFormat::RG8Unorm, "TexCopyConv GB8"},
};

static wgpu::BindGroupLayout g_bindGroupLayout;
static absl::flat_hash_map<GXTexFmt, wgpu::RenderPipeline> g_pipelines;
static std::vector<ConvRequest> g_queue;

static wgpu::RenderPipeline create_pipeline(const ConvPipeline& conv) {
  std::string shaderSource;
  shaderSource.reserve(ShaderPreamble.size() + conv.fragShader.size());
  shaderSource += ShaderPreamble;
  shaderSource += conv.fragShader;

  wgpu::ShaderSourceWGSL wgslSource{};
  wgslSource.code = shaderSource.c_str();
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = conv.label,
  };
  auto module = g_device.CreateShaderModule(&moduleDescriptor);

  const std::array colorTargets{wgpu::ColorTargetState{
      .format = conv.outputFormat,
      .writeMask = wgpu::ColorWriteMask::All,
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
      .multisample =
          wgpu::MultisampleState{
              .count = 1,
              .mask = UINT32_MAX,
          },
      .fragment = &fragmentState,
  };
  return g_device.CreateRenderPipeline(&pipelineDescriptor);
}

bool needs_conversion(const GXTexFmt fmt) { return g_pipelines.contains(fmt); }

void initialize() {
  // Bind group layout: sampler + source texture
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
  };
  const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
      .label = "TexCopyConv Bind Group Layout",
      .entryCount = bindGroupLayoutEntries.size(),
      .entries = bindGroupLayoutEntries.data(),
  };
  g_bindGroupLayout = g_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

  for (const auto& conv : ConvPipelines) {
    g_pipelines[conv.fmt] = create_pipeline(conv);
    if (conv.outputFormat != to_wgpu(conv.fmt)) {
      Log.fatal("Output format mismatch for {}", conv.fmt);
    }
  }
}

void shutdown() {
  g_queue.clear();
  g_pipelines.clear();
  g_bindGroupLayout = {};
}

void queue(ConvRequest req) { g_queue.push_back(std::move(req)); }

void execute(const wgpu::CommandEncoder& cmd) {
  if (g_queue.empty()) {
    return;
  }

  constexpr wgpu::SamplerDescriptor samplerDescriptor{
      .label = "TexCopyConv Sampler",
      .magFilter = wgpu::FilterMode::Nearest,
      .minFilter = wgpu::FilterMode::Nearest,
  };
  const auto sampler = g_device.CreateSampler(&samplerDescriptor);

  for (const auto& [fmt, src, dst] : g_queue) {
    auto it = g_pipelines.find(fmt);
    if (it == g_pipelines.end()) {
      Log.fatal("No copy conversion pipeline for format {}", static_cast<int>(fmt));
    }

    const std::array bindGroupEntries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .sampler = sampler,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .textureView = src->view,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = g_bindGroupLayout,
        .entryCount = bindGroupEntries.size(),
        .entries = bindGroupEntries.data(),
    };
    auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);

    const std::array colorAttachments{
        wgpu::RenderPassColorAttachment{
            .view = dst->view,
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
    auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
    pass.SetPipeline(it->second);
    pass.SetBindGroup(0, bindGroup);
    pass.Draw(3);
    pass.End();
  }

  g_queue.clear();
}

} // namespace aurora::gfx::tex_copy_conv

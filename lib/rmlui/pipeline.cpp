#include "pipeline.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include <RmlUi/Core/Vertex.h>
#include <tracy/Tracy.hpp>

namespace aurora::rmlui {
namespace {
using namespace std::string_view_literals;

wgpu::BindGroupLayout g_commonBindGroupLayout;
wgpu::BindGroupLayout g_imageBindGroupLayout;
wgpu::BindGroupLayout g_uniformBindGroupLayout;
wgpu::Sampler g_sampler;

constexpr uint32_t DynamicGroup1 = 1u << 1u;
constexpr uint32_t DynamicGroup2 = 1u << 2u;

constexpr uint64_t CommonUniformBindingSize = AURORA_ALIGN(sizeof(UniformBlock), 16);
constexpr uint64_t ExtraUniformBindingSize =
    AURORA_ALIGN(std::max({sizeof(BlurUniformBlock), sizeof(DropShadowUniformBlock), sizeof(SimpleFilterUniformBlock),
                           sizeof(GradientUniformBlock), sizeof(SeedResampleUniformBlock)}),
                 16);

constexpr std::string_view vertexSource = R"(
struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

struct Uniforms {
    mvp: mat4x4<f32>,
    translation: vec4<f32>,
    gamma: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    var translatedPos = uniforms.translation.xy + in.position;

    out.position = uniforms.mvp * vec4<f32>(translatedPos, 0.0, 1.0);
    out.color = in.color;
    out.uv = in.uv;
    return out;
}
)"sv;

constexpr std::string_view fragmentSource = R"(
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

struct Uniforms {
    mvp: mat4x4<f32>,
    translation: vec4<f32>,
    gamma: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;

@fragment
fn main(in: VertexOutput) -> @location(0) vec4<f32> {
    let color = in.color * textureSample(t, s, in.uv);
    if (uniforms.gamma == 1.0) {
        return color;
    }
    let corrected_color = pow(color.rgb, vec3<f32>(uniforms.gamma));
    return vec4<f32>(corrected_color, color.a);
}
)"sv;

constexpr std::string_view gradientFragmentSource = R"(
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

struct Uniforms {
    mvp: mat4x4<f32>,
    translation: vec4<f32>,
    gamma: f32,
};

struct GradientUniforms {
    function: i32,
    num_stops: i32,
    p: vec2<f32>,
    v: vec2<f32>,
    padding: vec2<f32>,
    stop_colors: array<vec4<f32>, 16>,
    stop_positions: array<vec4<f32>, 4>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(1) @binding(0) var<uniform> gradient: GradientUniforms;

const LINEAR: i32 = 0;
const RADIAL: i32 = 1;
const CONIC: i32 = 2;
const REPEATING_LINEAR: i32 = 3;
const REPEATING_RADIAL: i32 = 4;
const REPEATING_CONIC: i32 = 5;
const PI: f32 = 3.14159265;

fn bayer_dither(position: vec4<f32>) -> f32 {
    let bayer = array<u32, 64>(
        0u, 32u, 8u, 40u, 2u, 34u, 10u, 42u,
        48u, 16u, 56u, 24u, 50u, 18u, 58u, 26u,
        12u, 44u, 4u, 36u, 14u, 46u, 6u, 38u,
        60u, 28u, 52u, 20u, 62u, 30u, 54u, 22u,
        3u, 35u, 11u, 43u, 1u, 33u, 9u, 41u,
        51u, 19u, 59u, 27u, 49u, 17u, 57u, 25u,
        15u, 47u, 7u, 39u, 13u, 45u, 5u, 37u,
        63u, 31u, 55u, 23u, 61u, 29u, 53u, 21u
    );
    let x = u32(position.x) % 8u;
    let y = u32(position.y) % 8u;
    return (f32(bayer[x + y * 8u]) / 64.0 - 0.5) / 255.0;
}

fn stop_position(index: i32) -> f32 {
    let stop_index = u32(index);
    let group_index = stop_index / 4u;
    let component_index = stop_index % 4u;
    return gradient.stop_positions[group_index][component_index];
}

fn stop_color_mix(t: f32) -> vec4<f32> {
    var color = gradient.stop_colors[0];

    for (var i = 1; i < 16; i = i + 1) {
        if (i < gradient.num_stops) {
            color = mix(color, gradient.stop_colors[u32(i)], smoothstep(stop_position(i - 1), stop_position(i), t));
        }
    }

    return color;
}

@fragment
fn main(in: VertexOutput) -> @location(0) vec4<f32> {
    var t = 0.0;

    if (gradient.function == LINEAR || gradient.function == REPEATING_LINEAR) {
        let dist_square = dot(gradient.v, gradient.v);
        let v = in.uv - gradient.p;
        t = dot(gradient.v, v) / dist_square;
    } else if (gradient.function == RADIAL || gradient.function == REPEATING_RADIAL) {
        let v = in.uv - gradient.p;
        t = length(gradient.v * v);
    } else if (gradient.function == CONIC || gradient.function == REPEATING_CONIC) {
        let v = in.uv - gradient.p;
        let rotated = vec2<f32>(
            gradient.v.x * v.x + gradient.v.y * v.y,
            -gradient.v.y * v.x + gradient.v.x * v.y
        );
        t = 0.5 + atan2(-rotated.x, rotated.y) / (2.0 * PI);
    }

    if (gradient.function == REPEATING_LINEAR ||
        gradient.function == REPEATING_RADIAL ||
        gradient.function == REPEATING_CONIC) {
        let t0 = stop_position(0);
        let t1 = stop_position(gradient.num_stops - 1);
        let span = t1 - t0;
        t = t0 + (t - t0) - span * floor((t - t0) / span);
    }

    let color = in.color * stop_color_mix(t);
    if (uniforms.gamma == 1.0) {
        return color;
    }
    let corrected_color = pow(color.rgb, vec3<f32>(uniforms.gamma));
    let dithered_color = clamp(corrected_color + vec3<f32>(bayer_dither(in.position)), vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(dithered_color, color.a);
}
)"sv;

constexpr std::string_view fullscreenVertexSource = R"(
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(0.0, 0.0),
    vec2(0.0, 2.0),
    vec2(2.0, 0.0),
);

@vertex
fn main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4<f32>(pos[vtxIdx], 0.0, 1.0);
    out.uv = uvs[vtxIdx];
    return out;
}
)"sv;

constexpr std::string_view blurVertexSource = R"(
struct BlurUniforms {
    texel_offset: vec2<f32>,
    radius: f32,
    padding: f32,
    tex_coord_min: vec2<f32>,
    tex_coord_max: vec2<f32>,
    weights: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv0: vec2<f32>,
    @location(1) uv1: vec2<f32>,
    @location(2) uv2: vec2<f32>,
    @location(3) uv3: vec2<f32>,
    @location(4) uv4: vec2<f32>,
    @location(5) uv5: vec2<f32>,
    @location(6) uv6: vec2<f32>,
};

@group(2) @binding(0) var<uniform> blur: BlurUniforms;

const BLUR_NUM_WEIGHTS: i32 = 4;

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(0.0, 0.0),
    vec2(0.0, 2.0),
    vec2(2.0, 0.0),
);

fn blur_uv(uv: vec2<f32>, index: i32) -> vec2<f32> {
    return uv - f32(index - BLUR_NUM_WEIGHTS + 1) * blur.texel_offset;
}

@vertex
fn main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    let uv = uvs[vtxIdx];
    var out: VertexOutput;
    out.position = vec4<f32>(pos[vtxIdx], 0.0, 1.0);
    out.uv0 = blur_uv(uv, 0);
    out.uv1 = blur_uv(uv, 1);
    out.uv2 = blur_uv(uv, 2);
    out.uv3 = blur_uv(uv, 3);
    out.uv4 = blur_uv(uv, 4);
    out.uv5 = blur_uv(uv, 5);
    out.uv6 = blur_uv(uv, 6);
    return out;
}
)"sv;

constexpr std::string_view blitFragmentSource = R"(
@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    return textureSample(t, s, uv);
}
)"sv;

constexpr std::string_view opaqueBlitFragmentSource = R"(
@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let color = textureSample(t, s, uv);
    return vec4<f32>(color.rgb, 1.0);
}
)"sv;

constexpr std::string_view seedResampleFragmentSource = R"(
struct SeedUniforms {
    sampler_mode: u32,
    frame_width: f32,
    frame_height: f32,
    pad: u32,
};

@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;
@group(2) @binding(0) var<uniform> uniforms: SeedUniforms;

fn sample_by_pixel(pixel: vec2<i32>) -> vec4<f32> {
    let source_dims = textureDimensions(t);
    let max_coord = vec2<i32>(source_dims) - vec2<i32>(1, 1);
    let coord = clamp(pixel, vec2<i32>(0, 0), max_coord);
    return textureLoad(t, coord, 0);
}

fn sample_area(frag_position: vec4<f32>) -> vec4<f32> {
    let source_size = vec2<f32>(textureDimensions(t));
    let target_size = max(vec2<f32>(uniforms.frame_width, uniforms.frame_height), vec2<f32>(1.0, 1.0));

    let source_min = clamp((frag_position.xy - vec2<f32>(0.5, 0.5)) / target_size,
                           vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0)) * source_size;
    let source_max = clamp((frag_position.xy + vec2<f32>(0.5, 0.5)) / target_size,
                           vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0)) * source_size;

    let first_pixel = vec2<i32>(floor(source_min));
    let last_pixel = vec2<i32>(ceil(source_max));
    let max_iterations: i32 = 16;

    var avg_color = vec4<f32>(0.0, 0.0, 0.0, 0.0);
    var total_weight = 0.0;

    for (var iy: i32 = 0; iy < max_iterations; iy = iy + 1) {
        let source_y = first_pixel.y + iy;
        if (source_y < last_pixel.y) {
            let y0 = f32(source_y);
            let weight_y = max(min(source_max.y, y0 + 1.0) - max(source_min.y, y0), 0.0);

            for (var ix: i32 = 0; ix < max_iterations; ix = ix + 1) {
                let source_x = first_pixel.x + ix;
                if (source_x < last_pixel.x) {
                    let x0 = f32(source_x);
                    let weight_x = max(min(source_max.x, x0 + 1.0) - max(source_min.x, x0), 0.0);
                    let weight = weight_x * weight_y;
                    avg_color += weight * sample_by_pixel(vec2<i32>(source_x, source_y));
                    total_weight += weight;
                }
            }
        }
    }

    return avg_color / max(total_weight, 0.000001);
}

@fragment
fn main(@builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    var color = textureSample(t, s, uv);
    if (uniforms.sampler_mode == 1u) {
        color = sample_area(position);
    }
    return vec4(color.rgb, 1.0);
}
)"sv;

constexpr std::string_view simpleFilterFragmentSource = R"(
struct SimpleFilterUniforms {
    matrix: mat4x4<f32>,
    opacity: vec4<f32>,
};

@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;
@group(2) @binding(0) var<uniform> simple_filter: SimpleFilterUniforms;

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let tex_color = textureSample(t, s, uv);
    let transformed_color = simple_filter.matrix * tex_color;
    return vec4<f32>(transformed_color.rgb, tex_color.a) * simple_filter.opacity.x;
}
)"sv;

constexpr std::string_view maskImageFragmentSource = R"(
@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;
@group(2) @binding(0) var mask_t: texture_2d<f32>;

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let tex_color = textureSample(t, s, uv);
    let mask_alpha = textureSample(mask_t, s, uv).a;
    return tex_color * mask_alpha;
}
)"sv;

constexpr std::string_view blurFragmentSource = R"(
struct BlurUniforms {
    texel_offset: vec2<f32>,
    radius: f32,
    padding: f32,
    tex_coord_min: vec2<f32>,
    tex_coord_max: vec2<f32>,
    weights: vec4<f32>,
};

@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;
@group(2) @binding(0) var<uniform> blur: BlurUniforms;

fn get_weight(index: i32) -> f32 {
    return blur.weights[u32(abs(index))];
}

fn sample_blur(sample_uv: vec2<f32>, offset_index: i32) -> vec4<f32> {
    let in_region = step(blur.tex_coord_min, sample_uv) * step(sample_uv, blur.tex_coord_max);
    return textureSample(t, s, sample_uv) * get_weight(offset_index) * in_region.x * in_region.y;
}

@fragment
fn main(@location(0) uv0: vec2<f32>, @location(1) uv1: vec2<f32>, @location(2) uv2: vec2<f32>,
        @location(3) uv3: vec2<f32>, @location(4) uv4: vec2<f32>, @location(5) uv5: vec2<f32>,
        @location(6) uv6: vec2<f32>) -> @location(0) vec4<f32> {
    var color = sample_blur(uv0, -3);
    color += sample_blur(uv1, -2);
    color += sample_blur(uv2, -1);
    color += sample_blur(uv3, 0);
    color += sample_blur(uv4, 1);
    color += sample_blur(uv5, 2);
    color += sample_blur(uv6, 3);
    return color;
}
)"sv;

constexpr std::string_view regionBlitFragmentSource = R"(
struct BlurUniforms {
    texel_offset: vec2<f32>,
    radius: f32,
    padding: f32,
    tex_coord_min: vec2<f32>,
    tex_coord_max: vec2<f32>,
    weights: vec4<f32>,
};

@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;
@group(2) @binding(0) var<uniform> blur: BlurUniforms;

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let sample_uv = mix(blur.tex_coord_min, blur.tex_coord_max, uv);
    return textureSample(t, s, sample_uv);
}
)"sv;

constexpr std::string_view dropShadowFragmentSource = R"(
struct DropShadowUniforms {
    color: vec4<f32>,
    uv_offset: vec2<f32>,
    tex_coord_min: vec2<f32>,
    tex_coord_max: vec2<f32>,
};

@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;
@group(2) @binding(0) var<uniform> shadow: DropShadowUniforms;

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let sample_uv = uv - shadow.uv_offset;
    let in_region = step(shadow.tex_coord_min, sample_uv) * step(sample_uv, shadow.tex_coord_max);
    let alpha = textureSample(t, s, sample_uv).a * in_region.x * in_region.y;
    return shadow.color * alpha;
}
)"sv;

wgpu::ComputeState compile_shader(std::string_view wgslSource, std::string_view label) {
  const wgpu::ShaderSourceWGSL source{
      wgpu::ShaderSourceWGSL::Init{
          .nextInChain = nullptr,
          .code = wgslSource,
      },
  };
  const wgpu::ShaderModuleDescriptor desc{
      .nextInChain = &source,
      .label = label,
  };
  return {
      .module = webgpu::g_device.CreateShaderModule(&desc),
      .entryPoint = "main",
  };
}

wgpu::BlendState blend_state(BlendMode mode) {
  switch (mode) {
  case BlendMode::Premultiplied:
    return {
        .color =
            {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
            },
        .alpha =
            {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
            },
    };
  case BlendMode::None:
  default:
    return {};
  }
}

const wgpu::PipelineLayout create_pipeline_layout(PipelineKind kind) {
  std::array<wgpu::BindGroupLayout, 3> layouts{};
  uint32_t layoutCount = 0;
  layouts[layoutCount++] = g_commonBindGroupLayout;

  switch (kind) {
  case PipelineKind::Gradient:
    layouts[layoutCount++] = g_uniformBindGroupLayout;
    break;
  case PipelineKind::MaskImage:
    layouts[layoutCount++] = g_imageBindGroupLayout;
    layouts[layoutCount++] = g_imageBindGroupLayout;
    break;
  case PipelineKind::Blur:
  case PipelineKind::RegionBlit:
  case PipelineKind::DropShadow:
  case PipelineKind::SimpleFilter:
  case PipelineKind::SeedResample:
    layouts[layoutCount++] = g_imageBindGroupLayout;
    layouts[layoutCount++] = g_uniformBindGroupLayout;
    break;
  case PipelineKind::Geometry:
  case PipelineKind::Blit:
  case PipelineKind::OpaqueBlit:
  default:
    layouts[layoutCount++] = g_imageBindGroupLayout;
    break;
  }

  const wgpu::PipelineLayoutDescriptor layoutDesc{
      .bindGroupLayoutCount = layoutCount,
      .bindGroupLayouts = layouts.data(),
  };
  return webgpu::g_device.CreatePipelineLayout(&layoutDesc);
}

const std::string_view fragment_source(PipelineKind kind) {
  switch (kind) {
  case PipelineKind::Geometry:
    return fragmentSource;
  case PipelineKind::Gradient:
    return gradientFragmentSource;
  case PipelineKind::OpaqueBlit:
    return opaqueBlitFragmentSource;
  case PipelineKind::SeedResample:
    return seedResampleFragmentSource;
  case PipelineKind::Blur:
    return blurFragmentSource;
  case PipelineKind::RegionBlit:
    return regionBlitFragmentSource;
  case PipelineKind::DropShadow:
    return dropShadowFragmentSource;
  case PipelineKind::SimpleFilter:
    return simpleFilterFragmentSource;
  case PipelineKind::MaskImage:
    return maskImageFragmentSource;
  case PipelineKind::Blit:
  default:
    return blitFragmentSource;
  }
}

const std::string_view vertex_source(VertexLayoutKind kind) {
  switch (kind) {
  case VertexLayoutKind::Geometry:
    return vertexSource;
  case VertexLayoutKind::BlurFullscreen:
    return blurVertexSource;
  case VertexLayoutKind::Fullscreen:
  default:
    return fullscreenVertexSource;
  }
}

} // namespace

void initialize_pipeline() {
  constexpr std::array commonEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
          .buffer =
              {
                  .type = wgpu::BufferBindingType::Uniform,
                  .hasDynamicOffset = true,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler =
              {
                  .type = wgpu::SamplerBindingType::Filtering,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor commonDesc{
      .entryCount = commonEntries.size(),
      .entries = commonEntries.data(),
  };
  g_commonBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&commonDesc);

  constexpr std::array imageEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              {
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor imageDesc{
      .entryCount = imageEntries.size(),
      .entries = imageEntries.data(),
  };
  g_imageBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&imageDesc);

  constexpr std::array uniformEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
          .buffer =
              {
                  .type = wgpu::BufferBindingType::Uniform,
                  .hasDynamicOffset = true,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor uniformDesc{
      .entryCount = uniformEntries.size(),
      .entries = uniformEntries.data(),
  };
  g_uniformBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&uniformDesc);

  constexpr wgpu::SamplerDescriptor samplerDesc{
      .addressModeU = wgpu::AddressMode::Repeat,
      .addressModeV = wgpu::AddressMode::Repeat,
      .addressModeW = wgpu::AddressMode::Repeat,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Linear,
      .maxAnisotropy = 1,
  };
  g_sampler = webgpu::g_device.CreateSampler(&samplerDesc);
}

void shutdown_pipeline() {
  g_commonBindGroupLayout = {};
  g_imageBindGroupLayout = {};
  g_uniformBindGroupLayout = {};
  g_sampler = {};
}

gfx::BindGroupRef texture_bind_group_ref(const wgpu::TextureView& view) {
  const std::array entries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = view,
      },
  };
  const wgpu::BindGroupDescriptor desc{
      .layout = g_imageBindGroupLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return gfx::bind_group_ref(desc);
}

gfx::BindGroupRef common_bind_group_ref() {
  const std::array entries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .buffer = gfx::g_uniformBuffer,
          .offset = 0,
          .size = CommonUniformBindingSize,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .sampler = g_sampler,
      },
  };
  const wgpu::BindGroupDescriptor desc{
      .layout = g_commonBindGroupLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return gfx::bind_group_ref(desc);
}

gfx::BindGroupRef uniform_bind_group_ref() {
  const std::array entries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .buffer = gfx::g_uniformBuffer,
          .offset = 0,
          .size = ExtraUniformBindingSize,
      },
  };
  const wgpu::BindGroupDescriptor desc{
      .layout = g_uniformBindGroupLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return gfx::bind_group_ref(desc);
}

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  ZoneScoped;
  const auto kind = static_cast<PipelineKind>(config.kind);
  const auto vertexLayoutKind = static_cast<VertexLayoutKind>(config.vertexLayout);
  const auto colorFormat = static_cast<wgpu::TextureFormat>(config.colorFormat);
  const auto stencilFormat = static_cast<wgpu::TextureFormat>(config.stencilFormat);
  const auto stencilMode = static_cast<StencilMode>(config.stencilMode);
  const auto blendMode = static_cast<BlendMode>(config.blendMode);

  const auto vertexShader = compile_shader(vertex_source(vertexLayoutKind), "RmlUi Vertex Shader");
  const auto fragmentShader = compile_shader(fragment_source(kind), "RmlUi Fragment Shader");

  constexpr std::array vertexAttributes{
      wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x2,
          .offset = offsetof(Rml::Vertex, position),
          .shaderLocation = 0,
      },
      wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x2,
          .offset = offsetof(Rml::Vertex, tex_coord),
          .shaderLocation = 1,
      },
      wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Unorm8x4,
          .offset = offsetof(Rml::Vertex, colour),
          .shaderLocation = 2,
      },
  };
  const std::array vertexBufferLayouts{
      wgpu::VertexBufferLayout{
          .stepMode = wgpu::VertexStepMode::Vertex,
          .arrayStride = sizeof(Rml::Vertex),
          .attributeCount = vertexAttributes.size(),
          .attributes = vertexAttributes.data(),
      },
  };

  const auto blend = blend_state(blendMode);
  const wgpu::ColorTargetState colorState{
      .format = colorFormat,
      .blend = blendMode == BlendMode::None ? nullptr : &blend,
      .writeMask = static_cast<wgpu::ColorWriteMask>(config.colorWriteMask),
  };
  const wgpu::FragmentState fragmentState{
      .module = fragmentShader.module,
      .entryPoint = fragmentShader.entryPoint,
      .targetCount = 1,
      .targets = &colorState,
  };

  wgpu::DepthStencilState depthStencilState{};
  const wgpu::DepthStencilState* depthStencil = nullptr;
  if (stencilMode != StencilMode::None) {
    wgpu::CompareFunction compare = wgpu::CompareFunction::Always;
    wgpu::StencilOperation passOp = wgpu::StencilOperation::Keep;
    switch (stencilMode) {
    case StencilMode::EqualKeep:
      compare = wgpu::CompareFunction::Equal;
      break;
    case StencilMode::ClipReplace:
      passOp = wgpu::StencilOperation::Replace;
      break;
    case StencilMode::ClipIntersect:
      compare = wgpu::CompareFunction::Equal;
      passOp = wgpu::StencilOperation::IncrementClamp;
      break;
    case StencilMode::AlwaysKeep:
    case StencilMode::None:
    default:
      break;
    }
    const wgpu::StencilFaceState face{
        .compare = compare,
        .failOp = wgpu::StencilOperation::Keep,
        .depthFailOp = wgpu::StencilOperation::Keep,
        .passOp = passOp,
    };
    depthStencilState = {
        .format = stencilFormat,
        .stencilFront = face,
        .stencilBack = face,
        .stencilReadMask = 0xFF,
        .stencilWriteMask = 0xFF,
    };
    depthStencil = &depthStencilState;
  }

  const bool hasVertexBuffer = vertexLayoutKind == VertexLayoutKind::Geometry;
  const auto pipelineLayout = create_pipeline_layout(kind);
  const auto label = fmt::format("RmlUi Pipeline {}", config.kind);
  const wgpu::RenderPipelineDescriptor pipelineDesc{
      .label = label.c_str(),
      .layout = pipelineLayout,
      .vertex =
          {
              .module = vertexShader.module,
              .entryPoint = vertexShader.entryPoint,
              .bufferCount = hasVertexBuffer ? vertexBufferLayouts.size() : 0,
              .buffers = hasVertexBuffer ? vertexBufferLayouts.data() : nullptr,
          },
      .primitive =
          {
              .topology = wgpu::PrimitiveTopology::TriangleList,
              .stripIndexFormat = wgpu::IndexFormat::Undefined,
              .frontFace = wgpu::FrontFace::CW,
              .cullMode = wgpu::CullMode::None,
          },
      .depthStencil = depthStencil,
      .multisample =
          {
              .count = config.sampleCount,
          },
      .fragment = &fragmentState,
  };
  return webgpu::g_device.CreateRenderPipeline(&pipelineDesc);
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass) {
  if (!gfx::bind_pipeline(data.pipeline, pass)) {
    return;
  }

  const auto commonBindGroup = gfx::find_bind_group(common_bind_group_ref());
  const std::array commonOffsets{data.uniformRange.offset};
  pass.SetBindGroup(0, commonBindGroup, commonOffsets.size(), commonOffsets.data());

  if (data.bindGroup1 != 0) {
    const auto bindGroup = gfx::find_bind_group(data.bindGroup1);
    if ((data.dynamicBindGroupMask & DynamicGroup1) != 0) {
      const std::array offsets{data.bindGroup1DynamicOffset};
      pass.SetBindGroup(1, bindGroup, offsets.size(), offsets.data());
    } else {
      pass.SetBindGroup(1, bindGroup);
    }
  }

  if (data.bindGroup2 != 0) {
    const auto bindGroup = gfx::find_bind_group(data.bindGroup2);
    if ((data.dynamicBindGroupMask & DynamicGroup2) != 0) {
      const std::array offsets{data.bindGroup2DynamicOffset};
      pass.SetBindGroup(2, bindGroup, offsets.size(), offsets.data());
    } else {
      pass.SetBindGroup(2, bindGroup);
    }
  }

  if (data.hasBlendConstant != 0) {
    const wgpu::Color color{data.blendConstant[0], data.blendConstant[1], data.blendConstant[2], data.blendConstant[3]};
    pass.SetBlendConstant(&color);
  }
  pass.SetStencilReference(data.stencilRef);

  if (static_cast<DrawKind>(data.drawKind) == DrawKind::Geometry) {
    pass.SetVertexBuffer(0, gfx::g_vertexBuffer, data.vertexRange.offset, data.vertexRange.size);
    pass.SetIndexBuffer(gfx::g_indexBuffer, wgpu::IndexFormat::Uint32, data.indexRange.offset, data.indexRange.size);
    pass.DrawIndexed(data.indexCount);
  } else {
    pass.Draw(data.vertexCount);
  }
}

uint32_t sampler_mode() noexcept {
  switch (webgpu::get_resampler()) {
  case SAMPLER_AREA:
    return 1;
  case SAMPLER_BILINEAR:
  default:
    return 0;
  }
}

} // namespace aurora::rmlui

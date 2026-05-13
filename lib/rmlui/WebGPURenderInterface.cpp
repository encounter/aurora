#include "WebGPURenderInterface.hpp"

#include "FileInterface_SDL.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../logging.hpp"
#include "../webgpu/gpu.hpp"
#include "../internal.hpp"
#include "../gfx/clear.hpp"

namespace aurora::rmlui {
namespace {
Module Log("aurora::rmlui::RenderInterface");

struct Image {
  std::unique_ptr<uint8_t[]> data;
  size_t size;
  uint32_t width;
  uint32_t height;
};

struct ShaderGeometryData {
  wgpu::Buffer m_vertexBuffer;
  wgpu::Buffer m_indexBuffer;
};

struct ShaderTextureData {
  wgpu::BindGroup m_bindGroup;
  wgpu::Texture m_texture;
  wgpu::TextureView m_textureView;
};

struct CompiledShaderData {
  GradientUniformBlock gradient;
};

Image get_image(const Rml::String& source) {
  FileInterface_SDL fileInterface;
  const Rml::FileHandle file = fileInterface.Open(source);
  if (file == Rml::FileHandle{}) {
    return {};
  }

  auto* stream = reinterpret_cast<SDL_IOStream*>(file);
  SDL_Surface* loadedSurface = SDL_LoadPNG_IO(stream, true);
  if (loadedSurface == nullptr) {
    Log.warn("Failed to load image '{}': {}", source, SDL_GetError());
    return {};
  }

  SDL_Surface* rgbaSurface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loadedSurface);
  if (rgbaSurface == nullptr) {
    Log.warn("Failed to convert image '{}': {}", source, SDL_GetError());
    return {};
  }

  const auto iconWidth = static_cast<uint32_t>(rgbaSurface->w);
  const auto iconHeight = static_cast<uint32_t>(rgbaSurface->h);
  const size_t rowSize = static_cast<size_t>(iconWidth) * 4;
  const size_t size = rowSize * static_cast<size_t>(iconHeight);
  auto ptr = std::make_unique<uint8_t[]>(size);
  for (uint32_t row = 0; row < iconHeight; ++row) {
    const auto* src = static_cast<const uint8_t*>(rgbaSurface->pixels) +
                      static_cast<size_t>(row) * static_cast<size_t>(rgbaSurface->pitch);
    auto* dst = ptr.get() + static_cast<size_t>(row) * rowSize;
    std::memcpy(dst, src, rowSize);

    // Convert colors to premultiplied alpha, which is necessary for correct alpha compositing.
    for (size_t col = 0; col < rowSize; col += 4) {
      const uint8_t alpha = dst[col + 3];
      for (size_t channel = 0; channel < 3; ++channel) {
        dst[col + channel] =
            static_cast<uint8_t>((static_cast<uint32_t>(dst[col + channel]) * static_cast<uint32_t>(alpha)) / 255);
      }
    }
  }

  SDL_DestroySurface(rgbaSurface);
  return Image{
      .data = std::move(ptr),
      .size = size,
      .width = iconWidth,
      .height = iconHeight,
  };
}

void sigma_to_params(float desiredSigma, int& passLevel, float& sigma) {
  constexpr int MaxPasses = 10;
  constexpr float MaxSinglePassSigma = 3.f;
  const int downsampleHint = static_cast<int>(desiredSigma * (2.f / MaxSinglePassSigma));
  passLevel = downsampleHint > 0 ? static_cast<int>(std::log2(static_cast<float>(downsampleHint))) : 0;
  passLevel = std::clamp(passLevel, 0, MaxPasses);
  sigma = std::clamp(desiredSigma / static_cast<float>(1 << passLevel), 0.f, MaxSinglePassSigma);
}

Rml::Vector4f blur_weights(float sigma, uint32_t radius) {
  std::array<float, MaxBlurRadius + 1> scalarWeights = {};
  float normalization = 0.f;
  radius = std::min(radius, MaxBlurRadius);
  for (uint32_t i = 0; i <= radius; ++i) {
    if (std::abs(sigma) < 0.1f) {
      scalarWeights[i] = i == 0 ? 1.f : 0.f;
    } else {
      const float x = static_cast<float>(i);
      scalarWeights[i] = std::exp(-(x * x) / (2.f * sigma * sigma));
    }
    normalization += (i == 0 ? 1.f : 2.f) * scalarWeights[i];
  }

  if (normalization > 0.f) {
    for (uint32_t i = 0; i <= radius; ++i) {
      scalarWeights[i] /= normalization;
    }
  }

  Rml::Vector4f weights = {};
  for (uint32_t i = 0; i <= radius; ++i) {
    weights[i] = scalarWeights[i];
  }
  return weights;
}

Rml::Vector4f to_colorf(Rml::ColourbPremultiplied color) {
  constexpr float InvByte = 1.f / 255.f;
  return {
      static_cast<float>(color.red) * InvByte,
      static_cast<float>(color.green) * InvByte,
      static_cast<float>(color.blue) * InvByte,
      static_cast<float>(color.alpha) * InvByte,
  };
}

Rml::ColumnMajorMatrix4f to_shader_matrix(const Rml::Matrix4f& matrix) {
  if constexpr (std::is_same_v<Rml::Matrix4f, Rml::RowMajorMatrix4f>) {
    return matrix.Transpose();
  } else {
    return matrix;
  }
}

Rml::Rectanglei downsample_scissor(Rml::Rectanglei scissor) {
  scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
  scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
  return scissor;
}

wgpu::ComputeState compile_shader(const std::string_view& wgslSource, const std::string_view& label) {
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

wgpu::StencilFaceState stencil_face(wgpu::CompareFunction compare, wgpu::StencilOperation passOp) {
  return {
      .compare = compare,
      .failOp = wgpu::StencilOperation::Keep,
      .depthFailOp = wgpu::StencilOperation::Keep,
      .passOp = passOp,
  };
}
} // namespace

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

constexpr std::string_view colorMatrixFragmentSource = R"(
struct ColorMatrixUniforms {
    matrix: mat4x4<f32>,
};

@group(0) @binding(1) var s: sampler;
@group(1) @binding(0) var t: texture_2d<f32>;
@group(2) @binding(0) var<uniform> color_matrix: ColorMatrixUniforms;

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let tex_color = textureSample(t, s, uv);
    let transformed_color = (color_matrix.matrix * tex_color).rgb;
    return vec4<f32>(transformed_color, tex_color.a);
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

inline constexpr uint64_t UniformBufferSize = 1048576; // 1mb

Rml::CompiledGeometryHandle WebGPURenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                   Rml::Span<const int> indices) {
  auto* geometryData = new ShaderGeometryData();
  const wgpu::BufferDescriptor vtxBufferDesc{
      .label = "RmlUi Vertex Buffer",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex,
      .size = AURORA_ALIGN(vertices.size() * sizeof(Rml::Vertex), 4),
  };
  geometryData->m_vertexBuffer = webgpu::g_device.CreateBuffer(&vtxBufferDesc);
  webgpu::g_queue.WriteBuffer(geometryData->m_vertexBuffer, 0, vertices.data(), sizeof(Rml::Vertex) * vertices.size());

  const wgpu::BufferDescriptor idxBufferDesc{
      .label = "RmlUi Index Buffer",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index,
      .size = AURORA_ALIGN(indices.size() * sizeof(int), 4),
  };
  geometryData->m_indexBuffer = webgpu::g_device.CreateBuffer(&idxBufferDesc);
  webgpu::g_queue.WriteBuffer(geometryData->m_indexBuffer, 0, indices.data(), sizeof(int) * indices.size());

  return reinterpret_cast<Rml::CompiledGeometryHandle>(geometryData);
}

void WebGPURenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                           Rml::TextureHandle texture) {
  DrawGeometry(geometry, translation, texture,
               m_pipelines[static_cast<size_t>(m_clipMaskEnabled ? PipelineType::Masked : PipelineType::Normal)]);
}

void WebGPURenderInterface::DrawGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                         Rml::TextureHandle texture, const wgpu::RenderPipeline& pipeline) {
  EnsureActiveLayerPass("RmlUi resumed geometry layer pass");
  if (m_pass == nullptr) {
    return;
  }

  SetupRenderState(translation);

  auto* geometryData = reinterpret_cast<ShaderGeometryData*>(geometry);
  auto* textureData = reinterpret_cast<ShaderTextureData*>(texture != 0 ? texture : m_nullTexture);

  m_pass.SetVertexBuffer(0, geometryData->m_vertexBuffer, 0, geometryData->m_vertexBuffer.GetSize());
  m_pass.SetIndexBuffer(geometryData->m_indexBuffer, wgpu::IndexFormat::Uint32, 0,
                        geometryData->m_indexBuffer.GetSize());
  m_pass.SetPipeline(pipeline);
  m_pass.SetBindGroup(0, m_commonBindGroup, 1, &m_uniformCurrentOffset);
  m_pass.SetBindGroup(1, textureData->m_bindGroup);
  m_pass.DrawIndexed(geometryData->m_indexBuffer.GetSize() / sizeof(int));

  m_uniformCurrentOffset += AURORA_ALIGN(sizeof(UniformBlock), 256);
}

void WebGPURenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  delete reinterpret_cast<ShaderGeometryData*>(geometry);
}

Rml::TextureHandle WebGPURenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
  // load texels from image source
  const auto image = get_image(source);
  if (image.size == 0) {
    Log.error("Failed to load texture! Path: {}", source);
    return 0;
  }

  dimensions.x = static_cast<int>(image.width);
  dimensions.y = static_cast<int>(image.height);
  return GenerateTexture({image.data.get(), image.size}, dimensions);
}

Rml::TextureHandle WebGPURenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                          Rml::Vector2i source_dimensions) {
  auto* texData = new ShaderTextureData();
  const wgpu::Extent3D size{
      .width = static_cast<uint32_t>(source_dimensions.x),
      .height = static_cast<uint32_t>(source_dimensions.y),
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDesc{
      .label = "RmlUi Texture",
      .usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = wgpu::TextureFormat::RGBA8Unorm,
  };
  texData->m_texture = webgpu::g_device.CreateTexture(&textureDesc);
  texData->m_textureView = texData->m_texture.CreateView(nullptr);

  constexpr uint32_t BytesPerPixel = 4;
  const wgpu::TexelCopyTextureInfo dst{
      .texture = texData->m_texture,
      .aspect = wgpu::TextureAspect::All,
  };
  const wgpu::TexelCopyBufferLayout layout{
      .offset = 0,
      .bytesPerRow = static_cast<uint32_t>(source_dimensions.x) * BytesPerPixel,
      .rowsPerImage = static_cast<uint32_t>(source_dimensions.y),
  };
  webgpu::g_queue.WriteTexture(&dst, source.data(), source_dimensions.x * BytesPerPixel * source_dimensions.y, &layout,
                               &size);

  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = texData->m_textureView,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDesc{
      .layout = m_imageBindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  texData->m_bindGroup = webgpu::g_device.CreateBindGroup(&bindGroupDesc);

  return reinterpret_cast<Rml::TextureHandle>(texData);
}

void WebGPURenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
  delete reinterpret_cast<ShaderTextureData*>(texture);
}

void WebGPURenderInterface::EnableScissorRegion(bool enable) {
  m_enableScissorRegion = enable;
  ApplyScissorRegion();
}

void WebGPURenderInterface::SetScissorRegion(Rml::Rectanglei region) {
  m_scissorRegion = region;
  ApplyScissorRegion();
}

void WebGPURenderInterface::ApplyScissorRegion() {
  if (m_pass == nullptr) {
    return;
  }

  ApplyScissorRegion(GetActiveScissorRegion());
}

void WebGPURenderInterface::ApplyScissorRegion(Rml::Rectanglei region) {
  if (m_pass == nullptr) {
    return;
  }

  const int maxWidth = static_cast<int>(m_frameSize.width);
  const int maxHeight = static_cast<int>(m_frameSize.height);
  const uint32_t x = static_cast<uint32_t>(std::clamp(region.Left(), 0, maxWidth));
  const uint32_t y = static_cast<uint32_t>(std::clamp(region.Top(), 0, maxHeight));
  const uint32_t width = static_cast<uint32_t>(std::clamp(region.Width(), 0, maxWidth - static_cast<int>(x)));
  const uint32_t height = static_cast<uint32_t>(std::clamp(region.Height(), 0, maxHeight - static_cast<int>(y)));
  m_pass.SetScissorRect(x, y, width, height);
}

Rml::Rectanglei WebGPURenderInterface::GetActiveScissorRegion() const {
  const int maxWidth = m_windowSize.x > 0 ? m_windowSize.x : static_cast<int>(m_frameSize.width);
  const int maxHeight = m_windowSize.y > 0 ? m_windowSize.y : static_cast<int>(m_frameSize.height);
  if (!m_enableScissorRegion || !m_scissorRegion.Valid()) {
    return Rml::Rectanglei::FromSize({std::max(maxWidth, 0), std::max(maxHeight, 0)});
  }

  const int left = std::clamp(m_scissorRegion.Left(), 0, maxWidth);
  const int top = std::clamp(m_scissorRegion.Top(), 0, maxHeight);
  const int right = std::clamp(m_scissorRegion.Right(), left, maxWidth);
  const int bottom = std::clamp(m_scissorRegion.Bottom(), top, maxHeight);
  return Rml::Rectanglei::FromCorners({left, top}, {right, bottom});
}

void WebGPURenderInterface::EnableClipMask(bool enable) {
  m_clipMaskEnabled = enable;
  if (!enable) {
    m_stencilRef = 0;
    if (m_pass != nullptr) {
      m_pass.SetStencilReference(0);
    }
  } else if (m_pass != nullptr) {
    m_pass.SetStencilReference(m_stencilRef);
  }
}

void WebGPURenderInterface::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                                             Rml::Vector2f translation) {
  EnsureActiveLayerPass("RmlUi resumed clip mask layer pass");
  if (m_pass == nullptr) {
    return;
  }

  EnsureClipResetGeometry();

  const Rml::Matrix4f prevMatrix = m_translationMatrix;
  switch (operation) {
  case Rml::ClipMaskOperation::Set:
    m_translationMatrix = Rml::Matrix4f::Identity();
    m_pass.SetStencilReference(0);
    DrawGeometry(m_clipResetGeometry, {}, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    m_translationMatrix = prevMatrix;

    m_stencilRef = 1;
    m_pass.SetStencilReference(m_stencilRef);
    DrawGeometry(geometry, translation, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    break;
  case Rml::ClipMaskOperation::SetInverse:
    m_translationMatrix = Rml::Matrix4f::Identity();
    m_pass.SetStencilReference(1);
    DrawGeometry(m_clipResetGeometry, {}, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    m_translationMatrix = prevMatrix;

    m_stencilRef = 1;
    m_pass.SetStencilReference(0);
    DrawGeometry(geometry, translation, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    break;
  case Rml::ClipMaskOperation::Intersect:
    if (m_stencilRef == std::numeric_limits<uint8_t>::max()) {
      Log.warn("RmlUi clip mask nesting exceeded stencil capacity; further nested clipping may be incorrect");
      break;
    }

    m_pass.SetStencilReference(m_stencilRef);
    DrawGeometry(geometry, translation, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipIntersect)]);
    ++m_stencilRef;
    break;
  }

  m_pass.SetStencilReference(m_stencilRef);
}

void WebGPURenderInterface::SetTransform(const Rml::Matrix4f* transform) {
  if (transform == nullptr) {
    m_translationMatrix = Rml::Matrix4f::Identity();
  } else {
    m_translationMatrix = *transform;
  }
}

void WebGPURenderInterface::EnsureRenderTarget(RenderTarget& target, const char* label, const wgpu::Extent3D& size,
                                               bool multisampled) {
  const bool useMultisampling = multisampled && LayerSampleCount > 1;
  if (target.view && target.size == size && static_cast<bool>(target.multisampleView) == useMultisampling) {
    return;
  }

  target = {};
  target.size = size;
  const wgpu::TextureDescriptor textureDesc{
      .label = label,
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = m_renderTargetFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  target.texture = webgpu::g_device.CreateTexture(&textureDesc);
  target.view = target.texture.CreateView(nullptr);
  target.bindGroup = CreateImageBindGroup(target.view);

  if (useMultisampling) {
    const wgpu::TextureDescriptor multisampleTextureDesc{
        .label = "RmlUi Multisampled Render Target",
        .usage = wgpu::TextureUsage::RenderAttachment,
        .dimension = wgpu::TextureDimension::e2D,
        .size = size,
        .format = m_renderTargetFormat,
        .mipLevelCount = 1,
        .sampleCount = LayerSampleCount,
    };
    target.multisampleTexture = webgpu::g_device.CreateTexture(&multisampleTextureDesc);
    target.multisampleView = target.multisampleTexture.CreateView(nullptr);
  }
}

wgpu::BindGroup WebGPURenderInterface::CreateImageBindGroup(const wgpu::TextureView& view) const {
  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = view,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDesc{
      .layout = m_imageBindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  return webgpu::g_device.CreateBindGroup(&bindGroupDesc);
}

void WebGPURenderInterface::EnsureFrameTargets(const wgpu::Extent3D& size) {
  if (m_layers.empty()) {
    m_layers.resize(1);
  }

  EnsureRenderTarget(m_postprocessTargets[0], "RmlUi Postprocess A", size);
  EnsureRenderTarget(m_postprocessTargets[1], "RmlUi Postprocess B", size);
  EnsureRenderTarget(m_postprocessTargets[2], "RmlUi Postprocess C", size);
  EnsureRenderTarget(m_blendMaskTarget, "RmlUi Blend Mask", size);
}

TexCoordLimits WebGPURenderInterface::GetPostprocessTexCoordLimits() const {
  return GetPostprocessTexCoordLimits(GetActiveScissorRegion());
}

TexCoordLimits WebGPURenderInterface::GetPostprocessTexCoordLimits(Rml::Rectanglei region) const {
  const float viewportWidth = std::max(m_viewport.width, 1.f);
  const float viewportHeight = std::max(m_viewport.height, 1.f);
  const int maxWidth = m_windowSize.x > 0 ? m_windowSize.x : static_cast<int>(viewportWidth);
  const int maxHeight = m_windowSize.y > 0 ? m_windowSize.y : static_cast<int>(viewportHeight);

  const int left = std::clamp(region.Left(), 0, maxWidth);
  const int top = std::clamp(region.Top(), 0, maxHeight);
  const int right = std::clamp(region.Right(), left, maxWidth);
  const int bottom = std::clamp(region.Bottom(), top, maxHeight);

  if (right <= left || bottom <= top) {
    const Rml::Vector2f empty = {0.5f / viewportWidth, 0.5f / viewportHeight};
    return {
        .min = empty,
        .max = empty,
    };
  }

  const Rml::Vector2f viewportOrigin{m_viewport.left, m_viewport.top};
  const Rml::Vector2f viewportSize{viewportWidth, viewportHeight};
  const Rml::Vector2f minLimit{0.5f / viewportWidth, 0.5f / viewportHeight};
  const Rml::Vector2f maxLimit{1.f - minLimit.x, 1.f - minLimit.y};
  const auto clamp_limits = [minLimit, maxLimit](Rml::Vector2f value) {
    return Rml::Vector2f{
        std::clamp(value.x, minLimit.x, maxLimit.x),
        std::clamp(value.y, minLimit.y, maxLimit.y),
    };
  };

  const Rml::Vector2f min =
      (Rml::Vector2f(static_cast<float>(left), static_cast<float>(top)) - viewportOrigin + Rml::Vector2f(0.5f)) /
      viewportSize;
  const Rml::Vector2f max =
      (Rml::Vector2f(static_cast<float>(right), static_cast<float>(bottom)) - viewportOrigin - Rml::Vector2f(0.5f)) /
      viewportSize;
  return {
      .min = clamp_limits(min),
      .max = clamp_limits(max),
  };
}

void WebGPURenderInterface::ApplyViewport() {
  if (m_pass != nullptr) {
    m_pass.SetViewport(m_viewport.left, m_viewport.top, m_viewport.width, m_viewport.height, m_viewport.znear,
                       m_viewport.zfar);
  }
}

void WebGPURenderInterface::ApplyFullFrameScissor() {
  if (m_pass != nullptr) {
    m_pass.SetScissorRect(0, 0, m_frameSize.width, m_frameSize.height);
  }
}

void WebGPURenderInterface::BeginRenderTargetPass(const wgpu::TextureView& view, wgpu::LoadOp loadOp, const char* label,
                                                  bool clearStencil) {
  EndActivePass();
  (void)clearStencil;

  const std::array attachments{
      wgpu::RenderPassColorAttachment{
          .view = view,
          .loadOp = loadOp,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {0.f, 0.f, 0.f, 0.f},
      },
  };
  const wgpu::RenderPassDescriptor renderPassDesc{
      .label = label,
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
  };
  m_pass = m_encoder.BeginRenderPass(&renderPassDesc);
  ApplyViewport();
  ApplyScissorRegion();
  m_pass.SetStencilReference(m_stencilRef);
}

void WebGPURenderInterface::BeginLayerPass(Rml::LayerHandle layer, wgpu::LoadOp loadOp, const char* label,
                                           bool clearStencil, bool resolveMultisampled) {
  m_activeLayer = layer;
  EndActivePass();

  const RenderTarget& target = m_layers[layer];
  const bool multisampled = LayerSampleCount > 1 && static_cast<bool>(target.multisampleView);
  const std::array attachments{
      wgpu::RenderPassColorAttachment{
          .view = multisampled ? target.multisampleView : target.view,
          .resolveTarget = multisampled && resolveMultisampled ? target.view : nullptr,
          .loadOp = loadOp,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {0.f, 0.f, 0.f, 0.f},
      },
  };
  const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment{
      .view = GetClipMaskStencilView(m_frameSize),
      .stencilLoadOp = clearStencil ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
      .stencilStoreOp = wgpu::StoreOp::Store,
      .stencilClearValue = 0,
  };
  const wgpu::RenderPassDescriptor renderPassDesc{
      .label = label,
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
      .depthStencilAttachment = &depthStencilAttachment,
  };
  m_pass = m_encoder.BeginRenderPass(&renderPassDesc);
  ApplyViewport();
  ApplyScissorRegion();
  m_pass.SetStencilReference(m_stencilRef);
}

void WebGPURenderInterface::EnsureFrameRenderingStarted() {
  if (m_frameRenderingStarted || m_encoder == nullptr || m_layers.empty() || !m_layers[0].view) {
    return;
  }

  m_frameRenderingStarted = true;
  const wgpu::BindGroup seedBindGroup = CreateImageBindGroup(m_frameSeedView);

  if (m_layers[0].multisampleView) {
    BeginLayerPass(0, wgpu::LoadOp::Clear, "RmlUi base layer seed pass", true);
    ApplyFullFrameScissor();
    DrawFullscreenTexture(seedBindGroup, m_layerOpaqueBlitPipeline);
    BeginLayerPass(0, wgpu::LoadOp::Load, "RmlUi base layer pass");
  } else {
    BeginRenderTargetPass(m_layers[0].view, wgpu::LoadOp::Clear, "RmlUi game frame copy pass");
    ApplyFullFrameScissor();
    DrawFullscreenTexture(seedBindGroup, m_opaqueBlitPipeline);
    BeginLayerPass(0, wgpu::LoadOp::Load, "RmlUi base layer pass", true);
  }
}

void WebGPURenderInterface::EnsureActiveLayerPass(const char* label) {
  EnsureFrameRenderingStarted();
  if (m_pass != nullptr || m_encoder == nullptr || m_activeLayer >= m_layers.size() || !m_layers[m_activeLayer].view) {
    return;
  }

  BeginLayerPass(m_activeLayer, wgpu::LoadOp::Load, label);
}

void WebGPURenderInterface::EndActivePass() {
  if (m_pass != nullptr) {
    m_pass.End();
    m_pass = nullptr;
  }
}

void WebGPURenderInterface::DrawFullscreenTexture(const wgpu::BindGroup& bindGroup,
                                                  const wgpu::RenderPipeline& pipeline,
                                                  const wgpu::BindGroup* extraBindGroup, uint32_t extraDynamicOffset,
                                                  bool extraBindGroupHasDynamicOffset) {
  constexpr uint32_t uniformOffset = 0;
  m_pass.SetPipeline(pipeline);
  m_pass.SetBindGroup(0, m_commonBindGroup, 1, &uniformOffset);
  m_pass.SetBindGroup(1, bindGroup);
  if (extraBindGroup != nullptr) {
    if (extraBindGroupHasDynamicOffset) {
      m_pass.SetBindGroup(2, *extraBindGroup, 1, &extraDynamicOffset);
    } else {
      m_pass.SetBindGroup(2, *extraBindGroup);
    }
  }
  m_pass.Draw(3);
}

void WebGPURenderInterface::CompositeToTarget(const wgpu::BindGroup& bindGroup, const wgpu::TextureView& view,
                                              wgpu::LoadOp loadOp, const wgpu::RenderPipeline& pipeline,
                                              const char* label, const wgpu::BindGroup* extraBindGroup,
                                              uint32_t extraDynamicOffset, bool extraBindGroupHasDynamicOffset) {
  BeginRenderTargetPass(view, loadOp, label);
  DrawFullscreenTexture(bindGroup, pipeline, extraBindGroup, extraDynamicOffset, extraBindGroupHasDynamicOffset);
}

void WebGPURenderInterface::RenderBlur(float sigma, const RenderTarget& sourceDestination, const RenderTarget& temp) {
  sigma = std::max(sigma, 0.f);
  if (sigma < 0.5f) {
    return;
  }

  const Rml::Rectanglei originalScissor = GetActiveScissorRegion();
  if (originalScissor.Width() <= 0 || originalScissor.Height() <= 0) {
    return;
  }

  auto write_blur_uniform = [&](Rml::Vector2f texelOffset, Rml::Rectanglei texCoordRegion, float radius,
                                Rml::Vector4f weights) {
    const uint32_t offset = m_blurUniformCurrentOffset;
    m_blurUniformCurrentOffset += AURORA_ALIGN(sizeof(BlurUniformBlock), 256);
    const TexCoordLimits texCoordLimits = GetPostprocessTexCoordLimits(texCoordRegion);
    const BlurUniformBlock uniform{
        .texelOffset = texelOffset,
        .radius = radius,
        .padding = 0.f,
        .texCoordMin = texCoordLimits.min,
        .texCoordMax = texCoordLimits.max,
        .weights = weights,
    };
    webgpu::g_queue.WriteBuffer(m_blurUniformBuffer, offset, &uniform, sizeof(uniform));
    return offset;
  };
  auto write_region_blit_uniform = [&](Rml::Rectanglei texCoordRegion, Rml::Vector4f weights) {
    const float viewportWidth = std::max(m_viewport.width, 1.f);
    const float viewportHeight = std::max(m_viewport.height, 1.f);
    const int maxWidth = m_windowSize.x > 0 ? m_windowSize.x : static_cast<int>(viewportWidth);
    const int maxHeight = m_windowSize.y > 0 ? m_windowSize.y : static_cast<int>(viewportHeight);
    const int left = std::clamp(texCoordRegion.Left(), 0, maxWidth);
    const int top = std::clamp(texCoordRegion.Top(), 0, maxHeight);
    const int right = std::clamp(texCoordRegion.Right(), left, maxWidth);
    const int bottom = std::clamp(texCoordRegion.Bottom(), top, maxHeight);
    const Rml::Vector2f viewportOrigin{m_viewport.left, m_viewport.top};
    const Rml::Vector2f viewportSize{viewportWidth, viewportHeight};
    const uint32_t offset = m_blurUniformCurrentOffset;
    m_blurUniformCurrentOffset += AURORA_ALIGN(sizeof(BlurUniformBlock), 256);
    const BlurUniformBlock uniform{
        .texelOffset = {},
        .radius = 0.f,
        .padding = 0.f,
        .texCoordMin =
            (Rml::Vector2f(static_cast<float>(left), static_cast<float>(top)) - viewportOrigin) / viewportSize,
        .texCoordMax =
            (Rml::Vector2f(static_cast<float>(right), static_cast<float>(bottom)) - viewportOrigin) / viewportSize,
        .weights = weights,
    };
    webgpu::g_queue.WriteBuffer(m_blurUniformBuffer, offset, &uniform, sizeof(uniform));
    return offset;
  };

  int passLevel = 0;
  sigma_to_params(sigma, passLevel, sigma);
  if (sigma == 0.f) {
    return;
  }

  Rml::Rectanglei scissor = originalScissor;
  const auto weights = blur_weights(sigma, MaxBlurRadius);
  constexpr auto radius = static_cast<float>(MaxBlurRadius);

  for (int i = 0; i < passLevel; ++i) {
    scissor = downsample_scissor(scissor);
    const bool fromSource = (i % 2) == 0;
    const RenderTarget& source = fromSource ? sourceDestination : temp;
    const RenderTarget& destination = fromSource ? temp : sourceDestination;

    BeginRenderTargetPass(destination.view, wgpu::LoadOp::Clear, "RmlUi blur downsample pass");
    m_pass.SetViewport(m_viewport.left, m_viewport.top, std::max(m_viewport.width * 0.5f, 1.f),
                       std::max(m_viewport.height * 0.5f, 1.f), m_viewport.znear, m_viewport.zfar);
    ApplyScissorRegion(scissor);
    DrawFullscreenTexture(source.bindGroup, m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)]);
  }

  if ((passLevel % 2) == 0) {
    BeginRenderTargetPass(temp.view, wgpu::LoadOp::Clear, "RmlUi blur transfer pass");
    ApplyViewport();
    ApplyScissorRegion(scissor);
    DrawFullscreenTexture(sourceDestination.bindGroup, m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)]);
  }

  const uint32_t verticalOffset =
      write_blur_uniform({0.f, 1.f / std::max(m_viewport.height, 1.f)}, scissor, radius, weights);
  BeginRenderTargetPass(sourceDestination.view, wgpu::LoadOp::Clear, "RmlUi vertical blur pass");
  ApplyScissorRegion(scissor);
  DrawFullscreenTexture(temp.bindGroup, m_blurPipeline, &m_blurBindGroup, verticalOffset);

  const uint32_t horizontalOffset =
      write_blur_uniform({1.f / std::max(m_viewport.width, 1.f), 0.f}, scissor, radius, weights);
  BeginRenderTargetPass(temp.view, wgpu::LoadOp::Clear, "RmlUi horizontal blur pass");
  ApplyScissorRegion(scissor);
  DrawFullscreenTexture(sourceDestination.bindGroup, m_blurPipeline, &m_blurBindGroup, horizontalOffset);

  const uint32_t upscaleOffset = write_region_blit_uniform(scissor, weights);
  BeginRenderTargetPass(sourceDestination.view, wgpu::LoadOp::Clear, "RmlUi blur upscale pass");
  m_pass.SetViewport(static_cast<float>(originalScissor.Left()), static_cast<float>(originalScissor.Top()),
                     static_cast<float>(std::max(originalScissor.Width(), 1)),
                     static_cast<float>(std::max(originalScissor.Height(), 1)), 0.f, 1.f);
  ApplyScissorRegion(originalScissor);
  DrawFullscreenTexture(temp.bindGroup, m_regionBlitPipeline, &m_blurBindGroup, upscaleOffset);

  const Rml::Vector2i targetMin = scissor.p0 * (1 << passLevel);
  const Rml::Vector2i targetMax = scissor.p1 * (1 << passLevel);
  const Rml::Rectanglei targetRegion = Rml::Rectanglei::FromCorners(targetMin, targetMax);
  if (targetRegion.p0 != originalScissor.p0 || targetRegion.p1 != originalScissor.p1) {
    BeginRenderTargetPass(sourceDestination.view, wgpu::LoadOp::Load, "RmlUi blur power-of-two upscale pass");
    m_pass.SetViewport(static_cast<float>(targetRegion.Left()), static_cast<float>(targetRegion.Top()),
                       static_cast<float>(std::max(targetRegion.Width(), 1)),
                       static_cast<float>(std::max(targetRegion.Height(), 1)), 0.f, 1.f);
    ApplyScissorRegion(targetRegion);
    DrawFullscreenTexture(temp.bindGroup, m_regionBlitPipeline, &m_blurBindGroup, upscaleOffset);
  }
}

void WebGPURenderInterface::RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filters) {
  constexpr size_t sourceIndex = 0;
  constexpr size_t shadowIndex = 1;
  constexpr size_t tempIndex = 2;

  for (Rml::CompiledFilterHandle filterHandle : filters) {
    const auto* filter = reinterpret_cast<const CompiledFilter*>(filterHandle);
    if (filter == nullptr) {
      continue;
    }

    switch (filter->type) {
    case FilterType::Opacity: {
      const wgpu::Color blendColor{filter->opacity, filter->opacity, filter->opacity, filter->opacity};
      BeginRenderTargetPass(m_postprocessTargets[shadowIndex].view, wgpu::LoadOp::Clear, "RmlUi opacity pass");
      m_pass.SetBlendConstant(&blendColor);
      DrawFullscreenTexture(m_postprocessTargets[sourceIndex].bindGroup, m_opacityPipeline);
      CompositeToTarget(m_postprocessTargets[shadowIndex].bindGroup, m_postprocessTargets[sourceIndex].view,
                        wgpu::LoadOp::Clear, m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)],
                        "RmlUi opacity result copy pass");
      break;
    }
    case FilterType::Blur:
      RenderBlur(filter->sigma, m_postprocessTargets[sourceIndex], m_postprocessTargets[shadowIndex]);
      break;
    case FilterType::DropShadow: {
      TexCoordLimits texCoordLimits = GetPostprocessTexCoordLimits();
      if (filter->offset.x > 0.f)
        texCoordLimits.min.x += 1.f / std::max(m_viewport.width, 1.f);
      else if (filter->offset.x < 0.f)
        texCoordLimits.max.x -= 1.f / std::max(m_viewport.width, 1.f);

      const auto to_float_color = [](Rml::ColourbPremultiplied color) {
        constexpr float InvByte = 1.f / 255.f;
        return Rml::Vector4f(static_cast<float>(color.red) * InvByte, static_cast<float>(color.green) * InvByte,
                             static_cast<float>(color.blue) * InvByte, static_cast<float>(color.alpha) * InvByte);
      };
      const DropShadowUniformBlock dropShadowUniform{
          .color = to_float_color(filter->color),
          .uvOffset =
              {
                  filter->offset.x / std::max(m_viewport.width, 1.f),
                  filter->offset.y / std::max(m_viewport.height, 1.f),
              },
          .texCoordMin = texCoordLimits.min,
          .texCoordMax = texCoordLimits.max,
      };
      const uint32_t dropShadowOffset = m_dropShadowUniformCurrentOffset;
      m_dropShadowUniformCurrentOffset += AURORA_ALIGN(sizeof(DropShadowUniformBlock), 256);
      webgpu::g_queue.WriteBuffer(m_dropShadowUniformBuffer, dropShadowOffset, &dropShadowUniform,
                                  sizeof(dropShadowUniform));
      CompositeToTarget(m_postprocessTargets[sourceIndex].bindGroup, m_postprocessTargets[shadowIndex].view,
                        wgpu::LoadOp::Clear, m_dropShadowPipeline, "RmlUi drop shadow pass", &m_dropShadowBindGroup,
                        dropShadowOffset);

      RenderBlur(filter->sigma, m_postprocessTargets[shadowIndex], m_postprocessTargets[tempIndex]);

      CompositeToTarget(m_postprocessTargets[sourceIndex].bindGroup, m_postprocessTargets[shadowIndex].view,
                        wgpu::LoadOp::Load, m_blitPipelines[static_cast<size_t>(BlitPipelineType::Blend)],
                        "RmlUi drop shadow source composite pass");
      CompositeToTarget(m_postprocessTargets[shadowIndex].bindGroup, m_postprocessTargets[sourceIndex].view,
                        wgpu::LoadOp::Clear, m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)],
                        "RmlUi drop shadow result copy pass");
      break;
    }
    case FilterType::ColorMatrix: {
      const ColorMatrixUniformBlock colorMatrixUniform{
          .matrix = to_shader_matrix(filter->colorMatrix),
      };
      const uint32_t colorMatrixOffset = m_shaderUniformCurrentOffset;
      m_shaderUniformCurrentOffset += AURORA_ALIGN(sizeof(ColorMatrixUniformBlock), 256);
      webgpu::g_queue.WriteBuffer(m_shaderUniformBuffer, colorMatrixOffset, &colorMatrixUniform,
                                  sizeof(colorMatrixUniform));

      CompositeToTarget(m_postprocessTargets[sourceIndex].bindGroup, m_postprocessTargets[shadowIndex].view,
                        wgpu::LoadOp::Clear, m_colorMatrixPipeline, "RmlUi color matrix pass", &m_shaderBindGroup,
                        colorMatrixOffset);
      CompositeToTarget(m_postprocessTargets[shadowIndex].bindGroup, m_postprocessTargets[sourceIndex].view,
                        wgpu::LoadOp::Clear, m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)],
                        "RmlUi color matrix result copy pass");
      break;
    }
    case FilterType::MaskImage: {
      CompositeToTarget(m_postprocessTargets[sourceIndex].bindGroup, m_postprocessTargets[shadowIndex].view,
                        wgpu::LoadOp::Clear, m_maskImagePipeline, "RmlUi mask image pass", &m_blendMaskTarget.bindGroup,
                        0, false);
      CompositeToTarget(m_postprocessTargets[shadowIndex].bindGroup, m_postprocessTargets[sourceIndex].view,
                        wgpu::LoadOp::Clear, m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)],
                        "RmlUi mask image result copy pass");
      break;
    }
    }
  }

  EndActivePass();
}

void WebGPURenderInterface::BeginFrame(const wgpu::CommandEncoder& encoder, const webgpu::TextureWithSampler& target,
                                       const webgpu::TextureWithSampler& seedTarget) {
  m_encoder = encoder;
  m_frameSeedView = seedTarget.view;
  m_frameSize = target.size;
  m_viewport = {
      .left = 0.f,
      .top = 0.f,
      .width = static_cast<float>(target.size.width),
      .height = static_cast<float>(target.size.height),
      .znear = 0.f,
      .zfar = 1.f,
  };
  m_nextLayer = 1;
  m_layerStack = {0};
  m_activeLayer = 0;
  m_frameRenderingStarted = false;

  NewFrame();
  EnsureFrameTargets(target.size);
  wgpu::Texture multisampleTexture;
  wgpu::TextureView multisampleView;
  if constexpr (LayerSampleCount > 1) {
    if (m_layers[0].multisampleView && m_layers[0].size == target.size) {
      multisampleTexture = m_layers[0].multisampleTexture;
      multisampleView = m_layers[0].multisampleView;
    } else {
      const wgpu::TextureDescriptor multisampleTextureDesc{
          .label = "RmlUi Multisampled Base Layer",
          .usage = wgpu::TextureUsage::RenderAttachment,
          .dimension = wgpu::TextureDimension::e2D,
          .size = target.size,
          .format = m_renderTargetFormat,
          .mipLevelCount = 1,
          .sampleCount = LayerSampleCount,
      };
      multisampleTexture = webgpu::g_device.CreateTexture(&multisampleTextureDesc);
      multisampleView = multisampleTexture.CreateView(nullptr);
    }
  }

  m_layers[0] = {
      .texture = target.texture,
      .view = target.view,
      .bindGroup = CreateImageBindGroup(target.view),
      .multisampleTexture = multisampleTexture,
      .multisampleView = multisampleView,
      .size = target.size,
  };
}

bool WebGPURenderInterface::EndFrame() {
  const bool rendered = m_frameRenderingStarted;
  EndActivePass();

  m_layerStack.clear();
  if (!m_layers.empty()) {
    m_layers[0] = {
        .multisampleTexture = m_layers[0].multisampleTexture,
        .multisampleView = m_layers[0].multisampleView,
        .size = m_layers[0].size,
    };
  }
  m_encoder = nullptr;
  m_frameSeedView = {};
  m_frameRenderingStarted = false;
  return rendered;
}

Rml::LayerHandle WebGPURenderInterface::PushLayer() {
  EnsureFrameRenderingStarted();

  const Rml::LayerHandle layer = m_nextLayer++;
  if (static_cast<size_t>(layer) >= m_layers.size()) {
    m_layers.resize(static_cast<size_t>(layer) + 1);
  }

  EnsureRenderTarget(m_layers[static_cast<size_t>(layer)], "RmlUi Layer", m_frameSize, true);
  m_layerStack.push_back(layer);
  BeginLayerPass(layer, wgpu::LoadOp::Clear, "RmlUi pushed layer pass");
  return layer;
}

void WebGPURenderInterface::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                                            Rml::BlendMode blendMode,
                                            Rml::Span<const Rml::CompiledFilterHandle> filters) {
  if (source >= m_layers.size() || destination >= m_layers.size()) {
    Log.warn("RmlUi requested composite with invalid layer handles: {} -> {}", source, destination);
    return;
  }

  EnsureFrameRenderingStarted();
  CompositeToTarget(m_layers[source].bindGroup, m_postprocessTargets[0].view, wgpu::LoadOp::Clear,
                    m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)], "RmlUi layer copy pass");
  const Rml::LayerHandle topLayer = m_layerStack.empty() ? 0 : m_layerStack.back();
  RenderFilters(filters);

  const bool replace = blendMode == Rml::BlendMode::Replace;
  const BlitPipelineType pipelineType =
      replace ? (m_clipMaskEnabled ? BlitPipelineType::ReplaceMasked : BlitPipelineType::Replace)
              : (m_clipMaskEnabled ? BlitPipelineType::BlendMasked : BlitPipelineType::Blend);
  BeginLayerPass(destination, wgpu::LoadOp::Load, "RmlUi layer composite pass");
  DrawFullscreenTexture(m_postprocessTargets[0].bindGroup, m_layerBlitPipelines[static_cast<size_t>(pipelineType)]);

  if (destination != topLayer) {
    EndActivePass();
    m_activeLayer = topLayer;
  }
}

void WebGPURenderInterface::PopLayer() {
  if (m_layerStack.size() <= 1) {
    Log.warn("RmlUi requested PopLayer with no pushed layer");
    return;
  }

  m_layerStack.pop_back();
  EndActivePass();
  m_activeLayer = m_layerStack.back();
}

Rml::TextureHandle WebGPURenderInterface::SaveLayerAsTexture() {
  if (m_encoder == nullptr) {
    Log.warn("RmlUi requested SaveLayerAsTexture outside a frame");
    return 0;
  }

  if (!m_scissorRegion.Valid()) {
    Log.warn("RmlUi requested SaveLayerAsTexture without a valid scissor region");
    return 0;
  }

  EnsureFrameRenderingStarted();

  const Rml::LayerHandle layer = m_layerStack.empty() ? m_activeLayer : m_layerStack.back();
  if (layer >= m_layers.size() || !m_layers[layer].texture) {
    Log.warn("RmlUi requested SaveLayerAsTexture with invalid layer handle {}", layer);
    return 0;
  }

  const int left = std::clamp(m_scissorRegion.Left(), 0, static_cast<int>(m_frameSize.width));
  const int top = std::clamp(m_scissorRegion.Top(), 0, static_cast<int>(m_frameSize.height));
  const int right = std::clamp(m_scissorRegion.Right(), left, static_cast<int>(m_frameSize.width));
  const int bottom = std::clamp(m_scissorRegion.Bottom(), top, static_cast<int>(m_frameSize.height));
  if (right <= left || bottom <= top) {
    return 0;
  }

  auto* texData = new ShaderTextureData();
  const wgpu::Extent3D textureSize{
      .width = static_cast<uint32_t>(right - left),
      .height = static_cast<uint32_t>(bottom - top),
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDesc{
      .label = "RmlUi Saved Layer Texture",
      .usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding,
      .dimension = wgpu::TextureDimension::e2D,
      .size = textureSize,
      .format = m_renderTargetFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  texData->m_texture = webgpu::g_device.CreateTexture(&textureDesc);
  texData->m_textureView = texData->m_texture.CreateView(nullptr);

  EndActivePass();

  const wgpu::TexelCopyTextureInfo src{
      .texture = m_layers[layer].texture,
      .origin =
          wgpu::Origin3D{
              .x = static_cast<uint32_t>(left),
              .y = static_cast<uint32_t>(top),
          },
      .aspect = wgpu::TextureAspect::All,
  };
  const wgpu::TexelCopyTextureInfo dst{
      .texture = texData->m_texture,
      .aspect = wgpu::TextureAspect::All,
  };
  m_encoder.CopyTextureToTexture(&src, &dst, &textureSize);

  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = texData->m_textureView,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDesc{
      .layout = m_imageBindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  texData->m_bindGroup = webgpu::g_device.CreateBindGroup(&bindGroupDesc);

  BeginLayerPass(layer, wgpu::LoadOp::Load, "RmlUi saved layer restore pass");
  return reinterpret_cast<Rml::TextureHandle>(texData);
}

Rml::CompiledFilterHandle WebGPURenderInterface::SaveLayerAsMaskImage() {
  if (m_encoder == nullptr) {
    Log.warn("RmlUi requested SaveLayerAsMaskImage outside a frame");
    return {};
  }

  const Rml::LayerHandle layer = m_layerStack.empty() ? m_activeLayer : m_layerStack.back();
  if (layer >= m_layers.size() || !m_layers[layer].texture) {
    Log.warn("RmlUi requested SaveLayerAsMaskImage with invalid layer handle {}", layer);
    return {};
  }

  EnsureFrameRenderingStarted();

  CompositeToTarget(m_layers[layer].bindGroup, m_postprocessTargets[0].view, wgpu::LoadOp::Clear,
                    m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)], "RmlUi mask source copy pass");
  CompositeToTarget(m_postprocessTargets[0].bindGroup, m_blendMaskTarget.view, wgpu::LoadOp::Clear,
                    m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)], "RmlUi mask image save pass");

  BeginLayerPass(layer, wgpu::LoadOp::Load, "RmlUi mask image restore pass");

  auto* filter = new CompiledFilter{
      .type = FilterType::MaskImage,
  };
  return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
}

Rml::CompiledFilterHandle WebGPURenderInterface::CompileFilter(const Rml::String& name,
                                                               const Rml::Dictionary& parameters) {
  if (name == "opacity") {
    auto* filter = new CompiledFilter{
        .type = FilterType::Opacity,
        .opacity = Rml::Get(parameters, "value", 1.0f),
    };
    return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
  }

  if (name == "blur") {
    auto* filter = new CompiledFilter{
        .type = FilterType::Blur,
        .sigma = Rml::Get(parameters, "sigma", 1.0f),
    };
    return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
  }

  if (name == "drop-shadow") {
    auto* filter = new CompiledFilter{
        .type = FilterType::DropShadow,
        .sigma = Rml::Get(parameters, "sigma", 0.f),
        .offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f)),
        .color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied(),
    };
    return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
  }

  CompiledFilter colorMatrixFilter = {
      .type = FilterType::ColorMatrix,
  };
  if (name == "brightness") {
    const float value = Rml::Get(parameters, "value", 1.0f);
    colorMatrixFilter.colorMatrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
  } else if (name == "contrast") {
    const float value = Rml::Get(parameters, "value", 1.0f);
    const float grayness = 0.5f - 0.5f * value;
    colorMatrixFilter.colorMatrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
    colorMatrixFilter.colorMatrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
  } else if (name == "invert") {
    const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
    const float inverted = 1.f - 2.f * value;
    colorMatrixFilter.colorMatrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
    colorMatrixFilter.colorMatrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
  } else if (name == "grayscale") {
    const float value = Rml::Get(parameters, "value", 1.0f);
    const float revValue = 1.f - value;
    const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
    colorMatrixFilter.colorMatrix =
        Rml::Matrix4f::FromRows({gray.x + revValue, gray.y, gray.z, 0.f}, {gray.x, gray.y + revValue, gray.z, 0.f},
                                {gray.x, gray.y, gray.z + revValue, 0.f}, {0.f, 0.f, 0.f, 1.f});
  } else if (name == "sepia") {
    const float value = Rml::Get(parameters, "value", 1.0f);
    const float revValue = 1.f - value;
    const Rml::Vector3f rMix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
    const Rml::Vector3f gMix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
    const Rml::Vector3f bMix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
    colorMatrixFilter.colorMatrix =
        Rml::Matrix4f::FromRows({rMix.x + revValue, rMix.y, rMix.z, 0.f}, {gMix.x, gMix.y + revValue, gMix.z, 0.f},
                                {bMix.x, bMix.y, bMix.z + revValue, 0.f}, {0.f, 0.f, 0.f, 1.f});
  } else if (name == "hue-rotate") {
    const float value = Rml::Get(parameters, "value", 1.0f);
    const float s = Rml::Math::Sin(value);
    const float c = Rml::Math::Cos(value);
    colorMatrixFilter.colorMatrix = Rml::Matrix4f::FromRows(
        {0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0.f},
        {0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0.f},
        {0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0.f},
        {0.f, 0.f, 0.f, 1.f});
  } else if (name == "saturate") {
    const float value = Rml::Get(parameters, "value", 1.0f);
    colorMatrixFilter.colorMatrix = Rml::Matrix4f::FromRows(
        {0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0.f},
        {0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0.f},
        {0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0.f}, {0.f, 0.f, 0.f, 1.f});
  } else {
    Log.warn("Unsupported RmlUi filter '{}'", name);
    return {};
  }
  return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(colorMatrixFilter)));
}

void WebGPURenderInterface::ReleaseFilter(Rml::CompiledFilterHandle filter) {
  delete reinterpret_cast<CompiledFilter*>(filter);
}

Rml::CompiledShaderHandle WebGPURenderInterface::CompileShader(const Rml::String& name,
                                                               const Rml::Dictionary& parameters) {
  const bool supportedGradient = name == "linear-gradient" || name == "radial-gradient" || name == "conic-gradient";
  if (!supportedGradient) {
    Log.warn("Unsupported RmlUi shader '{}'", name);
    return {};
  }

  const auto it = parameters.find("color_stop_list");
  if (it == parameters.end() || it->second.GetType() != Rml::Variant::COLORSTOPLIST) {
    Log.warn("RmlUi shader '{}' missing color stop list", name);
    return {};
  }

  const Rml::ColorStopList& colorStopList = it->second.GetReference<Rml::ColorStopList>();
  const size_t numStops = std::min(colorStopList.size(), MaxGradientStops);
  if (numStops == 0) {
    return {};
  }

  auto* shader = new CompiledShaderData();
  const bool repeating = Rml::Get(parameters, "repeating", false);

  constexpr int32_t Linear = 0;
  constexpr int32_t Radial = 1;
  constexpr int32_t Conic = 2;
  constexpr int32_t RepeatingOffset = 3;

  if (name == "linear-gradient") {
    shader->gradient.function = Linear + (repeating ? RepeatingOffset : 0);
    shader->gradient.p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
    shader->gradient.v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - shader->gradient.p;
  } else if (name == "radial-gradient") {
    shader->gradient.function = Radial + (repeating ? RepeatingOffset : 0);
    shader->gradient.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
    shader->gradient.v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
  } else if (name == "conic-gradient") {
    shader->gradient.function = Conic + (repeating ? RepeatingOffset : 0);
    shader->gradient.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
    const float angle = Rml::Get(parameters, "angle", 0.f);
    shader->gradient.v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
  } else {
    Log.warn("Unsupported RmlUi shader '{}'", name);
    delete shader;
    return {};
  }

  shader->gradient.numStops = static_cast<int32_t>(numStops);

  for (size_t i = 0; i < numStops; ++i) {
    const Rml::ColorStop& stop = colorStopList[i];
    shader->gradient.stopColors[i] = to_colorf(stop.color);
    shader->gradient.stopPositions[i / 4][i % 4] = stop.position.number;
  }

  return reinterpret_cast<Rml::CompiledShaderHandle>(shader);
}

void WebGPURenderInterface::RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                                         Rml::Vector2f translation, Rml::TextureHandle) {
  if (shader == 0 || geometry == 0) {
    return;
  }
  EnsureActiveLayerPass("RmlUi resumed shader layer pass");
  if (m_pass == nullptr) {
    return;
  }

  SetupRenderState(translation);

  const auto* shaderData = reinterpret_cast<const CompiledShaderData*>(shader);
  const uint32_t shaderOffset = m_shaderUniformCurrentOffset;
  m_shaderUniformCurrentOffset += AURORA_ALIGN(sizeof(GradientUniformBlock), 256);
  webgpu::g_queue.WriteBuffer(m_shaderUniformBuffer, shaderOffset, &shaderData->gradient, sizeof(shaderData->gradient));

  const auto* geometryData = reinterpret_cast<const ShaderGeometryData*>(geometry);
  m_pass.SetVertexBuffer(0, geometryData->m_vertexBuffer, 0, geometryData->m_vertexBuffer.GetSize());
  m_pass.SetIndexBuffer(geometryData->m_indexBuffer, wgpu::IndexFormat::Uint32, 0,
                        geometryData->m_indexBuffer.GetSize());
  m_pass.SetPipeline(m_gradientPipelines[m_clipMaskEnabled ? 1 : 0]);
  m_pass.SetBindGroup(0, m_commonBindGroup, 1, &m_uniformCurrentOffset);
  m_pass.SetBindGroup(1, m_shaderBindGroup, 1, &shaderOffset);
  m_pass.DrawIndexed(geometryData->m_indexBuffer.GetSize() / sizeof(int));

  m_uniformCurrentOffset += AURORA_ALIGN(sizeof(UniformBlock), 256);
}

void WebGPURenderInterface::ReleaseShader(Rml::CompiledShaderHandle shader) {
  delete reinterpret_cast<CompiledShaderData*>(shader);
}

// code heavily based of imgui wgpu impl
void WebGPURenderInterface::CreateDeviceObjects() {
  constexpr std::array commonBindGroupLayoutEntries{
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
  const wgpu::BindGroupLayoutDescriptor commonBindGroupLayoutDesc{
      .entryCount = commonBindGroupLayoutEntries.size(),
      .entries = commonBindGroupLayoutEntries.data(),
  };
  m_commonBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&commonBindGroupLayoutDesc);

  constexpr std::array imageBindGroupLayoutEntries{
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
  const wgpu::BindGroupLayoutDescriptor imageBindGroupLayoutDesc{
      .entryCount = imageBindGroupLayoutEntries.size(),
      .entries = imageBindGroupLayoutEntries.data(),
  };
  m_imageBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&imageBindGroupLayoutDesc);

  constexpr std::array blurBindGroupLayoutEntries{
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
  const wgpu::BindGroupLayoutDescriptor blurBindGroupLayoutDesc{
      .entryCount = blurBindGroupLayoutEntries.size(),
      .entries = blurBindGroupLayoutEntries.data(),
  };
  m_blurBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&blurBindGroupLayoutDesc);

  constexpr std::array dropShadowBindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .buffer =
              {
                  .type = wgpu::BufferBindingType::Uniform,
                  .hasDynamicOffset = true,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor dropShadowBindGroupLayoutDesc{
      .entryCount = dropShadowBindGroupLayoutEntries.size(),
      .entries = dropShadowBindGroupLayoutEntries.data(),
  };
  m_dropShadowBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&dropShadowBindGroupLayoutDesc);

  constexpr std::array shaderBindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .buffer =
              {
                  .type = wgpu::BufferBindingType::Uniform,
                  .hasDynamicOffset = true,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor shaderBindGroupLayoutDesc{
      .entryCount = shaderBindGroupLayoutEntries.size(),
      .entries = shaderBindGroupLayoutEntries.data(),
  };
  m_shaderBindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&shaderBindGroupLayoutDesc);

  const std::array layouts{m_commonBindGroupLayout, m_imageBindGroupLayout};
  const wgpu::PipelineLayoutDescriptor layoutDesc{
      .bindGroupLayoutCount = layouts.size(),
      .bindGroupLayouts = layouts.data(),
  };
  m_pipelineLayout = webgpu::g_device.CreatePipelineLayout(&layoutDesc);

  const std::array blurLayouts{m_commonBindGroupLayout, m_imageBindGroupLayout, m_blurBindGroupLayout};
  const wgpu::PipelineLayoutDescriptor blurLayoutDesc{
      .bindGroupLayoutCount = blurLayouts.size(),
      .bindGroupLayouts = blurLayouts.data(),
  };
  m_blurPipelineLayout = webgpu::g_device.CreatePipelineLayout(&blurLayoutDesc);

  const std::array dropShadowLayouts{m_commonBindGroupLayout, m_imageBindGroupLayout, m_dropShadowBindGroupLayout};
  const wgpu::PipelineLayoutDescriptor dropShadowLayoutDesc{
      .bindGroupLayoutCount = dropShadowLayouts.size(),
      .bindGroupLayouts = dropShadowLayouts.data(),
  };
  m_dropShadowPipelineLayout = webgpu::g_device.CreatePipelineLayout(&dropShadowLayoutDesc);

  const std::array colorMatrixLayouts{m_commonBindGroupLayout, m_imageBindGroupLayout, m_shaderBindGroupLayout};
  const wgpu::PipelineLayoutDescriptor colorMatrixLayoutDesc{
      .bindGroupLayoutCount = colorMatrixLayouts.size(),
      .bindGroupLayouts = colorMatrixLayouts.data(),
  };
  m_colorMatrixPipelineLayout = webgpu::g_device.CreatePipelineLayout(&colorMatrixLayoutDesc);

  const std::array maskImageLayouts{m_commonBindGroupLayout, m_imageBindGroupLayout, m_imageBindGroupLayout};
  const wgpu::PipelineLayoutDescriptor maskImageLayoutDesc{
      .bindGroupLayoutCount = maskImageLayouts.size(),
      .bindGroupLayouts = maskImageLayouts.data(),
  };
  m_maskImagePipelineLayout = webgpu::g_device.CreatePipelineLayout(&maskImageLayoutDesc);

  const std::array shaderLayouts{m_commonBindGroupLayout, m_shaderBindGroupLayout};
  const wgpu::PipelineLayoutDescriptor shaderLayoutDesc{
      .bindGroupLayoutCount = shaderLayouts.size(),
      .bindGroupLayouts = shaderLayouts.data(),
  };
  m_shaderPipelineLayout = webgpu::g_device.CreatePipelineLayout(&shaderLayoutDesc);

  const auto vertexShader = compile_shader(vertexSource, "RmlUi Vertex Shader");
  const auto fragmentShader = compile_shader(fragmentSource, "RmlUi Fragment Shader");
  const auto gradientFragmentShader = compile_shader(gradientFragmentSource, "RmlUi Gradient Fragment Shader");
  const auto fullscreenVertexShader = compile_shader(fullscreenVertexSource, "RmlUi Fullscreen Vertex Shader");
  const auto blurVertexShader = compile_shader(blurVertexSource, "RmlUi Blur Vertex Shader");
  const auto blitFragmentShader = compile_shader(blitFragmentSource, "RmlUi Blit Fragment Shader");
  const auto opaqueBlitFragmentShader = compile_shader(opaqueBlitFragmentSource, "RmlUi Opaque Blit Fragment Shader");
  const auto colorMatrixFragmentShader =
      compile_shader(colorMatrixFragmentSource, "RmlUi Color Matrix Fragment Shader");
  const auto maskImageFragmentShader = compile_shader(maskImageFragmentSource, "RmlUi Mask Image Fragment Shader");
  const auto blurFragmentShader = compile_shader(blurFragmentSource, "RmlUi Blur Fragment Shader");
  const auto regionBlitFragmentShader = compile_shader(regionBlitFragmentSource, "RmlUi Region Blit Fragment Shader");
  const auto dropShadowFragmentShader = compile_shader(dropShadowFragmentSource, "RmlUi Drop Shadow Fragment Shader");
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
  constexpr wgpu::BlendState premultipliedBlendState{
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
  constexpr wgpu::BlendState opacityBlendState{
      .color =
          {
              .operation = wgpu::BlendOperation::Add,
              .srcFactor = wgpu::BlendFactor::Constant,
              .dstFactor = wgpu::BlendFactor::Zero,
          },
      .alpha =
          {
              .operation = wgpu::BlendOperation::Add,
              .srcFactor = wgpu::BlendFactor::Constant,
              .dstFactor = wgpu::BlendFactor::Zero,
          },
  };

  const auto create_pipeline = [&](const char* label, wgpu::CompareFunction compareFn,
                                   wgpu::StencilOperation stencilPass, wgpu::ColorWriteMask colorWriteMask) {
    const wgpu::ColorTargetState colorState{
        .format = m_renderTargetFormat,
        .blend = &premultipliedBlendState,
        .writeMask = colorWriteMask,
    };
    const wgpu::FragmentState fragmentState{
        .module = fragmentShader.module,
        .entryPoint = fragmentShader.entryPoint,
        .targetCount = 1,
        .targets = &colorState,
    };
    const auto stencilFace = stencil_face(compareFn, stencilPass);
    const wgpu::DepthStencilState depthStencilState{
        .format = ClipMaskStencilFormat,
        .stencilFront = stencilFace,
        .stencilBack = stencilFace,
        .stencilReadMask = 0xFF,
        .stencilWriteMask = 0xFF,
    };
    const wgpu::RenderPipelineDescriptor pipelineDesc{
        .label = label,
        .layout = m_pipelineLayout,
        .vertex =
            {
                .module = vertexShader.module,
                .entryPoint = vertexShader.entryPoint,
                .bufferCount = vertexBufferLayouts.size(),
                .buffers = vertexBufferLayouts.data(),
            },
        .primitive =
            {
                .topology = wgpu::PrimitiveTopology::TriangleList,
                .stripIndexFormat = wgpu::IndexFormat::Undefined,
                .frontFace = wgpu::FrontFace::CW,
                .cullMode = wgpu::CullMode::None,
            },
        .depthStencil = &depthStencilState,
        .multisample =
            {
                .count = LayerSampleCount,
            },
        .fragment = &fragmentState,
    };
    return webgpu::g_device.CreateRenderPipeline(&pipelineDesc);
  };

  m_pipelines[static_cast<size_t>(PipelineType::Normal)] = create_pipeline(
      "RmlUi Pipeline", wgpu::CompareFunction::Always, wgpu::StencilOperation::Keep, wgpu::ColorWriteMask::All);
  m_pipelines[static_cast<size_t>(PipelineType::Masked)] = create_pipeline(
      "RmlUi Masked Pipeline", wgpu::CompareFunction::Equal, wgpu::StencilOperation::Keep, wgpu::ColorWriteMask::All);
  m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)] =
      create_pipeline("RmlUi Clip Replace Pipeline", wgpu::CompareFunction::Always, wgpu::StencilOperation::Replace,
                      wgpu::ColorWriteMask::None);
  m_pipelines[static_cast<size_t>(PipelineType::ClipIntersect)] =
      create_pipeline("RmlUi Clip Intersect Pipeline", wgpu::CompareFunction::Equal,
                      wgpu::StencilOperation::IncrementClamp, wgpu::ColorWriteMask::None);

  const auto create_gradient_pipeline = [&](const char* label, wgpu::CompareFunction compareFn) {
    const wgpu::ColorTargetState colorState{
        .format = m_renderTargetFormat,
        .blend = &premultipliedBlendState,
        .writeMask = wgpu::ColorWriteMask::All,
    };
    const wgpu::FragmentState fragmentState{
        .module = gradientFragmentShader.module,
        .entryPoint = gradientFragmentShader.entryPoint,
        .targetCount = 1,
        .targets = &colorState,
    };
    const auto stencilFace = stencil_face(compareFn, wgpu::StencilOperation::Keep);
    const wgpu::DepthStencilState depthStencilState{
        .format = ClipMaskStencilFormat,
        .stencilFront = stencilFace,
        .stencilBack = stencilFace,
        .stencilReadMask = 0xFF,
        .stencilWriteMask = 0xFF,
    };
    const wgpu::RenderPipelineDescriptor pipelineDesc{
        .label = label,
        .layout = m_shaderPipelineLayout,
        .vertex =
            {
                .module = vertexShader.module,
                .entryPoint = vertexShader.entryPoint,
                .bufferCount = vertexBufferLayouts.size(),
                .buffers = vertexBufferLayouts.data(),
            },
        .primitive =
            {
                .topology = wgpu::PrimitiveTopology::TriangleList,
                .stripIndexFormat = wgpu::IndexFormat::Undefined,
                .frontFace = wgpu::FrontFace::CW,
                .cullMode = wgpu::CullMode::None,
            },
        .depthStencil = &depthStencilState,
        .multisample =
            {
                .count = LayerSampleCount,
            },
        .fragment = &fragmentState,
    };
    return webgpu::g_device.CreateRenderPipeline(&pipelineDesc);
  };

  m_gradientPipelines[0] = create_gradient_pipeline("RmlUi Gradient Pipeline", wgpu::CompareFunction::Always);
  m_gradientPipelines[1] = create_gradient_pipeline("RmlUi Masked Gradient Pipeline", wgpu::CompareFunction::Equal);

  const auto create_blit_pipeline = [&](const char* label, const wgpu::ComputeState& blitShader,
                                        wgpu::CompareFunction compareFn, bool blend, uint32_t sampleCount,
                                        bool useStencil) {
    const wgpu::ColorTargetState colorState{
        .format = m_renderTargetFormat,
        .blend = blend ? &premultipliedBlendState : nullptr,
        .writeMask = wgpu::ColorWriteMask::All,
    };
    const wgpu::FragmentState fragmentState{
        .module = blitShader.module,
        .entryPoint = blitShader.entryPoint,
        .targetCount = 1,
        .targets = &colorState,
    };
    const auto stencilFace = stencil_face(compareFn, wgpu::StencilOperation::Keep);
    const wgpu::DepthStencilState depthStencilState{
        .format = ClipMaskStencilFormat,
        .stencilFront = stencilFace,
        .stencilBack = stencilFace,
        .stencilReadMask = 0xFF,
        .stencilWriteMask = 0xFF,
    };
    const wgpu::RenderPipelineDescriptor pipelineDesc{
        .label = label,
        .layout = m_pipelineLayout,
        .vertex =
            {
                .module = fullscreenVertexShader.module,
                .entryPoint = fullscreenVertexShader.entryPoint,
            },
        .primitive =
            {
                .topology = wgpu::PrimitiveTopology::TriangleList,
            },
        .depthStencil = useStencil ? &depthStencilState : nullptr,
        .multisample =
            {
                .count = sampleCount,
            },
        .fragment = &fragmentState,
    };
    return webgpu::g_device.CreateRenderPipeline(&pipelineDesc);
  };

  m_blitPipelines[static_cast<size_t>(BlitPipelineType::Blend)] = create_blit_pipeline(
      "RmlUi Blit Blend Pipeline", blitFragmentShader, wgpu::CompareFunction::Always, true, 1, false);
  m_blitPipelines[static_cast<size_t>(BlitPipelineType::BlendMasked)] = create_blit_pipeline(
      "RmlUi Blit Blend Masked Pipeline", blitFragmentShader, wgpu::CompareFunction::Equal, true, 1, false);
  m_blitPipelines[static_cast<size_t>(BlitPipelineType::Replace)] = create_blit_pipeline(
      "RmlUi Blit Replace Pipeline", blitFragmentShader, wgpu::CompareFunction::Always, false, 1, false);
  m_blitPipelines[static_cast<size_t>(BlitPipelineType::ReplaceMasked)] = create_blit_pipeline(
      "RmlUi Blit Replace Masked Pipeline", blitFragmentShader, wgpu::CompareFunction::Equal, false, 1, false);
  m_layerBlitPipelines[static_cast<size_t>(BlitPipelineType::Blend)] =
      create_blit_pipeline("RmlUi Layer Blit Blend Pipeline", blitFragmentShader, wgpu::CompareFunction::Always, true,
                           LayerSampleCount, true);
  m_layerBlitPipelines[static_cast<size_t>(BlitPipelineType::BlendMasked)] =
      create_blit_pipeline("RmlUi Layer Blit Blend Masked Pipeline", blitFragmentShader, wgpu::CompareFunction::Equal,
                           true, LayerSampleCount, true);
  m_layerBlitPipelines[static_cast<size_t>(BlitPipelineType::Replace)] =
      create_blit_pipeline("RmlUi Layer Blit Replace Pipeline", blitFragmentShader, wgpu::CompareFunction::Always,
                           false, LayerSampleCount, true);
  m_layerBlitPipelines[static_cast<size_t>(BlitPipelineType::ReplaceMasked)] =
      create_blit_pipeline("RmlUi Layer Blit Replace Masked Pipeline", blitFragmentShader, wgpu::CompareFunction::Equal,
                           false, LayerSampleCount, true);
  m_opaqueBlitPipeline = create_blit_pipeline("RmlUi Opaque Blit Pipeline", opaqueBlitFragmentShader,
                                              wgpu::CompareFunction::Always, false, 1, false);
  m_layerOpaqueBlitPipeline = create_blit_pipeline("RmlUi Layer Opaque Blit Pipeline", opaqueBlitFragmentShader,
                                                   wgpu::CompareFunction::Always, false, LayerSampleCount, true);

  const wgpu::ColorTargetState opacityColorState{
      .format = m_renderTargetFormat,
      .blend = &opacityBlendState,
      .writeMask = wgpu::ColorWriteMask::All,
  };
  const wgpu::FragmentState opacityFragmentState{
      .module = blitFragmentShader.module,
      .entryPoint = blitFragmentShader.entryPoint,
      .targetCount = 1,
      .targets = &opacityColorState,
  };
  const wgpu::RenderPipelineDescriptor opacityPipelineDesc{
      .label = "RmlUi Opacity Pipeline",
      .layout = m_pipelineLayout,
      .vertex =
          {
              .module = fullscreenVertexShader.module,
              .entryPoint = fullscreenVertexShader.entryPoint,
          },
      .primitive =
          {
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .depthStencil = nullptr,
      .multisample =
          {
              .count = 1,
          },
      .fragment = &opacityFragmentState,
  };
  m_opacityPipeline = webgpu::g_device.CreateRenderPipeline(&opacityPipelineDesc);

  const wgpu::ColorTargetState blurColorState{
      .format = m_renderTargetFormat,
      .writeMask = wgpu::ColorWriteMask::All,
  };
  const wgpu::FragmentState blurFragmentState{
      .module = blurFragmentShader.module,
      .entryPoint = blurFragmentShader.entryPoint,
      .targetCount = 1,
      .targets = &blurColorState,
  };
  const auto create_filter_pipeline = [&](const char* label, const wgpu::PipelineLayout& layout,
                                          const wgpu::ComputeState& fragmentShader) {
    const wgpu::FragmentState fragmentState{
        .module = fragmentShader.module,
        .entryPoint = fragmentShader.entryPoint,
        .targetCount = 1,
        .targets = &blurColorState,
    };
    const wgpu::RenderPipelineDescriptor pipelineDesc{
        .label = label,
        .layout = layout,
        .vertex =
            {
                .module = fullscreenVertexShader.module,
                .entryPoint = fullscreenVertexShader.entryPoint,
            },
        .primitive =
            {
                .topology = wgpu::PrimitiveTopology::TriangleList,
            },
        .depthStencil = nullptr,
        .multisample =
            {
                .count = 1,
            },
        .fragment = &fragmentState,
    };
    return webgpu::g_device.CreateRenderPipeline(&pipelineDesc);
  };

  m_colorMatrixPipeline =
      create_filter_pipeline("RmlUi Color Matrix Pipeline", m_colorMatrixPipelineLayout, colorMatrixFragmentShader);
  m_maskImagePipeline =
      create_filter_pipeline("RmlUi Mask Image Pipeline", m_maskImagePipelineLayout, maskImageFragmentShader);

  const wgpu::RenderPipelineDescriptor blurPipelineDesc{
      .label = "RmlUi Blur Pipeline",
      .layout = m_blurPipelineLayout,
      .vertex =
          {
              .module = blurVertexShader.module,
              .entryPoint = blurVertexShader.entryPoint,
          },
      .primitive =
          {
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .depthStencil = nullptr,
      .multisample =
          {
              .count = 1,
          },
      .fragment = &blurFragmentState,
  };
  m_blurPipeline = webgpu::g_device.CreateRenderPipeline(&blurPipelineDesc);

  const wgpu::FragmentState regionBlitFragmentState{
      .module = regionBlitFragmentShader.module,
      .entryPoint = regionBlitFragmentShader.entryPoint,
      .targetCount = 1,
      .targets = &blurColorState,
  };
  const wgpu::RenderPipelineDescriptor regionBlitPipelineDesc{
      .label = "RmlUi Region Blit Pipeline",
      .layout = m_blurPipelineLayout,
      .vertex =
          {
              .module = fullscreenVertexShader.module,
              .entryPoint = fullscreenVertexShader.entryPoint,
          },
      .primitive =
          {
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .depthStencil = nullptr,
      .multisample =
          {
              .count = 1,
          },
      .fragment = &regionBlitFragmentState,
  };
  m_regionBlitPipeline = webgpu::g_device.CreateRenderPipeline(&regionBlitPipelineDesc);

  const wgpu::ColorTargetState dropShadowColorState{
      .format = m_renderTargetFormat,
      .writeMask = wgpu::ColorWriteMask::All,
  };
  const wgpu::FragmentState dropShadowFragmentState{
      .module = dropShadowFragmentShader.module,
      .entryPoint = dropShadowFragmentShader.entryPoint,
      .targetCount = 1,
      .targets = &dropShadowColorState,
  };
  const wgpu::RenderPipelineDescriptor dropShadowPipelineDesc{
      .label = "RmlUi Drop Shadow Pipeline",
      .layout = m_dropShadowPipelineLayout,
      .vertex =
          {
              .module = fullscreenVertexShader.module,
              .entryPoint = fullscreenVertexShader.entryPoint,
          },
      .primitive =
          {
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .depthStencil = nullptr,
      .multisample =
          {
              .count = 1,
          },
      .fragment = &dropShadowFragmentState,
  };
  m_dropShadowPipeline = webgpu::g_device.CreateRenderPipeline(&dropShadowPipelineDesc);

  const wgpu::SamplerDescriptor samplerDesc{
      .addressModeU = wgpu::AddressMode::Repeat,
      .addressModeV = wgpu::AddressMode::Repeat,
      .addressModeW = wgpu::AddressMode::Repeat,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Linear,
      .maxAnisotropy = 1,
  };
  m_sampler = webgpu::g_device.CreateSampler(&samplerDesc);

  CreateUniformBuffer();
  const wgpu::BufferDescriptor blurUniformBufferDesc{
      .label = "RmlUi Blur Uniform Buffer",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
      .size = AURORA_ALIGN(UniformBufferSize, 16),
  };
  m_blurUniformBuffer = webgpu::g_device.CreateBuffer(&blurUniformBufferDesc);
  const wgpu::BufferDescriptor dropShadowUniformBufferDesc{
      .label = "RmlUi Drop Shadow Uniform Buffer",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
      .size = AURORA_ALIGN(UniformBufferSize, 16),
  };
  m_dropShadowUniformBuffer = webgpu::g_device.CreateBuffer(&dropShadowUniformBufferDesc);
  const wgpu::BufferDescriptor shaderUniformBufferDesc{
      .label = "RmlUi Shader Uniform Buffer",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
      .size = AURORA_ALIGN(UniformBufferSize, 16),
  };
  m_shaderUniformBuffer = webgpu::g_device.CreateBuffer(&shaderUniformBufferDesc);

  const std::array commonBindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .buffer = m_uniformBuffer,
          .offset = 0,
          .size = AURORA_ALIGN(sizeof(UniformBlock), 16),
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .sampler = m_sampler,
      },
  };
  const wgpu::BindGroupDescriptor commonBindGroupDescriptor{
      .layout = m_commonBindGroupLayout,
      .entryCount = commonBindGroupEntries.size(),
      .entries = commonBindGroupEntries.data(),
  };
  m_commonBindGroup = webgpu::g_device.CreateBindGroup(&commonBindGroupDescriptor);

  const std::array blurBindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .buffer = m_blurUniformBuffer,
          .offset = 0,
          .size = AURORA_ALIGN(sizeof(BlurUniformBlock), 16),
      },
  };
  const wgpu::BindGroupDescriptor blurBindGroupDescriptor{
      .layout = m_blurBindGroupLayout,
      .entryCount = blurBindGroupEntries.size(),
      .entries = blurBindGroupEntries.data(),
  };
  m_blurBindGroup = webgpu::g_device.CreateBindGroup(&blurBindGroupDescriptor);

  const std::array dropShadowBindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .buffer = m_dropShadowUniformBuffer,
          .offset = 0,
          .size = AURORA_ALIGN(sizeof(DropShadowUniformBlock), 16),
      },
  };
  const wgpu::BindGroupDescriptor dropShadowBindGroupDescriptor{
      .layout = m_dropShadowBindGroupLayout,
      .entryCount = dropShadowBindGroupEntries.size(),
      .entries = dropShadowBindGroupEntries.data(),
  };
  m_dropShadowBindGroup = webgpu::g_device.CreateBindGroup(&dropShadowBindGroupDescriptor);

  const std::array shaderBindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .buffer = m_shaderUniformBuffer,
          .offset = 0,
          .size = AURORA_ALIGN(sizeof(GradientUniformBlock), 16),
      },
  };
  const wgpu::BindGroupDescriptor shaderBindGroupDescriptor{
      .layout = m_shaderBindGroupLayout,
      .entryCount = shaderBindGroupEntries.size(),
      .entries = shaderBindGroupEntries.data(),
  };
  m_shaderBindGroup = webgpu::g_device.CreateBindGroup(&shaderBindGroupDescriptor);

  switch (m_renderTargetFormat) {
  case wgpu::TextureFormat::ASTC10x10UnormSrgb:
  case wgpu::TextureFormat::ASTC10x5UnormSrgb:
  case wgpu::TextureFormat::ASTC10x6UnormSrgb:
  case wgpu::TextureFormat::ASTC10x8UnormSrgb:
  case wgpu::TextureFormat::ASTC12x10UnormSrgb:
  case wgpu::TextureFormat::ASTC12x12UnormSrgb:
  case wgpu::TextureFormat::ASTC4x4UnormSrgb:
  case wgpu::TextureFormat::ASTC5x5UnormSrgb:
  case wgpu::TextureFormat::ASTC6x5UnormSrgb:
  case wgpu::TextureFormat::ASTC6x6UnormSrgb:
  case wgpu::TextureFormat::ASTC8x5UnormSrgb:
  case wgpu::TextureFormat::ASTC8x6UnormSrgb:
  case wgpu::TextureFormat::ASTC8x8UnormSrgb:
  case wgpu::TextureFormat::BC1RGBAUnormSrgb:
  case wgpu::TextureFormat::BC2RGBAUnormSrgb:
  case wgpu::TextureFormat::BC3RGBAUnormSrgb:
  case wgpu::TextureFormat::BC7RGBAUnormSrgb:
  case wgpu::TextureFormat::BGRA8UnormSrgb:
  case wgpu::TextureFormat::ETC2RGB8A1UnormSrgb:
  case wgpu::TextureFormat::ETC2RGB8UnormSrgb:
  case wgpu::TextureFormat::ETC2RGBA8UnormSrgb:
  case wgpu::TextureFormat::RGBA8UnormSrgb:
    m_gamma = 2.2f;
    break;
  default:
    m_gamma = 1.0f;
  }

  CreateNullTexture();
}

void WebGPURenderInterface::SetupRenderState(const Rml::Vector2f& translation) {
  const float L = 0.f;
  const float R = static_cast<float>(m_windowSize.x);
  const float T = 0.f;
  const float B = static_cast<float>(m_windowSize.y);
  const float near = -10000.f;
  const float far = 10000.f;

  const Rml::Matrix4f proj = Rml::Matrix4f::FromColumns(
      {2.0f / (R - L), 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f / (T - B), 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f / (far - near), 0.0f},
      {(R + L) / (L - R), (T + B) / (B - T), -near / (far - near), 1.0f});

  const UniformBlock ubo{
      .MVP = proj * m_translationMatrix,
      .translation = {translation.x, translation.y, 0.0f, 1.0f},
      .Gamma = m_gamma,
  };
  webgpu::g_queue.WriteBuffer(m_uniformBuffer, m_uniformCurrentOffset, &ubo, sizeof(UniformBlock));

  constexpr wgpu::Color BlendColor{0.f, 0.f, 0.f, 0.f};
  m_pass.SetBlendConstant(&BlendColor);
}

void WebGPURenderInterface::CreateNullTexture() {
  constexpr std::array<Rml::byte, 4> tex_bytes{0xFF, 0xFF, 0xFF, 0xFF};
  const Rml::Span tex(tex_bytes.data(), tex_bytes.size());

  m_nullTexture = GenerateTexture(tex, {1, 1});
}

void WebGPURenderInterface::EnsureClipResetGeometry() {
  if (m_clipResetGeometry != 0 && m_clipResetGeometrySize == m_windowSize) {
    return;
  }

  if (m_clipResetGeometry != 0) {
    ReleaseGeometry(m_clipResetGeometry);
    m_clipResetGeometry = 0;
  }

  const float width = static_cast<float>(std::max(m_windowSize.x, 1));
  const float height = static_cast<float>(std::max(m_windowSize.y, 1));
  const auto colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
  const std::array vertices{
      Rml::Vertex{.position = {0.f, 0.f}, .colour = colour},
      Rml::Vertex{.position = {width, 0.f}, .colour = colour},
      Rml::Vertex{.position = {width, height}, .colour = colour},
      Rml::Vertex{.position = {0.f, height}, .colour = colour},
  };
  const std::array indices = {0, 1, 2, 0, 2, 3};
  m_clipResetGeometry = CompileGeometry({vertices.data(), vertices.size()}, {indices.data(), indices.size()});
  m_clipResetGeometrySize = m_windowSize;
}

void WebGPURenderInterface::CreateUniformBuffer() {
  constexpr wgpu::BufferDescriptor bufferDesc{
      .label = "RmlUi Uniform Buffer",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
      .size = AURORA_ALIGN(UniformBufferSize, 16),
  };
  m_uniformBuffer = webgpu::g_device.CreateBuffer(&bufferDesc);
}

void WebGPURenderInterface::NewFrame() {
  m_uniformCurrentOffset = 0;
  m_blurUniformCurrentOffset = 0;
  m_dropShadowUniformCurrentOffset = 0;
  m_shaderUniformCurrentOffset = 0;
  m_clipMaskEnabled = false;
  m_stencilRef = 0;
}

wgpu::TextureView WebGPURenderInterface::GetClipMaskStencilView(const wgpu::Extent3D& size) {
  if (m_clipMaskStencilView && m_clipMaskStencilSize == size) {
    return m_clipMaskStencilView;
  }

  m_clipMaskStencilSize = size;
  const wgpu::TextureDescriptor textureDesc{
      .label = "RmlUi Clip Mask Stencil",
      .usage = wgpu::TextureUsage::RenderAttachment,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = ClipMaskStencilFormat,
      .mipLevelCount = 1,
      .sampleCount = LayerSampleCount,
  };
  m_clipMaskStencilTexture = webgpu::g_device.CreateTexture(&textureDesc);

  constexpr wgpu::TextureViewDescriptor viewDesc{
      .label = "RmlUi Clip Mask Stencil View",
      .format = ClipMaskStencilFormat,
      .dimension = wgpu::TextureViewDimension::e2D,
      .aspect = wgpu::TextureAspect::StencilOnly,
  };
  m_clipMaskStencilView = m_clipMaskStencilTexture.CreateView(&viewDesc);
  return m_clipMaskStencilView;
}

} // namespace aurora::rmlui

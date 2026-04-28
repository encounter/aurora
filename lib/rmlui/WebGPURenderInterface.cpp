#include "WebGPURenderInterface.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>

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

bool IsFile(const std::string& path) {
  SDL_PathInfo pathInfo{};
  return SDL_GetPathInfo(path.c_str(), &pathInfo) && pathInfo.type == SDL_PATHTYPE_FILE;
}

std::string ResolveTextureSource(const Rml::String& source) {
  std::string path(source);
  constexpr std::string_view scheme = "file://";
  if (path.compare(0, scheme.size(), scheme) == 0) {
    path.erase(0, scheme.size());
  }
  if (IsFile(path)) {
    return path;
  }

  if (!path.empty() && path.front() != '/') {
    const std::string absPath = "/" + path;
    if (IsFile(absPath)) {
      return absPath;
    }
  }

  const char* base_path = SDL_GetBasePath();
  if (base_path != nullptr && base_path[0] != '\0') {
    const std::string base(base_path);
    const std::string baseRel = base + path;
    if (IsFile(baseRel)) {
      return baseRel;
    }
  }

  return path;
}

Image GetImage(const std::string& path) {
  SDL_PathInfo pathInfo{};
  if (!(SDL_GetPathInfo(path.c_str(), &pathInfo) && pathInfo.type == SDL_PATHTYPE_FILE)) {
    Log.warn("Image '{}' does not exist", path);
    return {};
  }

  SDL_Surface* loadedSurface = SDL_LoadPNG(path.c_str());
  if (loadedSurface == nullptr) {
    Log.warn("Failed to load image '{}': {}", path, SDL_GetError());
    return {};
  }

  SDL_Surface* rgbaSurface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loadedSurface);
  if (rgbaSurface == nullptr) {
    Log.warn("Failed to convert image '{}': {}", path, SDL_GetError());
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
  }

  SDL_DestroySurface(rgbaSurface);
  return Image{
      .data = std::move(ptr),
      .size = size,
      .width = iconWidth,
      .height = iconHeight,
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
    let corrected_color = pow(color.rgb, vec3<f32>(uniforms.gamma));
    return vec4<f32>(corrected_color, color.a);
}
)"sv;

inline constexpr uint64_t UniformBufferSize = 1048576; // 1mb

static wgpu::ComputeState CreateShaderModule(const std::string_view& wgslSource, const std::string_view& label) {
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

static wgpu::StencilFaceState CreateStencilFace(wgpu::CompareFunction compare, wgpu::StencilOperation passOp) {
  return {
      .compare = compare,
      .failOp = wgpu::StencilOperation::Keep,
      .depthFailOp = wgpu::StencilOperation::Keep,
      .passOp = passOp,
  };
}

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
  SetupRenderState(translation);

  auto* geometryData = reinterpret_cast<ShaderGeometryData*>(geometry);
  auto* textureData = reinterpret_cast<ShaderTextureData*>(texture != 0 ? texture : m_nullTexture);

  m_pass->SetVertexBuffer(0, geometryData->m_vertexBuffer, 0, geometryData->m_vertexBuffer.GetSize());
  m_pass->SetIndexBuffer(geometryData->m_indexBuffer, wgpu::IndexFormat::Uint32, 0,
                         geometryData->m_indexBuffer.GetSize());
  m_pass->SetPipeline(pipeline);
  m_pass->SetBindGroup(0, m_CommonBindGroup, 1, &m_uniformCurrentOffset);
  m_pass->SetBindGroup(1, textureData->m_bindGroup);
  m_pass->DrawIndexed(geometryData->m_indexBuffer.GetSize() / sizeof(int));

  m_uniformCurrentOffset += AURORA_ALIGN(sizeof(UniformBlock), 64);
}

void WebGPURenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  delete reinterpret_cast<ShaderGeometryData*>(geometry);
}

Rml::TextureHandle WebGPURenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
  // load texels from image source
  const auto resolved_source = ResolveTextureSource(source);
  const auto image = GetImage(resolved_source);

  if (image.size == 0) {
    Log.error("Failed to load texture! Path: {}", resolved_source);
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
      .layout = m_ImageBindGroupLayout,
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

  if (!m_enableScissorRegion || !m_scissorRegion.Valid()) {
    m_pass->SetScissorRect(0, 0, std::max(m_windowSize.x, 0), std::max(m_windowSize.y, 0));
    return;
  }

  const uint32_t x = static_cast<uint32_t>(std::clamp(m_scissorRegion.Left(), 0, m_windowSize.x));
  const uint32_t y = static_cast<uint32_t>(std::clamp(m_scissorRegion.Top(), 0, m_windowSize.y));
  const uint32_t width =
      static_cast<uint32_t>(std::clamp(m_scissorRegion.Width(), 0, m_windowSize.x - static_cast<int>(x)));
  const uint32_t height =
      static_cast<uint32_t>(std::clamp(m_scissorRegion.Height(), 0, m_windowSize.y - static_cast<int>(y)));
  m_pass->SetScissorRect(x, y, width, height);
}

void WebGPURenderInterface::EnableClipMask(bool enable) {
  m_clipMaskEnabled = enable;
  if (!enable) {
    m_stencilRef = 0;
    if (m_pass != nullptr) {
      m_pass->SetStencilReference(0);
    }
  } else if (m_pass != nullptr) {
    m_pass->SetStencilReference(m_stencilRef);
  }
}

void WebGPURenderInterface::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                                             Rml::Vector2f translation) {
  if (m_pass == nullptr) {
    return;
  }

  EnsureClipResetGeometry();

  const Rml::Matrix4f prevMatrix = m_translationMatrix;
  switch (operation) {
  case Rml::ClipMaskOperation::Set:
    m_translationMatrix = Rml::Matrix4f::Identity();
    m_pass->SetStencilReference(0);
    DrawGeometry(m_clipResetGeometry, {}, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    m_translationMatrix = prevMatrix;

    m_stencilRef = 1;
    m_pass->SetStencilReference(m_stencilRef);
    DrawGeometry(geometry, translation, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    break;
  case Rml::ClipMaskOperation::SetInverse:
    m_translationMatrix = Rml::Matrix4f::Identity();
    m_pass->SetStencilReference(1);
    DrawGeometry(m_clipResetGeometry, {}, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    m_translationMatrix = prevMatrix;

    m_stencilRef = 1;
    m_pass->SetStencilReference(0);
    DrawGeometry(geometry, translation, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipReplace)]);
    break;
  case Rml::ClipMaskOperation::Intersect:
    if (m_stencilRef == std::numeric_limits<uint8_t>::max()) {
      Log.warn("RmlUi clip mask nesting exceeded stencil capacity; further nested clipping may be incorrect");
      break;
    }

    m_pass->SetStencilReference(m_stencilRef);
    DrawGeometry(geometry, translation, 0, m_pipelines[static_cast<size_t>(PipelineType::ClipIntersect)]);
    ++m_stencilRef;
    break;
  }

  m_pass->SetStencilReference(m_stencilRef);
}

void WebGPURenderInterface::SetTransform(const Rml::Matrix4f* transform) {
  if (transform == nullptr) {
    m_translationMatrix = Rml::Matrix4f::Identity();
  } else {
    m_translationMatrix = *transform;
  }
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

  const std::array layouts{
      webgpu::g_device.CreateBindGroupLayout(&commonBindGroupLayoutDesc),
      webgpu::g_device.CreateBindGroupLayout(&imageBindGroupLayoutDesc),
  };
  const wgpu::PipelineLayoutDescriptor layoutDesc{
      .bindGroupLayoutCount = layouts.size(),
      .bindGroupLayouts = layouts.data(),
  };
  const auto pipelineLayout = webgpu::g_device.CreatePipelineLayout(&layoutDesc);

  const auto vertexShader = CreateShaderModule(vertexSource, "RmlUi Vertex Shader");
  const auto fragmentShader = CreateShaderModule(fragmentSource, "RmlUi Fragment Shader");
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
  constexpr wgpu::BlendState blendState{
      .color =
          {
              .operation = wgpu::BlendOperation::Add,
              .srcFactor = wgpu::BlendFactor::SrcAlpha,
              .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
          },
      .alpha =
          {
              .operation = wgpu::BlendOperation::Add,
              .srcFactor = wgpu::BlendFactor::One,
              .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
          },
  };

  const auto create_pipeline = [&](const char* label, wgpu::CompareFunction compareFn,
                                   wgpu::StencilOperation stencilPass, wgpu::ColorWriteMask colorWriteMask) {
    const wgpu::ColorTargetState colorState{
        .format = m_renderTargetFormat,
        .blend = &blendState,
        .writeMask = colorWriteMask,
    };
    const wgpu::FragmentState fragmnetState{
        .module = fragmentShader.module,
        .entryPoint = fragmentShader.entryPoint,
        .targetCount = 1,
        .targets = &colorState,
    };
    const auto stencil_face = CreateStencilFace(compareFn, stencilPass);
    const wgpu::DepthStencilState depthStencilState{
        .format = ClipMaskStencilFormat,
        .stencilFront = stencil_face,
        .stencilBack = stencil_face,
        .stencilReadMask = 0xFF,
        .stencilWriteMask = 0xFF,
    };
    const wgpu::RenderPipelineDescriptor pipelineDesc{
        .label = label,
        .layout = pipelineLayout,
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
                .count = 1,
            },
        .fragment = &fragmnetState,
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
  const wgpu::SamplerDescriptor samplerDesc{
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Linear,
      .maxAnisotropy = 1,
  };
  m_sampler = webgpu::g_device.CreateSampler(&samplerDesc);

  CreateUniformBuffer();

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
      .layout = layouts[0],
      .entryCount = commonBindGroupEntries.size(),
      .entries = commonBindGroupEntries.data(),
  };
  m_CommonBindGroup = webgpu::g_device.CreateBindGroup(&commonBindGroupDescriptor);
  m_ImageBindGroupLayout = layouts[1];

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

  const Rml::Matrix4f proj =
      Rml::Matrix4f::FromColumns({2.0f / (R - L), 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
                                 {0.0f, 0.0f, 0.5f, 0.0f}, {(R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f});

  const UniformBlock ubo{
      .MVP = proj * m_translationMatrix,
      .translation = {translation.x, translation.y, 0.0f, 1.0f},
      .Gamma = m_gamma,
  };
  webgpu::g_queue.WriteBuffer(m_uniformBuffer, m_uniformCurrentOffset, &ubo, sizeof(UniformBlock));

  constexpr wgpu::Color BlendColor{0.f, 0.f, 0.f, 0.f};
  m_pass->SetBlendConstant(&BlendColor);
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
      .sampleCount = 1,
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

#include "WebGPURenderInterface.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_surface.h>

#include <string>
#include <string_view>

#include "../logging.hpp"
#include "../webgpu/gpu.hpp"
#include "../internal.hpp"
#include "../gfx/clear.hpp"

namespace aurora::rmlui {

struct Image {
  std::unique_ptr<uint8_t[]> data;
  size_t size;
  uint32_t width;
  uint32_t height;
};

struct ShaderGeometryData {
  wgpu::RenderPipeline m_pipeline; // potentially in the future, this can be used for RenderInterface::RenderShader support
  wgpu::Buffer m_vertexBuffer;
  wgpu::Buffer m_indexBuffer;
};

struct ShaderTextureData {
  wgpu::BindGroup m_bindGroup;
  wgpu::Texture m_texture;
  wgpu::TextureView m_textureView;
};

static Module Log("aurora::rmlui::RenderInterface");

bool IsFile(const std::string& path) {
  SDL_PathInfo pathInfo{};
  return SDL_GetPathInfo(path.c_str(), &pathInfo) && pathInfo.type == SDL_PATHTYPE_FILE;
}

std::string ResolveTextureSource(const Rml::String& source) {
  std::string path(source);
  constexpr std::string_view file_scheme = "file://";
  if (path.compare(0, file_scheme.size(), file_scheme) == 0) {
    path.erase(0, file_scheme.size());
  }
  if (IsFile(path)) {
    return path;
  }

  if (!path.empty() && path.front() != '/') {
    const std::string absolute_path = "/" + path;
    if (IsFile(absolute_path)) {
      return absolute_path;
    }
  }

  const char* base_path = SDL_GetBasePath();
  if (base_path != nullptr && base_path[0] != '\0') {
    const std::string base(base_path);
    const std::string base_relative = base + path;
    if (IsFile(base_relative)) {
      return base_relative;
    }
  }

  return path;
}

// shader source from imgui

static const char vtx_shader[] = R"(
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
)";

static const char frag_shader[] = R"(
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
)";

inline constexpr uint64_t UniformBufferSize = 1048576;  // 1mb

// helper func from dusk
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
    std::move(ptr),
    size,
    iconWidth,
    iconHeight
};
}

static wgpu::ComputeState CreateShaderModule(const std::string_view& wgsl_source, const std::string_view& shader_label) {
  wgpu::ShaderSourceWGSL shader_source = {};
  shader_source.sType = wgpu::SType::ShaderSourceWGSL;
  shader_source.code = wgsl_source;

  wgpu::ShaderModuleDescriptor module_desc = {
    &shader_source,
      shader_label
  };

  wgpu::ComputeState out_state = {};
  out_state.module = webgpu::g_device.CreateShaderModule(&module_desc);
  out_state.entryPoint = "main";

  return out_state;
}

Rml::CompiledGeometryHandle WebGPURenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
  ShaderGeometryData* shader_data = new ShaderGeometryData();

  shader_data->m_pipeline = m_pipeline;

  // setup vertex buffer

  wgpu::BufferDescriptor vtx_buf_desc = {
    nullptr,
    "RmlUi Vertex Buffer",
    wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex,
    AURORA_ALIGN(vertices.size() * sizeof(Rml::Vertex), 4),
    false
  };
  shader_data->m_vertexBuffer = webgpu::g_device.CreateBuffer(&vtx_buf_desc);
  webgpu::g_queue.WriteBuffer(shader_data->m_vertexBuffer, 0, vertices.data(), sizeof(Rml::Vertex) * vertices.size());

  // setup index buffer

  wgpu::BufferDescriptor idx_buf_desc = {
    nullptr,
    "RmlUi Index Buffer",
    wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index,
    AURORA_ALIGN(indices.size() * sizeof(int), 4),
    false
  };
  shader_data->m_indexBuffer = webgpu::g_device.CreateBuffer(&idx_buf_desc);
  webgpu::g_queue.WriteBuffer(shader_data->m_indexBuffer, 0, indices.data(), sizeof(int) * indices.size());

  return (Rml::CompiledGeometryHandle)shader_data;
}

void WebGPURenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {
  SetupRenderState(translation);

  auto* geometry_data = (ShaderGeometryData*)geometry;
  auto* texture_data = (ShaderTextureData*)(texture != 0 ? texture : m_nullTexture);

  m_pass->SetVertexBuffer(0, geometry_data->m_vertexBuffer, 0, geometry_data->m_vertexBuffer.GetSize());
  m_pass->SetIndexBuffer(geometry_data->m_indexBuffer, wgpu::IndexFormat::Uint32, 0, geometry_data->m_indexBuffer.GetSize());
  m_pass->SetPipeline(geometry_data->m_pipeline);

  const std::array offsets{m_uniformCurrentOffset};
  m_pass->SetBindGroup(0, m_CommonBindGroup, offsets.size(), offsets.data());

  m_pass->SetBindGroup(1, texture_data->m_bindGroup);

  m_pass->DrawIndexed(geometry_data->m_indexBuffer.GetSize() / sizeof(int));

  m_uniformCurrentOffset += AURORA_ALIGN(sizeof(UniformBlock), 64);
}

void WebGPURenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  auto* shader_data = (ShaderGeometryData*)geometry;
  delete shader_data;
}

Rml::TextureHandle WebGPURenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
  // load texels from image source
  const std::string resolved_source = ResolveTextureSource(source);
  Image image_data = GetImage(resolved_source);

  if (image_data.size == 0) {
    Log.error("Failed to load texture! Path: {}", resolved_source);
    return 0;
  }

  texture_dimensions.x = (int)image_data.width;
  texture_dimensions.y = (int)image_data.height;

  Rml::Span<const Rml::byte> tex_bytes(image_data.data.get(), image_data.size);
  return GenerateTexture(tex_bytes, texture_dimensions);
}

Rml::TextureHandle WebGPURenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) {
  auto* tex_data = new ShaderTextureData();

  // create texture
  {
    wgpu::TextureDescriptor tex_desc = {};
    tex_desc.label = "RmlUi Texture";
    tex_desc.dimension = wgpu::TextureDimension::e2D;
    tex_desc.size.width = source_dimensions.x;
    tex_desc.size.height = source_dimensions.y;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.sampleCount = 1;
    tex_desc.format = wgpu::TextureFormat::RGBA8Unorm;
    tex_desc.mipLevelCount = 1;
    tex_desc.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding;

    tex_data->m_texture = webgpu::g_device.CreateTexture(&tex_desc);
  }

  // create view
  {
    wgpu::TextureViewDescriptor tex_view_desc = {};
    tex_view_desc.format = wgpu::TextureFormat::RGBA8Unorm;
    tex_view_desc.dimension = wgpu::TextureViewDimension::e2D;
    tex_view_desc.baseMipLevel = 0;
    tex_view_desc.mipLevelCount = 1;
    tex_view_desc.baseArrayLayer = 0;
    tex_view_desc.arrayLayerCount = 1;
    tex_view_desc.aspect = wgpu::TextureAspect::All;
    tex_data->m_textureView = tex_data->m_texture.CreateView(&tex_view_desc);
  }

  // get texels from image source, load into texture
  {
    auto bytesPerPixel = 4;  // bytes per pixel is 4 since source data is always RGBA8

    wgpu::TexelCopyTextureInfo dst_view = {};
    dst_view.texture = tex_data->m_texture;
    dst_view.mipLevel = 0;
    dst_view.origin = { 0, 0, 0 };
    dst_view.aspect = wgpu::TextureAspect::All;
    wgpu::TexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = source_dimensions.x * bytesPerPixel;
    layout.rowsPerImage = source_dimensions.y;
    wgpu::Extent3D size = {
      (uint32_t)source_dimensions.x,
      (uint32_t)source_dimensions.y,
      1
    };
    webgpu::g_queue.WriteTexture(&dst_view, source.data(),
      (uint32_t)(source_dimensions.x * bytesPerPixel * source_dimensions.y), &layout, &size);
  }

  // create bind group
  {
    wgpu::BindGroupEntry image_bg_entries[] = { { nullptr, 0, nullptr, 0, 0, nullptr, tex_data->m_textureView } };

    wgpu::BindGroupDescriptor image_bg_descriptor = {};
    image_bg_descriptor.layout = m_ImageBindGroupLayout;
    image_bg_descriptor.entryCount = sizeof(image_bg_entries) / sizeof(wgpu::BindGroupEntry);
    image_bg_descriptor.entries = image_bg_entries;

    tex_data->m_bindGroup = webgpu::g_device.CreateBindGroup(&image_bg_descriptor);
  }

  return (Rml::TextureHandle)tex_data;
}

void WebGPURenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
  auto* shader_data = (ShaderTextureData*)texture;
  delete shader_data;
}

void WebGPURenderInterface::EnableScissorRegion(bool enable) {
  m_enableScissorRegion = enable;

  if (!m_enableScissorRegion) {
    m_pass->SetScissorRect(0, 0, m_windowSize.x, m_windowSize.y);
  }
}

void WebGPURenderInterface::SetScissorRegion(Rml::Rectanglei region) {
  if (m_enableScissorRegion) {
    m_pass->SetScissorRect(region.Left(), region.Top(), region.Width(), region.Height());
  }
}

void WebGPURenderInterface::SetTransform(const Rml::Matrix4f* transform) {
  if (transform == nullptr) {
    m_translationMatrix = Rml::Matrix4f::Identity();
  }else {
    m_translationMatrix = *transform;
  }
}

// code heavily based of imgui wgpu impl
void WebGPURenderInterface::CreateDeviceObjects() {
    // Create render pipeline
    wgpu::RenderPipelineDescriptor graphics_pipeline_desc = {};
    graphics_pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    graphics_pipeline_desc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    graphics_pipeline_desc.primitive.frontFace = wgpu::FrontFace::CW;
    graphics_pipeline_desc.primitive.cullMode = wgpu::CullMode::None;
    graphics_pipeline_desc.multisample = wgpu::MultisampleState{nullptr, 1, UINT32_MAX, false};

    // Bind group layouts
    wgpu::BindGroupLayoutEntry common_bg_layout_entries[2] = {};
    common_bg_layout_entries[0].binding = 0;
    common_bg_layout_entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    common_bg_layout_entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    common_bg_layout_entries[0].buffer.hasDynamicOffset = true;
    common_bg_layout_entries[1].binding = 1;
    common_bg_layout_entries[1].visibility = wgpu::ShaderStage::Fragment;
    common_bg_layout_entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutEntry image_bg_layout_entries[1] = {};
    image_bg_layout_entries[0].binding = 0;
    image_bg_layout_entries[0].visibility = wgpu::ShaderStage::Fragment;
    image_bg_layout_entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    image_bg_layout_entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    wgpu::BindGroupLayoutDescriptor common_bg_layout_desc = {};
    common_bg_layout_desc.entryCount = 2;
    common_bg_layout_desc.entries = common_bg_layout_entries;

    wgpu::BindGroupLayoutDescriptor image_bg_layout_desc = {};
    image_bg_layout_desc.entryCount = 1;
    image_bg_layout_desc.entries = image_bg_layout_entries;

    wgpu::BindGroupLayout bg_layouts[2];
    bg_layouts[0] = webgpu::g_device.CreateBindGroupLayout(&common_bg_layout_desc);
    bg_layouts[1] = webgpu::g_device.CreateBindGroupLayout(&image_bg_layout_desc);

    wgpu::PipelineLayoutDescriptor layout_desc = {};
    layout_desc.bindGroupLayoutCount = 2;
    layout_desc.bindGroupLayouts = bg_layouts;
    graphics_pipeline_desc.layout = webgpu::g_device.CreatePipelineLayout(&layout_desc);

    // Create the vertex shader
    wgpu::ComputeState vertex_shader_desc = CreateShaderModule(vtx_shader, "RmlUi Vertex Shader");
    graphics_pipeline_desc.vertex.module = vertex_shader_desc.module;
    graphics_pipeline_desc.vertex.entryPoint = vertex_shader_desc.entryPoint;

    // Vertex input configuration
    wgpu::VertexAttribute attribute_desc[] = {
        { nullptr, wgpu::VertexFormat::Float32x2, offsetof(Rml::Vertex, position), 0 },
{ nullptr, wgpu::VertexFormat::Float32x2,  offsetof(Rml::Vertex, tex_coord), 1 },
        { nullptr, wgpu::VertexFormat::Unorm8x4, offsetof(Rml::Vertex, colour),  2 },
    };

    wgpu::VertexBufferLayout buffer_layouts[1];
    buffer_layouts[0].arrayStride = sizeof(Rml::Vertex);
    buffer_layouts[0].stepMode = wgpu::VertexStepMode::Vertex;
    buffer_layouts[0].attributeCount = 3;
    buffer_layouts[0].attributes = attribute_desc;

    graphics_pipeline_desc.vertex.bufferCount = 1;
    graphics_pipeline_desc.vertex.buffers = buffer_layouts;

    // Create the pixel shader
    wgpu::ComputeState pixel_shader_desc = CreateShaderModule(frag_shader, "RmlUi Fragment Shader");

    // Create the blending setup
    wgpu::BlendState blend_state = {};
    blend_state.alpha.operation = wgpu::BlendOperation::Add;
    blend_state.alpha.srcFactor = wgpu::BlendFactor::One;
    blend_state.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend_state.color.operation = wgpu::BlendOperation::Add;
    blend_state.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend_state.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

    wgpu::ColorTargetState color_state = {};
    color_state.format = m_renderTargetFormat;
    color_state.blend = &blend_state;
    color_state.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragment_state = {};
    fragment_state.module = pixel_shader_desc.module;
    fragment_state.entryPoint = pixel_shader_desc.entryPoint;
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_state;

    graphics_pipeline_desc.fragment = &fragment_state;

    // Create depth-stencil State
    wgpu::DepthStencilState depth_stencil_state = {};
    depth_stencil_state.format = m_depthStencilFormat;
    depth_stencil_state.depthWriteEnabled = wgpu::OptionalBool::False;
    depth_stencil_state.depthCompare = wgpu::CompareFunction::Always;
    depth_stencil_state.stencilFront.compare = wgpu::CompareFunction::Always;
    depth_stencil_state.stencilFront.failOp = wgpu::StencilOperation::Keep;
    depth_stencil_state.stencilFront.depthFailOp = wgpu::StencilOperation::Keep;
    depth_stencil_state.stencilFront.passOp = wgpu::StencilOperation::Keep;
    depth_stencil_state.stencilBack.compare = wgpu::CompareFunction::Always;
    depth_stencil_state.stencilBack.failOp = wgpu::StencilOperation::Keep;
    depth_stencil_state.stencilBack.depthFailOp = wgpu::StencilOperation::Keep;
    depth_stencil_state.stencilBack.passOp = wgpu::StencilOperation::Keep;

    // Configure disabled depth-stencil state
    graphics_pipeline_desc.depthStencil = (m_depthStencilFormat == wgpu::TextureFormat::Undefined) ? nullptr :  &depth_stencil_state;

    m_pipeline = webgpu::g_device.CreateRenderPipeline(&graphics_pipeline_desc);

    // create sampler
    wgpu::SamplerDescriptor sampler_desc = {};
    sampler_desc.minFilter = wgpu::FilterMode::Linear;
    sampler_desc.magFilter = wgpu::FilterMode::Linear;
    sampler_desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    sampler_desc.addressModeU = wgpu::AddressMode::ClampToEdge;
    sampler_desc.addressModeV = wgpu::AddressMode::ClampToEdge;
    sampler_desc.addressModeW = wgpu::AddressMode::ClampToEdge;
    sampler_desc.maxAnisotropy = 1;
    m_sampler = webgpu::g_device.CreateSampler(&sampler_desc);

    CreateUniformBuffer();

    // Create resource bind group
    wgpu::BindGroupEntry common_bg_entries[] = {
        { nullptr, 0, m_uniformBuffer, 0, AURORA_ALIGN(sizeof(UniformBlock), 16), nullptr, nullptr },
        { nullptr, 1, nullptr, 0, 0, m_sampler, nullptr },
    };

    wgpu::BindGroupDescriptor common_bg_descriptor = {};
    common_bg_descriptor.layout = bg_layouts[0];
    common_bg_descriptor.entryCount = sizeof(common_bg_entries) / sizeof(wgpu::BindGroupEntry);
    common_bg_descriptor.entries = common_bg_entries;
    m_CommonBindGroup = webgpu::g_device.CreateBindGroup(&common_bg_descriptor);
    m_ImageBindGroupLayout = bg_layouts[1];

    // calc gamma for shader
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
    float L = 0;
    float R = m_windowSize.x;
    float T = 0;
    float B = m_windowSize.y;

    Rml::Matrix4f proj = Rml::Matrix4f::FromColumns(
    { 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
    { 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
    { 0.0f,         0.0f,           0.5f,       0.0f },
    { (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f }
    );

    UniformBlock ubo = {
      proj * m_translationMatrix,
      {translation.x, translation.y, 0.0f, 1.0f},
        m_gamma
    };

    webgpu::g_queue.WriteBuffer(m_uniformBuffer, m_uniformCurrentOffset, &ubo, sizeof(UniformBlock));

    // Setup blend factor
    wgpu::Color blend_color = { 0.f, 0.f, 0.f, 0.f };
    m_pass->SetBlendConstant(&blend_color);
}

void WebGPURenderInterface::CreateNullTexture() {
  const Rml::byte tex_bytes[] = {
    0xFF, 0xFF, 0xFF, 0xFF
  };
  Rml::Span tex(tex_bytes, sizeof(tex_bytes));

  m_nullTexture = GenerateTexture(tex, {1,1});
}

void WebGPURenderInterface::CreateUniformBuffer() {
  wgpu::BufferDescriptor ub_desc = {
    nullptr,
    "RmlUi Uniform Buffer",
      wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
    AURORA_ALIGN(UniformBufferSize, 16),
    false
  };
  m_uniformBuffer = webgpu::g_device.CreateBuffer(&ub_desc);
}

void WebGPURenderInterface::NewFrame() {
  m_uniformCurrentOffset = 0;
}

}

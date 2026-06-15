#include "WebGPURenderInterface.hpp"

#include "FileInterface_SDL.h"
#include "RuntimeTextureProvider.hpp"
#include "pipeline.hpp"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../logging.hpp"
#include "../webgpu/gpu.hpp"
#include "../gfx/texture.hpp"

namespace aurora::rmlui {
namespace {
Module Log("aurora::rmlui::RenderInterface");

constexpr size_t rmlBufferOffsetAlignment = 4;
constexpr float FilterEpsilon = 0.0001f;

struct Image {
  std::unique_ptr<uint8_t[]> data;
  size_t size;
  uint32_t width;
  uint32_t height;
};

struct ShaderGeometryData {
  std::vector<Rml::Vertex> vertices;
  std::vector<uint32_t> indices;
};

struct ShaderTextureData {
  wgpu::Texture m_texture;
  wgpu::TextureView m_textureView;
  std::vector<Rml::byte> m_pendingUpload;
  wgpu::Extent3D m_size{};
  uint32_t m_rowBytes = 0;
  bool m_uploaded = false;
};

struct CompiledShaderData {
  GradientUniformBlock gradient;
};

enum class FilterType {
  Opacity,
  Blur,
  DropShadow,
  ColorMatrix,
  MaskImage,
};

struct CompiledFilter {
  FilterType type = FilterType::Blur;
  float opacity = 1.f;
  float sigma = 0.f;
  Rml::Vector2f offset;
  Rml::ColourbPremultiplied color;
  Rml::Matrix4f colorMatrix;
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

bool is_identity_matrix(const Rml::Matrix4f& matrix) {
  const Rml::Matrix4f identity = Rml::Matrix4f::Identity();
  const auto* matrixData = matrix.data();
  const auto* identityData = identity.data();
  for (size_t i = 0; i < 16; ++i) {
    if (std::abs(matrixData[i] - identityData[i]) > FilterEpsilon) {
      return false;
    }
  }
  return true;
}

bool is_identity_filter(const CompiledFilter& filter) {
  switch (filter.type) {
  case FilterType::Opacity:
    return std::abs(filter.opacity - 1.f) <= FilterEpsilon;
  case FilterType::Blur:
    return filter.sigma < 0.5f;
  case FilterType::ColorMatrix:
    return is_identity_matrix(filter.colorMatrix);
  case FilterType::DropShadow:
  case FilterType::MaskImage:
  default:
    return false;
  }
}

std::vector<Rml::CompiledFilterHandle> active_filters(Rml::Span<const Rml::CompiledFilterHandle> filters) {
  std::vector<Rml::CompiledFilterHandle> activeFilters;
  activeFilters.reserve(filters.size());
  for (Rml::CompiledFilterHandle filterHandle : filters) {
    const auto* filter = reinterpret_cast<const CompiledFilter*>(filterHandle);
    if (filter != nullptr && !is_identity_filter(*filter)) {
      activeFilters.push_back(filterHandle);
    }
  }
  return activeFilters;
}

bool try_fold_simple_filters(Rml::Span<const Rml::CompiledFilterHandle> filters, Rml::Matrix4f& colorMatrix,
                             float& opacity) {
  colorMatrix = Rml::Matrix4f::Identity();
  opacity = 1.f;

  for (Rml::CompiledFilterHandle filterHandle : filters) {
    const auto* filter = reinterpret_cast<const CompiledFilter*>(filterHandle);
    if (filter == nullptr) {
      continue;
    }

    switch (filter->type) {
    case FilterType::Opacity:
      opacity *= filter->opacity;
      break;
    case FilterType::ColorMatrix:
      colorMatrix = filter->colorMatrix * colorMatrix;
      break;
    case FilterType::Blur:
    case FilterType::DropShadow:
    case FilterType::MaskImage:
    default:
      return false;
    }
  }

  return true;
}

Rml::Rectanglei downsample_scissor(Rml::Rectanglei scissor) {
  scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
  scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
  return scissor;
}

PipelineConfig make_pipeline_config(PipelineKind kind, wgpu::TextureFormat colorFormat, uint32_t sampleCount,
                                    VertexLayoutKind vertexLayout, StencilMode stencilMode, BlendMode blendMode,
                                    wgpu::ColorWriteMask colorWriteMask = wgpu::ColorWriteMask::All) {
  return {
      .kind = static_cast<uint32_t>(kind),
      .colorFormat = static_cast<uint32_t>(colorFormat),
      .sampleCount = sampleCount,
      .vertexLayout = static_cast<uint32_t>(vertexLayout),
      .stencilFormat = static_cast<uint32_t>(WebGPURenderInterface::ClipMaskStencilFormat),
      .stencilMode = static_cast<uint32_t>(stencilMode),
      .blendMode = static_cast<uint32_t>(blendMode),
      .colorWriteMask = static_cast<uint32_t>(colorWriteMask),
  };
}

gfx::PipelineRef geometry_pipeline(wgpu::TextureFormat colorFormat, uint32_t sampleCount,
                                   WebGPURenderInterface::PipelineType type) {
  StencilMode stencilMode = StencilMode::AlwaysKeep;
  auto colorWriteMask = wgpu::ColorWriteMask::All;
  switch (type) {
  case WebGPURenderInterface::PipelineType::Masked:
    stencilMode = StencilMode::EqualKeep;
    break;
  case WebGPURenderInterface::PipelineType::ClipReplace:
    stencilMode = StencilMode::ClipReplace;
    colorWriteMask = wgpu::ColorWriteMask::None;
    break;
  case WebGPURenderInterface::PipelineType::ClipIntersect:
    stencilMode = StencilMode::ClipIntersect;
    colorWriteMask = wgpu::ColorWriteMask::None;
    break;
  case WebGPURenderInterface::PipelineType::Normal:
  case WebGPURenderInterface::PipelineType::Count:
  default:
    break;
  }
  return gfx::pipeline_ref(make_pipeline_config(PipelineKind::Geometry, colorFormat, sampleCount,
                                                VertexLayoutKind::Geometry, stencilMode, BlendMode::Premultiplied,
                                                colorWriteMask));
}

gfx::PipelineRef gradient_pipeline(wgpu::TextureFormat colorFormat, uint32_t sampleCount, bool masked) {
  return gfx::pipeline_ref(
      make_pipeline_config(PipelineKind::Gradient, colorFormat, sampleCount, VertexLayoutKind::Geometry,
                           masked ? StencilMode::EqualKeep : StencilMode::AlwaysKeep, BlendMode::Premultiplied));
}

gfx::PipelineRef blit_pipeline(wgpu::TextureFormat colorFormat, uint32_t sampleCount,
                               WebGPURenderInterface::BlitPipelineType type, bool useStencil) {
  const bool blend = type == WebGPURenderInterface::BlitPipelineType::Blend ||
                     type == WebGPURenderInterface::BlitPipelineType::BlendMasked;
  const bool masked = type == WebGPURenderInterface::BlitPipelineType::BlendMasked ||
                      type == WebGPURenderInterface::BlitPipelineType::ReplaceMasked;
  return gfx::pipeline_ref(
      make_pipeline_config(PipelineKind::Blit, colorFormat, sampleCount, VertexLayoutKind::Fullscreen,
                           masked ? StencilMode::EqualKeep : (useStencil ? StencilMode::AlwaysKeep : StencilMode::None),
                           blend ? BlendMode::Premultiplied : BlendMode::None));
}

gfx::PipelineRef simple_filter_pipeline(wgpu::TextureFormat colorFormat, uint32_t sampleCount,
                                        WebGPURenderInterface::BlitPipelineType type, bool useStencil) {
  const bool blend = type == WebGPURenderInterface::BlitPipelineType::Blend ||
                     type == WebGPURenderInterface::BlitPipelineType::BlendMasked;
  const bool masked = type == WebGPURenderInterface::BlitPipelineType::BlendMasked ||
                      type == WebGPURenderInterface::BlitPipelineType::ReplaceMasked;
  return gfx::pipeline_ref(
      make_pipeline_config(PipelineKind::SimpleFilter, colorFormat, sampleCount, VertexLayoutKind::Fullscreen,
                           masked ? StencilMode::EqualKeep : (useStencil ? StencilMode::AlwaysKeep : StencilMode::None),
                           blend ? BlendMode::Premultiplied : BlendMode::None));
}

gfx::PipelineRef seed_resample_pipeline(wgpu::TextureFormat colorFormat, uint32_t sampleCount, bool useStencil) {
  return gfx::pipeline_ref(
      make_pipeline_config(PipelineKind::SeedResample, colorFormat, sampleCount, VertexLayoutKind::Fullscreen,
                           useStencil ? StencilMode::AlwaysKeep : StencilMode::None, BlendMode::None));
}

gfx::PipelineRef filter_pipeline(PipelineKind kind, wgpu::TextureFormat colorFormat,
                                 VertexLayoutKind vertexLayout = VertexLayoutKind::Fullscreen,
                                 BlendMode blendMode = BlendMode::None) {
  return gfx::pipeline_ref(make_pipeline_config(kind, colorFormat, 1, vertexLayout, StencilMode::None, blendMode));
}

void queue_texture_upload_if_needed(ShaderTextureData& texture) {
  if (texture.m_uploaded || texture.m_pendingUpload.empty()) {
    return;
  }
  const wgpu::TexelCopyTextureInfo dst{
      .texture = texture.m_texture,
      .aspect = wgpu::TextureAspect::All,
  };
  gfx::queue_texture_upload_data(texture.m_pendingUpload.data(), texture.m_rowBytes, texture.m_size.height, dst,
                                 texture.m_size);
  texture.m_pendingUpload.clear();
  texture.m_pendingUpload.shrink_to_fit();
  texture.m_uploaded = true;
}

} // namespace

Rml::CompiledGeometryHandle WebGPURenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                   Rml::Span<const int> indices) {
  auto* geometryData = new ShaderGeometryData();
  geometryData->vertices.assign(vertices.begin(), vertices.end());
  geometryData->indices.reserve(indices.size());
  for (const int index : indices) {
    geometryData->indices.push_back(static_cast<uint32_t>(index));
  }

  return reinterpret_cast<Rml::CompiledGeometryHandle>(geometryData);
}

void WebGPURenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                           Rml::TextureHandle texture) {
  DrawGeometry(geometry, translation, texture,
               geometry_pipeline(m_renderTargetFormat, LayerSampleCount,
                                 m_clipMaskEnabled ? PipelineType::Masked : PipelineType::Normal));
}

void WebGPURenderInterface::DrawGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                         Rml::TextureHandle texture, gfx::PipelineRef pipeline) {
  EnsureActiveLayerPass("RmlUi resumed geometry layer pass");
  if (!m_passActive) {
    return;
  }

  auto* geometryData = reinterpret_cast<ShaderGeometryData*>(geometry);
  auto* textureData = reinterpret_cast<ShaderTextureData*>(texture != 0 ? texture : m_nullTexture);
  if (geometryData == nullptr || textureData == nullptr) {
    return;
  }
  queue_texture_upload_if_needed(*textureData);

  const auto uniformRange = SetupRenderState(translation);
  const auto vertexRange =
      gfx::push_verts(reinterpret_cast<const uint8_t*>(geometryData->vertices.data()),
                      geometryData->vertices.size() * sizeof(Rml::Vertex), rmlBufferOffsetAlignment);
  const auto indexRange = gfx::push_indices(reinterpret_cast<const uint8_t*>(geometryData->indices.data()),
                                            geometryData->indices.size() * sizeof(uint32_t), rmlBufferOffsetAlignment);
  gfx::push_draw_command(DrawData{
      .pipeline = pipeline,
      .vertexRange = vertexRange,
      .indexRange = indexRange,
      .uniformRange = uniformRange,
      .bindGroup1 = texture_bind_group_ref(textureData->m_textureView),
      .drawKind = static_cast<uint32_t>(DrawKind::Geometry),
      .indexCount = static_cast<uint32_t>(geometryData->indices.size()),
      .stencilRef = m_stencilRef,
      .blendConstant = {0.f, 0.f, 0.f, 0.f},
      .hasBlendConstant = 1,
  });
}

void WebGPURenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  delete reinterpret_cast<ShaderGeometryData*>(geometry);
}

Rml::TextureHandle WebGPURenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
  if (const auto runtimeTexture = load_runtime_texture(source)) {
    const size_t size = static_cast<size_t>(runtimeTexture->width) * static_cast<size_t>(runtimeTexture->height) * 4;
    if (runtimeTexture->width == 0 || runtimeTexture->height == 0 || runtimeTexture->rgba8.size() < size) {
      Log.error("Runtime texture provider returned invalid texture! Path: {}", source);
      return 0;
    }

    const auto* texels = reinterpret_cast<const Rml::byte*>(runtimeTexture->rgba8.data());
    std::vector<Rml::byte> premultiplied;
    if (!runtimeTexture->premultipliedAlpha) {
      premultiplied.assign(texels, texels + size);
      for (size_t offset = 0; offset < premultiplied.size(); offset += 4) {
        const uint8_t alpha = premultiplied[offset + 3];
        for (size_t channel = 0; channel < 3; ++channel) {
          premultiplied[offset + channel] = static_cast<uint8_t>(
              (static_cast<uint32_t>(premultiplied[offset + channel]) * static_cast<uint32_t>(alpha)) / 255);
        }
      }
      texels = premultiplied.data();
    }

    dimensions.x = static_cast<int>(runtimeTexture->width);
    dimensions.y = static_cast<int>(runtimeTexture->height);
    return GenerateTexture({texels, size}, dimensions);
  }

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
  texData->m_size = size;
  texData->m_rowBytes = static_cast<uint32_t>(source_dimensions.x) * BytesPerPixel;
  texData->m_pendingUpload.assign(source.begin(), source.end());
  texData->m_uploaded = texData->m_pendingUpload.empty();
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
  if (!m_passActive) {
    return;
  }

  ApplyScissorRegion(GetActiveScissorRegion());
}

void WebGPURenderInterface::ApplyScissorRegion(Rml::Rectanglei region) const {
  const int maxWidth = static_cast<int>(m_frameSize.width);
  const int maxHeight = static_cast<int>(m_frameSize.height);
  const uint32_t x = static_cast<uint32_t>(std::clamp(region.Left(), 0, maxWidth));
  const uint32_t y = static_cast<uint32_t>(std::clamp(region.Top(), 0, maxHeight));
  const uint32_t width = static_cast<uint32_t>(std::clamp(region.Width(), 0, maxWidth - static_cast<int>(x)));
  const uint32_t height = static_cast<uint32_t>(std::clamp(region.Height(), 0, maxHeight - static_cast<int>(y)));
  gfx::set_scissor(
      {static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(width), static_cast<int32_t>(height)});
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
  }
}

void WebGPURenderInterface::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                                             Rml::Vector2f translation) {
  EnsureActiveLayerPass("RmlUi resumed clip mask layer pass");
  if (!m_passActive) {
    return;
  }

  EnsureClipResetGeometry();

  const Rml::Matrix4f prevMatrix = m_translationMatrix;
  switch (operation) {
  case Rml::ClipMaskOperation::Set:
    m_translationMatrix = Rml::Matrix4f::Identity();
    m_stencilRef = 0;
    DrawGeometry(m_clipResetGeometry, {}, 0,
                 geometry_pipeline(m_renderTargetFormat, LayerSampleCount, PipelineType::ClipReplace));
    m_translationMatrix = prevMatrix;

    m_stencilRef = 1;
    DrawGeometry(geometry, translation, 0,
                 geometry_pipeline(m_renderTargetFormat, LayerSampleCount, PipelineType::ClipReplace));
    break;
  case Rml::ClipMaskOperation::SetInverse:
    m_translationMatrix = Rml::Matrix4f::Identity();
    m_stencilRef = 1;
    DrawGeometry(m_clipResetGeometry, {}, 0,
                 geometry_pipeline(m_renderTargetFormat, LayerSampleCount, PipelineType::ClipReplace));
    m_translationMatrix = prevMatrix;

    m_stencilRef = 1;
    m_stencilRef = 0;
    DrawGeometry(geometry, translation, 0,
                 geometry_pipeline(m_renderTargetFormat, LayerSampleCount, PipelineType::ClipReplace));
    m_stencilRef = 1;
    break;
  case Rml::ClipMaskOperation::Intersect:
    if (m_stencilRef == std::numeric_limits<uint8_t>::max()) {
      Log.warn("RmlUi clip mask nesting exceeded stencil capacity; further nested clipping may be incorrect");
      break;
    }

    DrawGeometry(geometry, translation, 0,
                 geometry_pipeline(m_renderTargetFormat, LayerSampleCount, PipelineType::ClipIntersect));
    ++m_stencilRef;
    break;
  }
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

void WebGPURenderInterface::ApplyViewport() const { gfx::set_viewport(m_viewport); }

void WebGPURenderInterface::ApplyFullFrameScissor() const {
  gfx::set_scissor({0, 0, static_cast<int32_t>(m_frameSize.width), static_cast<int32_t>(m_frameSize.height)});
}

void WebGPURenderInterface::BeginRenderTargetPass(const wgpu::TextureView& view, wgpu::LoadOp loadOp, const char* label,
                                                  bool clearStencil) {
  EndActivePass();
  gfx::begin_color_pass({
      .label = label,
      .colorView = view,
      .depthStencilView = clearStencil ? GetClipMaskStencilView(m_frameSize) : wgpu::TextureView{},
      .targetSize = m_frameSize,
      .sampleCount = 1,
      .colorLoadOp = loadOp,
      .colorStoreOp = wgpu::StoreOp::Store,
      .clearColor = {0.f, 0.f, 0.f, 0.f},
      .hasStencil = clearStencil,
      .stencilLoadOp = clearStencil ? wgpu::LoadOp::Clear : wgpu::LoadOp::Undefined,
      .stencilStoreOp = clearStencil ? wgpu::StoreOp::Store : wgpu::StoreOp::Undefined,
      .stencilClearValue = 0,
      .observable = true,
  });
  m_passActive = true;
  ApplyViewport();
  ApplyScissorRegion();
}

void WebGPURenderInterface::BeginLayerPass(Rml::LayerHandle layer, wgpu::LoadOp loadOp, const char* label,
                                           bool clearStencil, bool resolveMultisampled) {
  m_activeLayer = layer;
  EndActivePass();

  const RenderTarget& target = m_layers[layer];
  const bool multisampled = LayerSampleCount > 1 && static_cast<bool>(target.multisampleView);
  gfx::begin_color_pass({
      .label = label,
      .colorView = multisampled ? target.multisampleView : target.view,
      .resolveView = multisampled && resolveMultisampled ? target.view : wgpu::TextureView{},
      .depthStencilView = GetClipMaskStencilView(m_frameSize),
      .targetSize = m_frameSize,
      .sampleCount = multisampled ? LayerSampleCount : 1,
      .colorLoadOp = loadOp,
      .colorStoreOp = wgpu::StoreOp::Store,
      .clearColor = {0.f, 0.f, 0.f, 0.f},
      .hasStencil = true,
      .stencilLoadOp = clearStencil ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
      .stencilStoreOp = wgpu::StoreOp::Store,
      .stencilClearValue = 0,
      .observable = true,
  });
  m_passActive = true;
  ApplyViewport();
  ApplyScissorRegion();
}

void WebGPURenderInterface::EnsureFrameRenderingStarted() {
  if (m_frameRenderingStarted || !m_frameActive || m_layers.empty() || !m_layers[0].view) {
    return;
  }

  m_frameRenderingStarted = true;
  if (m_baseLayerContent == BaseLayerContent::Transparent) {
    BeginLayerPass(0, wgpu::LoadOp::Clear, "RmlUi transparent base layer pass", true);
    return;
  }

  const auto seedBindGroup = texture_bind_group_ref(m_frameSeedView);
  const auto seedUniformRange = gfx::push_uniform(SeedResampleUniformBlock{
      .samplerMode = sampler_mode(),
      .frameWidth = static_cast<float>(m_frameSize.width),
      .frameHeight = static_cast<float>(m_frameSize.height),
  });

  if (m_layers[0].multisampleView) {
    BeginLayerPass(0, wgpu::LoadOp::Clear, "RmlUi base layer seed pass", true);
    ApplyFullFrameScissor();
    DrawFullscreenTexture(seedBindGroup, seed_resample_pipeline(m_renderTargetFormat, LayerSampleCount, true),
                          uniform_bind_group_ref(), seedUniformRange);
    BeginLayerPass(0, wgpu::LoadOp::Load, "RmlUi base layer pass");
  } else {
    BeginRenderTargetPass(m_layers[0].view, wgpu::LoadOp::Clear, "RmlUi game frame copy pass");
    ApplyFullFrameScissor();
    DrawFullscreenTexture(seedBindGroup, seed_resample_pipeline(m_renderTargetFormat, 1, false),
                          uniform_bind_group_ref(), seedUniformRange);
    BeginLayerPass(0, wgpu::LoadOp::Load, "RmlUi base layer pass", true);
  }
}

void WebGPURenderInterface::EnsureActiveLayerPass(const char* label) {
  EnsureFrameRenderingStarted();
  if (m_passActive || !m_frameActive || m_activeLayer >= m_layers.size() || !m_layers[m_activeLayer].view) {
    return;
  }

  BeginLayerPass(m_activeLayer, wgpu::LoadOp::Load, label);
}

void WebGPURenderInterface::EndActivePass() {
  if (m_passActive) {
    gfx::end_color_pass();
    m_passActive = false;
  }
}

void WebGPURenderInterface::DrawFullscreenTexture(gfx::BindGroupRef bindGroup, gfx::PipelineRef pipeline,
                                                  gfx::BindGroupRef extraBindGroup, gfx::Range extraUniformRange,
                                                  bool extraBindGroupHasDynamicOffset,
                                                  std::array<float, 4> blendConstant, bool hasBlendConstant) {
  if (!m_passActive) {
    return;
  }

  gfx::push_draw_command(DrawData{
      .pipeline = pipeline,
      .uniformRange = {},
      .bindGroup1 = bindGroup,
      .bindGroup2 = extraBindGroup,
      .bindGroup2DynamicOffset = extraUniformRange.offset,
      .dynamicBindGroupMask = extraBindGroup != 0 && extraBindGroupHasDynamicOffset ? (1u << 2u) : 0u,
      .drawKind = static_cast<uint32_t>(DrawKind::Fullscreen),
      .vertexCount = 3,
      .stencilRef = m_stencilRef,
      .blendConstant = blendConstant,
      .hasBlendConstant = hasBlendConstant ? 1u : 0u,
  });
}

void WebGPURenderInterface::CompositeToTarget(gfx::BindGroupRef bindGroup, const wgpu::TextureView& view,
                                              wgpu::LoadOp loadOp, gfx::PipelineRef pipeline, const char* label,
                                              gfx::BindGroupRef extraBindGroup, gfx::Range extraUniformRange,
                                              bool extraBindGroupHasDynamicOffset, std::array<float, 4> blendConstant,
                                              bool hasBlendConstant) {
  BeginRenderTargetPass(view, loadOp, label);
  DrawFullscreenTexture(bindGroup, pipeline, extraBindGroup, extraUniformRange, extraBindGroupHasDynamicOffset,
                        blendConstant, hasBlendConstant);
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
    const TexCoordLimits texCoordLimits = GetPostprocessTexCoordLimits(texCoordRegion);
    const BlurUniformBlock uniform{
        .texelOffset = texelOffset,
        .radius = radius,
        .padding = 0.f,
        .texCoordMin = texCoordLimits.min,
        .texCoordMax = texCoordLimits.max,
        .weights = weights,
    };
    return gfx::push_uniform(uniform);
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
    return gfx::push_uniform(uniform);
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
    gfx::set_viewport({m_viewport.left, m_viewport.top, std::max(m_viewport.width * 0.5f, 1.f),
                       std::max(m_viewport.height * 0.5f, 1.f), m_viewport.znear, m_viewport.zfar});
    ApplyScissorRegion(scissor);
    DrawFullscreenTexture(texture_bind_group_ref(source.view),
                          blit_pipeline(m_renderTargetFormat, 1, BlitPipelineType::Replace, false));
  }

  if ((passLevel % 2) == 0) {
    BeginRenderTargetPass(temp.view, wgpu::LoadOp::Clear, "RmlUi blur transfer pass");
    ApplyViewport();
    ApplyScissorRegion(scissor);
    DrawFullscreenTexture(texture_bind_group_ref(sourceDestination.view),
                          blit_pipeline(m_renderTargetFormat, 1, BlitPipelineType::Replace, false));
  }

  const auto verticalRange =
      write_blur_uniform({0.f, 1.f / std::max(m_viewport.height, 1.f)}, scissor, radius, weights);
  BeginRenderTargetPass(sourceDestination.view, wgpu::LoadOp::Clear, "RmlUi vertical blur pass");
  ApplyScissorRegion(scissor);
  DrawFullscreenTexture(texture_bind_group_ref(temp.view),
                        filter_pipeline(PipelineKind::Blur, m_renderTargetFormat, VertexLayoutKind::BlurFullscreen),
                        uniform_bind_group_ref(), verticalRange);

  const auto horizontalRange =
      write_blur_uniform({1.f / std::max(m_viewport.width, 1.f), 0.f}, scissor, radius, weights);
  BeginRenderTargetPass(temp.view, wgpu::LoadOp::Clear, "RmlUi horizontal blur pass");
  ApplyScissorRegion(scissor);
  DrawFullscreenTexture(texture_bind_group_ref(sourceDestination.view),
                        filter_pipeline(PipelineKind::Blur, m_renderTargetFormat, VertexLayoutKind::BlurFullscreen),
                        uniform_bind_group_ref(), horizontalRange);

  const auto upscaleRange = write_region_blit_uniform(scissor, weights);
  BeginRenderTargetPass(sourceDestination.view, wgpu::LoadOp::Clear, "RmlUi blur upscale pass");
  gfx::set_viewport({static_cast<float>(originalScissor.Left()), static_cast<float>(originalScissor.Top()),
                     static_cast<float>(std::max(originalScissor.Width(), 1)),
                     static_cast<float>(std::max(originalScissor.Height(), 1)), 0.f, 1.f});
  ApplyScissorRegion(originalScissor);
  DrawFullscreenTexture(texture_bind_group_ref(temp.view),
                        filter_pipeline(PipelineKind::RegionBlit, m_renderTargetFormat), uniform_bind_group_ref(),
                        upscaleRange);

  const auto targetMin = scissor.p0 * (1 << passLevel);
  const auto targetMax = scissor.p1 * (1 << passLevel);
  const auto targetRegion = Rml::Rectanglei::FromCorners(targetMin, targetMax);
  if (targetRegion.p0 != originalScissor.p0 || targetRegion.p1 != originalScissor.p1) {
    BeginRenderTargetPass(sourceDestination.view, wgpu::LoadOp::Load, "RmlUi blur power-of-two upscale pass");
    gfx::set_viewport({static_cast<float>(targetRegion.Left()), static_cast<float>(targetRegion.Top()),
                       static_cast<float>(std::max(targetRegion.Width(), 1)),
                       static_cast<float>(std::max(targetRegion.Height(), 1)), 0.f, 1.f});
    ApplyScissorRegion(targetRegion);
    DrawFullscreenTexture(texture_bind_group_ref(temp.view),
                          filter_pipeline(PipelineKind::RegionBlit, m_renderTargetFormat), uniform_bind_group_ref(),
                          upscaleRange);
  }
}

size_t WebGPURenderInterface::RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filters) {
  size_t sourceIndex = 0;
  constexpr size_t tempIndex = 2;

  for (Rml::CompiledFilterHandle filterHandle : filters) {
    const auto* filter = reinterpret_cast<const CompiledFilter*>(filterHandle);
    if (filter == nullptr) {
      continue;
    }
    const size_t scratchIndex = sourceIndex == 0 ? 1 : 0;

    switch (filter->type) {
    case FilterType::Opacity: {
      const SimpleFilterUniformBlock simpleFilterUniform{
          .matrix = to_shader_matrix(Rml::Matrix4f::Identity()),
          .opacity = {filter->opacity, 0.f, 0.f, 0.f},
      };
      const auto simpleFilterRange = gfx::push_uniform(simpleFilterUniform);
      BeginRenderTargetPass(m_postprocessTargets[scratchIndex].view, wgpu::LoadOp::Clear, "RmlUi opacity pass");
      DrawFullscreenTexture(texture_bind_group_ref(m_postprocessTargets[sourceIndex].view),
                            filter_pipeline(PipelineKind::SimpleFilter, m_renderTargetFormat), uniform_bind_group_ref(),
                            simpleFilterRange);
      sourceIndex = scratchIndex;
      break;
    }
    case FilterType::Blur:
      RenderBlur(filter->sigma, m_postprocessTargets[sourceIndex], m_postprocessTargets[tempIndex]);
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
      const auto dropShadowRange = gfx::push_uniform(dropShadowUniform);
      CompositeToTarget(texture_bind_group_ref(m_postprocessTargets[sourceIndex].view),
                        m_postprocessTargets[scratchIndex].view, wgpu::LoadOp::Clear,
                        filter_pipeline(PipelineKind::DropShadow, m_renderTargetFormat), "RmlUi drop shadow pass",
                        uniform_bind_group_ref(), dropShadowRange);

      RenderBlur(filter->sigma, m_postprocessTargets[scratchIndex], m_postprocessTargets[tempIndex]);

      CompositeToTarget(texture_bind_group_ref(m_postprocessTargets[sourceIndex].view),
                        m_postprocessTargets[scratchIndex].view, wgpu::LoadOp::Load,
                        blit_pipeline(m_renderTargetFormat, 1, BlitPipelineType::Blend, false),
                        "RmlUi drop shadow source composite pass");
      sourceIndex = scratchIndex;
      break;
    }
    case FilterType::ColorMatrix: {
      const SimpleFilterUniformBlock simpleFilterUniform{
          .matrix = to_shader_matrix(filter->colorMatrix),
          .opacity = {1.f, 0.f, 0.f, 0.f},
      };
      const auto simpleFilterRange = gfx::push_uniform(simpleFilterUniform);

      CompositeToTarget(texture_bind_group_ref(m_postprocessTargets[sourceIndex].view),
                        m_postprocessTargets[scratchIndex].view, wgpu::LoadOp::Clear,
                        filter_pipeline(PipelineKind::SimpleFilter, m_renderTargetFormat), "RmlUi color matrix pass",
                        uniform_bind_group_ref(), simpleFilterRange);
      sourceIndex = scratchIndex;
      break;
    }
    case FilterType::MaskImage: {
      CompositeToTarget(texture_bind_group_ref(m_postprocessTargets[sourceIndex].view),
                        m_postprocessTargets[scratchIndex].view, wgpu::LoadOp::Clear,
                        filter_pipeline(PipelineKind::MaskImage, m_renderTargetFormat), "RmlUi mask image pass",
                        texture_bind_group_ref(m_blendMaskTarget.view), {}, false);
      sourceIndex = scratchIndex;
      break;
    }
    }
  }

  EndActivePass();
  return sourceIndex;
}

void WebGPURenderInterface::BeginFrame(const webgpu::TextureWithSampler& target,
                                       const webgpu::TextureWithSampler& sceneTarget,
                                       BaseLayerContent baseLayerContent) {
  m_frameSeedView = sceneTarget.view;
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
  m_frameActive = true;
  m_passActive = false;
  m_baseLayerContent = baseLayerContent;

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
  m_frameActive = false;
  m_frameSeedView = {};
  m_frameRenderingStarted = false;
  m_baseLayerContent = BaseLayerContent::Transparent;
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
  const Rml::LayerHandle topLayer = m_layerStack.empty() ? 0 : m_layerStack.back();
  const std::vector<Rml::CompiledFilterHandle> activeFilterHandles = active_filters(filters);
  const Rml::Span activeFilters{activeFilterHandles.data(), activeFilterHandles.size()};

  const bool replace = blendMode == Rml::BlendMode::Replace;
  const BlitPipelineType pipelineType =
      replace ? (m_clipMaskEnabled ? BlitPipelineType::ReplaceMasked : BlitPipelineType::Replace)
              : (m_clipMaskEnabled ? BlitPipelineType::BlendMasked : BlitPipelineType::Blend);
  if (activeFilters.empty() && source != destination) {
    BeginLayerPass(destination, wgpu::LoadOp::Load, "RmlUi layer direct composite pass");
    DrawFullscreenTexture(texture_bind_group_ref(m_layers[source].view),
                          blit_pipeline(m_renderTargetFormat, LayerSampleCount, pipelineType, true));

    if (destination != topLayer) {
      EndActivePass();
      m_activeLayer = topLayer;
    }
    return;
  }

  Rml::Matrix4f simpleFilterMatrix;
  float simpleFilterOpacity = 1.f;
  if (source != destination && try_fold_simple_filters(activeFilters, simpleFilterMatrix, simpleFilterOpacity)) {
    const SimpleFilterUniformBlock simpleFilterUniform{
        .matrix = to_shader_matrix(simpleFilterMatrix),
        .opacity = {simpleFilterOpacity, 0.f, 0.f, 0.f},
    };
    const auto simpleFilterRange = gfx::push_uniform(simpleFilterUniform);
    BeginLayerPass(destination, wgpu::LoadOp::Load, "RmlUi simple filter composite pass");
    DrawFullscreenTexture(texture_bind_group_ref(m_layers[source].view),
                          simple_filter_pipeline(m_renderTargetFormat, LayerSampleCount, pipelineType, true),
                          uniform_bind_group_ref(), simpleFilterRange);

    if (destination != topLayer) {
      EndActivePass();
      m_activeLayer = topLayer;
    }
    return;
  }

  CompositeToTarget(texture_bind_group_ref(m_layers[source].view), m_postprocessTargets[0].view, wgpu::LoadOp::Clear,
                    blit_pipeline(m_renderTargetFormat, 1, BlitPipelineType::Replace, false), "RmlUi layer copy pass");
  const size_t filteredIndex = RenderFilters(activeFilters);

  BeginLayerPass(destination, wgpu::LoadOp::Load, "RmlUi layer composite pass");
  DrawFullscreenTexture(texture_bind_group_ref(m_postprocessTargets[filteredIndex].view),
                        blit_pipeline(m_renderTargetFormat, LayerSampleCount, pipelineType, true));

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
  if (!m_frameActive) {
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
  texData->m_size = textureSize;
  texData->m_rowBytes = textureSize.width * 4;
  texData->m_uploaded = true;

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
  gfx::queue_texture_copy(src, dst, textureSize);

  BeginLayerPass(layer, wgpu::LoadOp::Load, "RmlUi saved layer restore pass");
  return reinterpret_cast<Rml::TextureHandle>(texData);
}

Rml::CompiledFilterHandle WebGPURenderInterface::SaveLayerAsMaskImage() {
  if (!m_frameActive) {
    Log.warn("RmlUi requested SaveLayerAsMaskImage outside a frame");
    return {};
  }

  const Rml::LayerHandle layer = m_layerStack.empty() ? m_activeLayer : m_layerStack.back();
  if (layer >= m_layers.size() || !m_layers[layer].texture) {
    Log.warn("RmlUi requested SaveLayerAsMaskImage with invalid layer handle {}", layer);
    return {};
  }

  EnsureFrameRenderingStarted();

  CompositeToTarget(texture_bind_group_ref(m_layers[layer].view), m_postprocessTargets[0].view, wgpu::LoadOp::Clear,
                    blit_pipeline(m_renderTargetFormat, 1, BlitPipelineType::Replace, false),
                    "RmlUi mask source copy pass");
  CompositeToTarget(texture_bind_group_ref(m_postprocessTargets[0].view), m_blendMaskTarget.view, wgpu::LoadOp::Clear,
                    blit_pipeline(m_renderTargetFormat, 1, BlitPipelineType::Replace, false),
                    "RmlUi mask image save pass");

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
  const auto* shaderData = reinterpret_cast<const CompiledShaderData*>(shader);
  const auto* geometryData = reinterpret_cast<const ShaderGeometryData*>(geometry);
  if (shaderData == nullptr || geometryData == nullptr) {
    return;
  }
  EnsureActiveLayerPass("RmlUi resumed shader layer pass");
  if (!m_passActive) {
    return;
  }

  const auto uniformRange = SetupRenderState(translation);
  const auto shaderRange = gfx::push_uniform(shaderData->gradient);
  const auto vertexRange =
      gfx::push_verts(reinterpret_cast<const uint8_t*>(geometryData->vertices.data()),
                      geometryData->vertices.size() * sizeof(Rml::Vertex), rmlBufferOffsetAlignment);
  const auto indexRange = gfx::push_indices(reinterpret_cast<const uint8_t*>(geometryData->indices.data()),
                                            geometryData->indices.size() * sizeof(uint32_t), rmlBufferOffsetAlignment);
  gfx::push_draw_command(DrawData{
      .pipeline = gradient_pipeline(m_renderTargetFormat, LayerSampleCount, m_clipMaskEnabled),
      .vertexRange = vertexRange,
      .indexRange = indexRange,
      .uniformRange = uniformRange,
      .bindGroup1 = uniform_bind_group_ref(),
      .bindGroup1DynamicOffset = shaderRange.offset,
      .dynamicBindGroupMask = 1u << 1u,
      .drawKind = static_cast<uint32_t>(DrawKind::Geometry),
      .indexCount = static_cast<uint32_t>(geometryData->indices.size()),
      .stencilRef = m_stencilRef,
      .blendConstant = {0.f, 0.f, 0.f, 0.f},
      .hasBlendConstant = 1,
  });
}

void WebGPURenderInterface::ReleaseShader(Rml::CompiledShaderHandle shader) {
  delete reinterpret_cast<CompiledShaderData*>(shader);
}

void WebGPURenderInterface::CreateDeviceObjects() {
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

gfx::Range WebGPURenderInterface::SetupRenderState(const Rml::Vector2f& translation) {
  const float L = 0.f;
  const float R = static_cast<float>(std::max(m_windowSize.x, 1));
  const float T = 0.f;
  const float B = static_cast<float>(std::max(m_windowSize.y, 1));
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
  return gfx::push_uniform(ubo);
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

void WebGPURenderInterface::NewFrame() {
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

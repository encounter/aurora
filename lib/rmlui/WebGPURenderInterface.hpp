#pragma once
#include <array>
#include <vector>

#include <dawn/webgpu_cpp.h>

#include "../gfx/clear.hpp"
#include "RmlUi/Core/RenderInterface.h"

namespace aurora::webgpu {
struct TextureWithSampler;
} // namespace aurora::webgpu

namespace aurora::rmlui {

inline constexpr bool EnableMsaa = false;
inline constexpr uint32_t LayerSampleCount = EnableMsaa ? 4 : 1;
inline constexpr uint32_t MaxBlurRadius = 3;
inline constexpr size_t MaxGradientStops = 16;
inline constexpr size_t GradientStopPositionGroupCount = (MaxGradientStops + 3) / 4;

struct UniformBlock {
  Rml::Matrix4f MVP;
  Rml::Vector4f translation;
  float Gamma;
};

struct BlurUniformBlock {
  Rml::Vector2f texelOffset;
  float radius;
  float padding;
  Rml::Vector2f texCoordMin;
  Rml::Vector2f texCoordMax;
  Rml::Vector4f weights;
};

struct DropShadowUniformBlock {
  Rml::Vector4f color;
  Rml::Vector2f uvOffset;
  Rml::Vector2f texCoordMin;
  Rml::Vector2f texCoordMax;
};

struct SimpleFilterUniformBlock {
  Rml::ColumnMajorMatrix4f matrix;
  Rml::Vector4f opacity;
};

struct GradientUniformBlock {
  int32_t function;
  int32_t numStops;
  Rml::Vector2f p;
  Rml::Vector2f v;
  Rml::Vector2f padding;
  std::array<Rml::Vector4f, MaxGradientStops> stopColors;
  std::array<Rml::Vector4f, GradientStopPositionGroupCount> stopPositions;
};

struct TexCoordLimits {
  Rml::Vector2f min;
  Rml::Vector2f max;
};

enum class BaseLayerContent {
  Transparent,
  Scene,
};

class WebGPURenderInterface : public Rml::RenderInterface {
public:
  static constexpr wgpu::TextureFormat ClipMaskStencilFormat = wgpu::TextureFormat::Stencil8;

  enum class PipelineType {
    Normal,
    Masked,
    ClipReplace,
    ClipIntersect,
    Count,
  };

  enum class BlitPipelineType {
    Blend,
    BlendMasked,
    Replace,
    ReplaceMasked,
    Count,
  };

private:
  struct RenderTarget {
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::Texture multisampleTexture;
    wgpu::TextureView multisampleView;
    wgpu::Extent3D size;
  };

  wgpu::TextureView m_frameSeedView;

  wgpu::TextureFormat m_renderTargetFormat = wgpu::TextureFormat::Undefined;
  wgpu::Texture m_clipMaskStencilTexture;
  wgpu::TextureView m_clipMaskStencilView;
  wgpu::Extent3D m_clipMaskStencilSize{};
  wgpu::Extent3D m_frameSize{};
  gfx::Viewport m_viewport{};

  Rml::TextureHandle m_nullTexture = 0;
  Rml::CompiledGeometryHandle m_clipResetGeometry = 0;
  Rml::Vector2i m_clipResetGeometrySize{};
  std::vector<RenderTarget> m_layers;
  std::array<RenderTarget, 3> m_postprocessTargets{};
  RenderTarget m_blendMaskTarget{};
  std::vector<Rml::LayerHandle> m_layerStack;
  Rml::LayerHandle m_activeLayer = 0;
  Rml::LayerHandle m_nextLayer = 1;
  BaseLayerContent m_baseLayerContent = BaseLayerContent::Transparent;

  Rml::Vector2i m_windowSize{};
  Rml::Matrix4f m_translationMatrix = Rml::Matrix4f::Identity();
  Rml::Rectanglei m_scissorRegion{};

  float m_gamma = 0.0f;
  bool m_enableScissorRegion = false;
  bool m_clipMaskEnabled = false;
  bool m_frameActive = false;
  bool m_passActive = false;
  bool m_frameRenderingStarted = false;
  uint32_t m_stencilRef = 0;

  gfx::Range SetupRenderState(const Rml::Vector2f& translation);

  void EnsureRenderTarget(RenderTarget& target, const char* label, const wgpu::Extent3D& size,
                          bool multisampled = false);
  void EnsureFrameTargets(const wgpu::Extent3D& size);
  Rml::Rectanglei GetActiveScissorRegion() const;
  TexCoordLimits GetPostprocessTexCoordLimits() const;
  TexCoordLimits GetPostprocessTexCoordLimits(Rml::Rectanglei region) const;
  void BeginRenderTargetPass(const wgpu::TextureView& view, wgpu::LoadOp loadOp, const char* label,
                             bool clearStencil = false);
  void BeginLayerPass(Rml::LayerHandle layer, wgpu::LoadOp loadOp, const char* label, bool clearStencil = false,
                      bool resolveMultisampled = true);
  void EnsureFrameRenderingStarted();
  void EnsureActiveLayerPass(const char* label);
  void EndActivePass();
  void ApplyViewport() const;
  void ApplyFullFrameScissor() const;
  void ApplyScissorRegion(Rml::Rectanglei region) const;
  void CreateNullTexture();
  void EnsureClipResetGeometry();
  void ApplyScissorRegion();
  void DrawGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture,
                    gfx::PipelineRef pipeline);
  void DrawFullscreenTexture(gfx::BindGroupRef bindGroup, gfx::PipelineRef pipeline,
                             gfx::BindGroupRef extraBindGroup = 0, gfx::Range extraUniformRange = {},
                             bool extraBindGroupHasDynamicOffset = true, std::array<float, 4> blendConstant = {},
                             bool hasBlendConstant = false);
  void CompositeToTarget(gfx::BindGroupRef bindGroup, const wgpu::TextureView& view, wgpu::LoadOp loadOp,
                         gfx::PipelineRef pipeline, const char* label, gfx::BindGroupRef extraBindGroup = 0,
                         gfx::Range extraUniformRange = {}, bool extraBindGroupHasDynamicOffset = true,
                         std::array<float, 4> blendConstant = {}, bool hasBlendConstant = false);
  void RenderBlur(float sigma, const RenderTarget& sourceDestination, const RenderTarget& temp);
  size_t RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filters);

public:
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
  Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
  void ReleaseTexture(Rml::TextureHandle texture) override;
  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;
  void EnableClipMask(bool enable) override;
  void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation) override;
  void SetTransform(const Rml::Matrix4f* transform) override;
  Rml::LayerHandle PushLayer() override;
  void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                       Rml::Span<const Rml::CompiledFilterHandle> filters) override;
  void PopLayer() override;
  Rml::TextureHandle SaveLayerAsTexture() override;
  Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
  Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
  void ReleaseFilter(Rml::CompiledFilterHandle filter) override;
  Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
  void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                    Rml::TextureHandle texture) override;
  void ReleaseShader(Rml::CompiledShaderHandle shader) override;

  void BeginFrame(const webgpu::TextureWithSampler& target, const webgpu::TextureWithSampler& scene_target,
                  BaseLayerContent baseLayerContent);
  bool EndFrame();
  void SetWindowSize(const Rml::Vector2i& window_size) { m_windowSize = window_size; }
  void SetRenderTargetFormat(wgpu::TextureFormat render_target_format) { m_renderTargetFormat = render_target_format; }
  wgpu::TextureView GetClipMaskStencilView(const wgpu::Extent3D& size);

  void CreateDeviceObjects();

  void NewFrame();
};
} // namespace aurora::rmlui

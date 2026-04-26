#pragma once
#include <dawn/webgpu_cpp.h>

#include "RmlUi/Core/RenderInterface.h"

namespace aurora::rmlui {

// copied from imgui wgpu impl
struct UniformBlock {
  float MVP[4][4];
  float Gamma;
  float translation[2];
};

class WebGPURenderInterface : public Rml::RenderInterface {
private:
  const wgpu::RenderPassEncoder* m_pass = nullptr;

  wgpu::RenderPipeline m_pipeline = nullptr;
  wgpu::Buffer m_uniformBuffer = nullptr;
  wgpu::Sampler m_sampler = nullptr;
  wgpu::TextureFormat m_depthStencilFormat = wgpu::TextureFormat::Undefined;
  wgpu::TextureFormat m_renderTargetFormat = wgpu::TextureFormat::Undefined;

  wgpu::BindGroup m_CommonBindGroup = nullptr;
  wgpu::BindGroupLayout m_ImageBindGroupLayout = nullptr;

  Rml::Vector2i m_windowSize;
  float m_gamma = 0.0f;

  bool m_enableScissorRegion = false;

  void CreateUniformBuffer();

  void SetupRenderState(const Rml::Vector2f& translation);
public:
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture) override;
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;

  void SetRenderPass(const wgpu::RenderPassEncoder* pass) { m_pass = pass; }
  void SetWindowSize(const Rml::Vector2i& window_size) { m_windowSize = window_size; }
  void SetRenderTargetFormat(wgpu::TextureFormat render_target_format) { m_renderTargetFormat = render_target_format; }

  void CreateDeviceObjects();

};
}

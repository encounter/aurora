#include <gtest/gtest.h>

#include <aurora/gfx.hpp>

#include "gfx/clear.hpp"
#include "gfx/frame_packet.hpp"

namespace aurora::gfx {
namespace {

RenderTargetLayout base_layout() {
  RenderTargetLayout layout{
      .colorAttachmentCount = 1,
      .colorAttachments = {{{
          .semantic = ColorAttachmentSemantic::SceneColor,
          .format = wgpu::TextureFormat::RGBA8Unorm,
          .width = 128,
          .height = 128,
      }}},
      .depthStencilFormat = wgpu::TextureFormat::Depth24Plus,
      .sampleCount = 4,
  };
  detail::finalize_render_target_layout(layout);
  return layout;
}

TEST(RenderTargetLayoutTest, KeyChangesWithPipelineCompatibility) {
  const auto base = base_layout();

  auto colorFormat = base;
  colorFormat.colorAttachments[SceneColorAttachmentIndex].format = wgpu::TextureFormat::BGRA8Unorm;
  detail::finalize_render_target_layout(colorFormat);
  EXPECT_NE(colorFormat.key, base.key);

  auto colorCount = base;
  colorCount.colorAttachmentCount = 2;
  colorCount.colorAttachments[1] = {ColorAttachmentSemantic::Normal, wgpu::TextureFormat::RGBA16Float};
  detail::finalize_render_target_layout(colorCount);
  EXPECT_NE(colorCount.key, base.key);

  auto depthFormat = base;
  depthFormat.depthStencilFormat = wgpu::TextureFormat::Depth32Float;
  detail::finalize_render_target_layout(depthFormat);
  EXPECT_NE(depthFormat.key, base.key);

  auto sampleCount = base;
  sampleCount.sampleCount = 1;
  detail::finalize_render_target_layout(sampleCount);
  EXPECT_NE(sampleCount.key, base.key);
}

TEST(RenderTargetLayoutTest, RenderPassDerivesLayoutFromAttachmentState) {
  detail::RenderPass pass{
      .colorAttachmentCount = 2,
      .depthStencilFormat = wgpu::TextureFormat::Depth24Plus,
      .msaaSamples = 4,
  };
  pass.colorAttachments[SceneColorAttachmentIndex].semantic = ColorAttachmentSemantic::SceneColor;
  pass.colorAttachments[SceneColorAttachmentIndex].format = wgpu::TextureFormat::RGBA8Unorm;
  pass.colorAttachments[1].semantic = ColorAttachmentSemantic::Normal;
  pass.colorAttachments[1].format = wgpu::TextureFormat::RGBA16Float;

  const auto initial = pass.target_layout();
  EXPECT_EQ(initial.colorAttachmentCount, 2u);
  EXPECT_EQ(initial.colorAttachments[SceneColorAttachmentIndex].semantic, ColorAttachmentSemantic::SceneColor);
  EXPECT_EQ(initial.colorAttachments[1].format, wgpu::TextureFormat::RGBA16Float);
  EXPECT_EQ(initial.depthStencilFormat, wgpu::TextureFormat::Depth24Plus);
  EXPECT_EQ(initial.sampleCount, 4u);

  pass.colorAttachments[1].format = wgpu::TextureFormat::RG16Float;
  EXPECT_NE(pass.target_layout().key, initial.key);
}

} // namespace
} // namespace aurora::gfx

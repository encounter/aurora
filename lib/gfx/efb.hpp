#pragma once

#include "present_target.hpp"

namespace aurora::gfx::efb {

struct State {
  AuroraWindowSize size{};
  webgpu::TextureWithSampler color;
  webgpu::TextureWithSampler resolvedColor;
  webgpu::TextureWithSampler depth;
  webgpu::TextureWithSampler normal;
  RenderTargetLayout layout;
  present::TargetSnapshot xfb;

  const webgpu::TextureWithSampler& present_source() const noexcept {
    return layout.sampleCount > 1 ? resolvedColor : color;
  }
};

void initialize(const AuroraSurfaceDesc& desc);
void shutdown();
State prepare_frame(bool commit = true);
// Copies the attachment handles on the recording thread.
State capture_state();
State applied_state();
AuroraWindowSize applied_size();
AuroraWindowSize pending_size();
AuroraWindowSize resolve_size(AuroraWindowSize size);
AuroraWindowSize calculate_size(AuroraWindowSize size, uint32_t baseWidth, uint32_t baseHeight,
                                float frameBufferScale, bool aspectFit) noexcept;
void set_display_size(uint32_t width, uint32_t height, float scale);
bool set_scale(float scale);
bool set_aspect_fit(bool fit);

} // namespace aurora::gfx::efb

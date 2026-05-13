#pragma once

#include "aurora/aurora.h"
#include "webgpu/gpu.hpp"

#include <SDL3/SDL_events.h>
#include <aurora/rmlui.hpp>
#include <dawn/webgpu_cpp.h>

namespace aurora::rmlui {
struct RenderOutput {
  const webgpu::TextureWithSampler* texture = nullptr;
  wgpu::BindGroup copyBindGroup;
};

void initialize(const AuroraWindowSize& size) noexcept;
void handle_event(SDL_Event& event) noexcept;
RenderOutput render(const wgpu::CommandEncoder& encoder, const webgpu::Viewport& presentViewport) noexcept;
void shutdown() noexcept;
} // namespace aurora::rmlui

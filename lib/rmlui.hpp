#pragma once
#include <SDL3/SDL_events.h>
#include <dawn/webgpu_cpp.h>

#include "aurora/aurora.h"

namespace aurora::rmlui {
void initialize(const AuroraWindowSize& window_size) noexcept;
void handle_event(SDL_Event& event) noexcept;
void render(const wgpu::RenderPassEncoder& pass) noexcept; // const wgpu::RenderPassEncoder& pass
void shutdown() noexcept;
} // namespace aurora::imgui
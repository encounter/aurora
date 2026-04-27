#pragma once
#include <SDL3/SDL_events.h>
#include <aurora/rmlui.hpp>
#include <dawn/webgpu_cpp.h>

#include "aurora/aurora.h"

namespace aurora::rmlui {
void initialize(const AuroraWindowSize& window_size) noexcept;
void handle_event(SDL_Event& event) noexcept;
void render(const wgpu::RenderPassEncoder& pass) noexcept;
void shutdown() noexcept;
} // namespace aurora::imgui

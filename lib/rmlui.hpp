#pragma once

#include "aurora/aurora.h"
#include "gfx/common.hpp"

#include <SDL3/SDL_events.h>
#include <aurora/rmlui.hpp>
#include <dawn/webgpu_cpp.h>

namespace aurora::rmlui {
void initialize(const AuroraWindowSize& size) noexcept;
void handle_event(SDL_Event& event) noexcept;
void render(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& outputView, const wgpu::Extent3D& size,
            const gfx::Viewport& viewport) noexcept;
void shutdown() noexcept;
} // namespace aurora::rmlui

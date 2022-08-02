#pragma once

#include <aurora/event.h>

#include <string_view>

union SDL_Event;

namespace wgpu {
class RenderPassEncoder;
} // namespace wgpu

namespace aurora::imgui {
void create_context() noexcept;
void initialize() noexcept;
void shutdown() noexcept;

void process_event(const SDL_Event& event) noexcept;
void new_frame(const AuroraWindowSize& size) noexcept;
void render(const wgpu::RenderPassEncoder& pass) noexcept;
} // namespace aurora::imgui

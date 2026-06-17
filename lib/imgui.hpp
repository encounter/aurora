#pragma once

#include <aurora/event.h>

#include <memory>
#include <utility>

union SDL_Event;

namespace wgpu {
class RenderPassEncoder;
} // namespace wgpu

namespace aurora::imgui {
class DrawData {
public:
  DrawData() noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

private:
  struct Impl;
  explicit DrawData(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

  std::shared_ptr<Impl> m_impl;

  friend DrawData freeze() noexcept;
  friend void render(const wgpu::RenderPassEncoder& pass, const DrawData& drawData) noexcept;
};

void create_context() noexcept;
void initialize() noexcept;
void shutdown() noexcept;

void process_event(const SDL_Event& event) noexcept;
bool wants_capture_event(const SDL_Event& event) noexcept;
void new_frame(const AuroraWindowSize& size) noexcept;
DrawData freeze() noexcept;
void render(const wgpu::RenderPassEncoder& pass, const DrawData& drawData) noexcept;
} // namespace aurora::imgui

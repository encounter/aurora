#pragma once

#include <aurora/aurora.h>
#include <aurora/gfx.hpp>
#include "../webgpu/gpu.hpp"
#include "../imgui.hpp"

#include <memory>

namespace aurora::gfx::present {

struct TargetState;

struct TargetSnapshot {
  std::shared_ptr<TargetState> state;
  PresentTargetId id = InvalidPresentTarget;
  uint64_t generation = 0;
  uint64_t revision = 0;
  AuroraWindowSize size{};
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  wgpu::PresentMode presentMode = wgpu::PresentMode::Fifo;
  wgpu::CompositeAlphaMode alphaMode = wgpu::CompositeAlphaMode::Auto;
  wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment;
  bool sdlManaged = false;
};

struct Request {
  TargetSnapshot target;
  webgpu::TextureWithSampler source;
  webgpu::Viewport viewport{};
  wgpu::BindGroup rmlBindGroup;
  bool rmlOverlay = false;
  bool primary = false;
  imgui::DrawData imguiDrawData;
};

TargetSnapshot snapshot(PresentTargetId id, bool primary = false);
void submit(const Request& request);
void shutdown();
void set_vsync(bool enabled) noexcept;
bool bind_xfb_surface(wgpu::Surface&& surface, const AuroraSurfaceDesc& desc);
bool replace_xfb_surface(const AuroraSurfaceDesc& desc);

bool is_sdl_xfb() noexcept;
bool ensure_sdl_target();
void retire_sdl_targets();
void resize_sdl_target(PresentTargetId id, uint32_t width, uint32_t height, uint32_t nativeWidth,
                       uint32_t nativeHeight, float scale) noexcept;

} // namespace aurora::gfx::present

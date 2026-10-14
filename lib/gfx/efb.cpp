#include "efb.hpp"

#include "frame.hpp"
#include "../dolphin/vi/vi_internal.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>

namespace aurora::gfx::efb {
namespace {

std::mutex g_configMutex;
AuroraWindowSize g_displaySize{};
std::optional<State> g_applied;
float g_scale = 0.f;
bool g_aspectFit = false;

uint32_t extent(double value) {
  return static_cast<uint32_t>(std::clamp(std::round(value), 1.0, static_cast<double>(UINT32_MAX)));
}

AuroraWindowSize display_size(uint32_t width, uint32_t height, float scale) {
  scale = std::isfinite(scale) && scale > 0.f ? scale : 1.f;
  return {
      .width = width ? extent(width / static_cast<double>(scale)) : 0,
      .height = height ? extent(height / static_cast<double>(scale)) : 0,
      .native_fb_width = width,
      .native_fb_height = height,
      .scale = scale,
  };
}

} // namespace

AuroraWindowSize calculate_size(AuroraWindowSize size, uint32_t baseWidth, uint32_t baseHeight,
                                float frameBufferScale, bool aspectFit) noexcept {
  baseWidth = std::max(1u, baseWidth);
  baseHeight = std::max(1u, baseHeight);
  if (!std::isfinite(size.scale) || size.scale <= 0.f) {
    size.scale = 1.f;
  }

  const uint32_t nativeWidth = size.native_fb_width ? size.native_fb_width : baseWidth;
  const uint32_t nativeHeight = size.native_fb_height ? size.native_fb_height : baseHeight;
  if (size.width == 0) {
    size.width = extent(nativeWidth / static_cast<double>(size.scale));
  }
  if (size.height == 0) {
    size.height = extent(nativeHeight / static_cast<double>(size.scale));
  }
  const double baseAspect = static_cast<double>(baseWidth) / baseHeight;
  const double nativeAspect = static_cast<double>(nativeWidth) / nativeHeight;

  if (std::isfinite(frameBufferScale) && frameBufferScale > 0.f) {
    size.fb_width = extent(baseWidth * static_cast<double>(frameBufferScale));
    size.fb_height = extent(baseHeight * static_cast<double>(frameBufferScale));
    if (!aspectFit) {
      if (nativeAspect >= baseAspect) {
        size.fb_width = extent(size.fb_height * nativeAspect);
      } else {
        size.fb_height = extent(size.fb_width / nativeAspect);
      }
    }
  } else {
    size.fb_width = nativeWidth;
    size.fb_height = nativeHeight;
    if (aspectFit) {
      if (nativeAspect > baseAspect) {
        size.fb_width = extent(nativeHeight * baseAspect);
      } else {
        size.fb_height = extent(nativeWidth / baseAspect);
      }
    }
  }

  return size;
}

void initialize(const AuroraSurfaceDesc& desc) {
  std::lock_guard lock{g_configMutex};
  g_displaySize = display_size(desc.width, desc.height, desc.scale);
  g_applied.reset();
  g_scale = 0.f;
  g_aspectFit = false;
}

void shutdown() {
  std::lock_guard lock{g_configMutex};
  g_applied.reset();
}

void set_display_size(uint32_t width, uint32_t height, float scale) {
  std::lock_guard lock{g_configMutex};
  g_displaySize = display_size(width, height, scale);
}

bool set_scale(float scale) {
  std::lock_guard lock{g_configMutex};
  scale = std::isfinite(scale) ? std::max(0.f, scale) : 0.f;
  if (g_scale == scale) {
    return false;
  }

  g_scale = scale;
  return true;
}

bool set_aspect_fit(bool fit) {
  std::lock_guard lock{g_configMutex};
  if (g_aspectFit == fit) {
    return false;
  }

  g_aspectFit = fit;
  return true;
}

AuroraWindowSize resolve_size(AuroraWindowSize size) {
  const auto [baseWidth, baseHeight] = vi::configured_fb_size();
  std::lock_guard lock{g_configMutex};
  return calculate_size(size, baseWidth, baseHeight, g_scale, g_aspectFit);
}

AuroraWindowSize pending_size() {
  const auto target = present::snapshot(xfb_target(), true);
  const auto [baseWidth, baseHeight] = vi::configured_fb_size();
  std::lock_guard lock{g_configMutex};
  auto size = target.state ? target.size : g_displaySize;
  if (!target.state) {
    if (!size.native_fb_width) {
      size.native_fb_width = baseWidth;
    }
    if (!size.native_fb_height) {
      size.native_fb_height = baseHeight;
    }
  }

  return calculate_size(size, baseWidth, baseHeight, g_scale, g_aspectFit);
}

AuroraWindowSize applied_size() {
  {
    std::lock_guard lock{g_configMutex};
    if (g_applied) {
      return g_applied->size;
    }
  }

  return pending_size();
}

State capture_state() {
  return {
      .size = applied_size(),
      .color = webgpu::g_frameBuffer,
      .resolvedColor = webgpu::g_frameBufferResolved,
      .depth = webgpu::g_depthBuffer,
      .normal = webgpu::g_normalBuffer,
      .layout = scene_render_target_layout(),
  };
}

State prepare_frame(bool commit) {
  const auto target = present::snapshot(xfb_target(), true);
  const auto [baseWidth, baseHeight] = vi::configured_fb_size();
  AuroraWindowSize size;
  {
    std::lock_guard lock{g_configMutex};
    size = target.state ? target.size : g_displaySize;
    if (!target.state) {
      if (!size.native_fb_width) {
        size.native_fb_width = baseWidth;
      }
      if (!size.native_fb_height) {
        size.native_fb_height = baseHeight;
      }
    }
    size = calculate_size(size, baseWidth, baseHeight, g_scale, g_aspectFit);
  }

  webgpu::resize_efb(size.fb_width, size.fb_height);
  auto state = capture_state();
  state.size = size;
  state.xfb = target;

  if (commit) {
    std::lock_guard lock{g_configMutex};
    g_applied = state;
  }

  return state;
}

State applied_state() {
  std::lock_guard lock{g_configMutex};
  return g_applied.value_or(State{});
}

} // namespace aurora::gfx::efb

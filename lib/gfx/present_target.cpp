#include "present_target.hpp"

#include "efb.hpp"
#include "frame.hpp"
#include "render_worker.hpp"
#include "../webgpu/gpu_prof.hpp"
#include "../internal.hpp"
#include "../window.hpp"

#include <aurora/webgpu.hpp>
#include <tracy/Tracy.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurora::gfx {
namespace present {

struct TargetState {
  std::mutex mutex;
  std::mutex configMutex;
  wgpu::Surface surface;
  WGPUSurface identity = nullptr;
  std::string label;
  std::vector<wgpu::TextureFormat> formats;
  std::vector<wgpu::PresentMode> presentModes;
  AuroraWindowSize size{};
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  wgpu::CompositeAlphaMode alphaMode = wgpu::CompositeAlphaMode::Auto;
  wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment;
  wgpu::SurfaceConfiguration applied{};
  wgpu::Future lastSubmission{};
  std::atomic<uint64_t> generation = 1;
  std::atomic_bool retired = false;
  uint64_t revision = 1;
  uint64_t appliedRevision = 0;
  bool dirty = true;
  bool vsync = true;
  std::atomic_bool sdlManaged = false;
  std::atomic<PresentTargetStatus> status = PresentTargetStatus::Pending;
};

} // namespace present

namespace {

constexpr Module Log{"aurora::gfx::present"};
std::mutex g_targetsMutex;
std::unordered_map<PresentTargetId, std::shared_ptr<present::TargetState>> g_targets;
PresentTargetId g_nextId = 1;
std::atomic g_xfbTarget = InvalidPresentTarget;

std::shared_ptr<present::TargetState> find_target(PresentTargetId id) {
  std::lock_guard lock{g_targetsMutex};
  const auto it = g_targets.find(id);
  return it != g_targets.end() ? it->second : nullptr;
}

uint32_t logical_extent(uint32_t nativeExtent, float scale) {
  if (!nativeExtent) {
    return 0;
  }
  return static_cast<uint32_t>(
      std::clamp(std::round(nativeExtent / static_cast<double>(scale)), 1.0, static_cast<double>(UINT32_MAX)));
}

AuroraWindowSize target_size(uint32_t width, uint32_t height, float scale) {
  scale = std::isfinite(scale) && scale > 0.f ? scale : 1.f;
  return {
      .width = logical_extent(width, scale),
      .height = logical_extent(height, scale),
      .native_fb_width = width,
      .native_fb_height = height,
      .scale = scale,
  };
}

bool supports(const present::TargetState& target, wgpu::TextureFormat format) {
  return std::ranges::find(target.formats, format) != target.formats.end();
}

uint32_t scissor_coord(double value, uint32_t maximum) {
  return std::isfinite(value) ? static_cast<uint32_t>(std::clamp(value, 0.0, static_cast<double>(maximum))) : 0;
}

} // namespace

PresentTargetId create_present_target(PresentTargetDesc&& desc) {
  if (!desc.surface || !webgpu::g_adapter || !webgpu::g_device) {
    return InvalidPresentTarget;
  }

  wgpu::SurfaceCapabilities caps;
  if (desc.surface.GetCapabilities(webgpu::g_adapter, &caps) != wgpu::Status::Success || !caps.formatCount ||
      !caps.presentModeCount || !caps.alphaModeCount || (caps.usages & desc.usage) != desc.usage ||
      (desc.usage & wgpu::TextureUsage::RenderAttachment) == wgpu::TextureUsage::None) {
    Log.warn("create_present_target: surface is incompatible with the active adapter");
    return InvalidPresentTarget;
  }

  auto target = std::make_shared<present::TargetState>();
  target->label = desc.label ? desc.label : "Present target";
  target->formats.assign(caps.formats, caps.formats + caps.formatCount);
  target->presentModes.assign(caps.presentModes, caps.presentModes + caps.presentModeCount);
  const std::array preferredFormats{
      desc.preferredFormat, color_format(), wgpu::TextureFormat::RGBA8Unorm, wgpu::TextureFormat::BGRA8Unorm,
      caps.formats[0],
  };
  for (const auto format : preferredFormats) {
    if (format != wgpu::TextureFormat::Undefined && supports(*target, format)) {
      target->format = format;
      break;
    }
  }

  target->alphaMode = caps.alphaModes[0];
  for (size_t i = 0; i < caps.alphaModeCount; ++i) {
    if (caps.alphaModes[i] == desc.preferredAlphaMode) {
      target->alphaMode = desc.preferredAlphaMode;
    }
  }
  target->size = target_size(desc.width, desc.height, desc.scale);
  target->usage = desc.usage;
  target->vsync = desc.vsync;

  std::lock_guard lock{g_targetsMutex};
  for (const auto& [id, existing] : g_targets) {
    if (!existing->retired.load(std::memory_order_acquire) && existing->identity == desc.surface.Get()) {
      Log.warn("create_present_target: surface is already registered");
      return InvalidPresentTarget;
    }
  }

  target->identity = desc.surface.Get();
  target->surface = std::move(desc.surface);
  const auto id = g_nextId++;
  g_targets.emplace(id, std::move(target));
  return id;
}

void destroy_present_target(PresentTargetId id) noexcept {
  std::shared_ptr<present::TargetState> target;
  {
    std::lock_guard lock{g_targetsMutex};
    const auto it = g_targets.find(id);
    if (it == g_targets.end()) {
      return;
    }
    target = it->second;
    target->retired.store(true, std::memory_order_release);
    auto expected = id;
    g_xfbTarget.compare_exchange_strong(expected, InvalidPresentTarget);
  }

  window::SurfaceLock activityLock{target->sdlManaged};
  std::lock_guard lock{target->mutex};
  ++target->generation;
  target->status = PresentTargetStatus::Lost;
  const bool configured = target->surface && target->appliedRevision != 0;
  if (target->surface) {
    if (target->appliedRevision) {
      target->surface.Unconfigure();
    }
    target->surface = {};
  }

  if (configured && webgpu::g_backendType == wgpu::BackendType::Vulkan && webgpu::g_queue) {
    webgpu::g_queue.Submit(0, nullptr);
    target->lastSubmission = webgpu::g_queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
                                                                 [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
  }
  if (target->lastSubmission.id && webgpu::g_instance) {
    while (webgpu::g_instance.WaitAny(target->lastSubmission, 100'000'000) == wgpu::WaitStatus::TimedOut) {}
  }
  // Tick Dawn after releasing the wrapper to retire native bindings
  if (webgpu::g_instance) {
    webgpu::g_instance.ProcessEvents();
  }

  std::lock_guard registryLock{g_targetsMutex};
  const auto it = g_targets.find(id);
  if (it != g_targets.end() && it->second == target) {
    g_targets.erase(it);
  }
}

PresentTargetStatus present_target_status(PresentTargetId id) noexcept {
  const auto target = find_target(id);
  if (!target) {
    return PresentTargetStatus::Lost;
  }

  return target->status.load(std::memory_order_acquire);
}

void resize_present_target(PresentTargetId id, uint32_t width, uint32_t height, float scale) noexcept {
  const auto target = find_target(id);
  if (!target) {
    return;
  }

  std::lock_guard lock{target->configMutex};
  const auto size = target_size(width, height, scale);
  if (target->size.width == size.width && target->size.height == size.height &&
      target->size.native_fb_width == size.native_fb_width && target->size.native_fb_height == size.native_fb_height &&
      target->size.scale == size.scale) {
    return;
  }

  target->size = size;
  ++target->revision;
}

bool set_xfb_target(PresentTargetId id) noexcept {
  std::lock_guard registryLock{g_targetsMutex};
  if (id != InvalidPresentTarget) {
    const auto it = g_targets.find(id);
    if (it == g_targets.end()) {
      return false;
    }
    if (it->second->retired.load(std::memory_order_acquire) || !supports(*it->second, color_format())) {
      Log.warn("set_xfb_target: surface does not support the fixed EFB format");
      return false;
    }
  }

  g_xfbTarget.store(id, std::memory_order_release);
  if (id == InvalidPresentTarget) {
    window::clear_surface_restore();
  }
  return true;
}

PresentTargetId xfb_target() noexcept { return g_xfbTarget.load(std::memory_order_acquire); }

namespace present {

bool is_sdl_xfb() noexcept {
  const auto target = find_target(xfb_target());
  return target && !target->retired.load(std::memory_order_acquire) &&
         target->sdlManaged.load(std::memory_order_acquire);
}

TargetSnapshot snapshot(PresentTargetId id, bool primary) {
  const auto target = find_target(id);
  if (!target) {
    return {};
  }

  std::lock_guard lock{target->configMutex};
  if (target->retired.load(std::memory_order_acquire)) {
    return {};
  }

  const auto mode = webgpu::select_present_mode(target->presentModes, target->vsync);
  return {
      .state = target,
      .id = id,
      .generation = target->generation.load(std::memory_order_acquire),
      .revision = target->revision,
      .size = target->size,
      .format = primary ? color_format() : target->format,
      .presentMode = mode,
      .alphaMode = target->alphaMode,
      .usage = target->usage,
      .sdlManaged = target->sdlManaged.load(std::memory_order_acquire),
  };
}

void submit(const Request& request) {
  const auto& snapshot = request.target;
  if (!snapshot.state || !request.source.view) {
    return;
  }

  window::SurfaceLock activityLock{snapshot.sdlManaged};
  auto& target = *snapshot.state;
  std::lock_guard lock{target.mutex};
  if (target.retired.load(std::memory_order_acquire) ||
      snapshot.generation != target.generation.load(std::memory_order_acquire) || !target.surface) {
    return;
  }

  const auto width = snapshot.size.native_fb_width;
  const auto height = snapshot.size.native_fb_height;
  if (!width || !height || (snapshot.sdlManaged && !window::is_presentable())) {
    target.status = PresentTargetStatus::TransientUnavailable;
    return;
  }

  AURORA_ASSERT(render_worker::is_worker_thread(), "Surface configuration must run on the render worker");
  if (target.dirty || target.appliedRevision != snapshot.revision || target.applied.format != snapshot.format ||
      target.applied.presentMode != snapshot.presentMode) {
    target.applied = {
        .device = webgpu::g_device,
        .format = snapshot.format,
        .usage = snapshot.usage,
        .width = width,
        .height = height,
        .alphaMode = snapshot.alphaMode,
        .presentMode = snapshot.presentMode,
    };
    target.surface.Configure(&target.applied);
    target.appliedRevision = snapshot.revision;
    target.dirty = false;
  }

  wgpu::SurfaceTexture surfaceTexture;
  {
    ZoneScopedN("Acquire texture");
    target.surface.GetCurrentTexture(&surfaceTexture);
  }

  switch (surfaceTexture.status) {
  case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
    target.dirty = true;
    [[fallthrough]];
  case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
    break;
  case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
  case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
    target.status = PresentTargetStatus::TransientUnavailable;
    target.dirty = true;
    return;
  case wgpu::SurfaceGetCurrentTextureStatus::Lost:
    target.status = PresentTargetStatus::Lost;
    target.dirty = true;
    return;
  default:
    target.status = PresentTargetStatus::Error;
    return;
  }
  if (!surfaceTexture.texture) {
    target.status = PresentTargetStatus::TransientUnavailable;
    return;
  }

  auto view = surfaceTexture.texture.CreateView();
  const auto encoderLabel = target.label + " encoder";
  const wgpu::CommandEncoderDescriptor encoderDesc{
      .label = encoderLabel.c_str(),
  };
  auto encoder = webgpu::g_device.CreateCommandEncoder(&encoderDesc);
  const wgpu::RenderPassColorAttachment attachment{
      .view = view,
      .loadOp = wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
  };
  const wgpu::RenderPassDescriptor passDesc{
      .label = "EFB copy render pass",
      .colorAttachmentCount = 1,
      .colorAttachments = &attachment,
      .timestampWrites = webgpu::gpu_prof::pass_writes("Present blit"),
  };
  auto pass = encoder.BeginRenderPass(&passDesc);
  const auto& viewport = request.viewport;
  pass.SetViewport(viewport.left, viewport.top, viewport.width, viewport.height, 0.f, 1.f);
  const auto x = scissor_coord(std::floor(viewport.left), width);
  const auto y = scissor_coord(std::floor(viewport.top), height);
  pass.SetScissorRect(x, y, scissor_coord(std::ceil(viewport.left + viewport.width), width) - x,
                      scissor_coord(std::ceil(viewport.top + viewport.height), height) - y);

  pass.SetPipeline(webgpu::copy_pipeline(snapshot.format));
  pass.SetBindGroup(0, request.rmlBindGroup && !request.rmlOverlay ? request.rmlBindGroup
                                                                   : webgpu::create_copy_bind_group(request.source));
  pass.Draw(3);
  if (request.rmlBindGroup && request.rmlOverlay) {
    pass.SetPipeline(webgpu::copy_pipeline(snapshot.format, true));
    pass.SetBindGroup(0, request.rmlBindGroup);
    pass.Draw(3);
  }
  pass.End();

  if (request.primary && request.imguiDrawData) {
    const wgpu::RenderPassColorAttachment imguiAttachment{
        .view = view,
        .loadOp = wgpu::LoadOp::Load,
        .storeOp = wgpu::StoreOp::Store,
    };
    const wgpu::RenderPassDescriptor imguiPassDesc{
        .label = "ImGui render pass",
        .colorAttachmentCount = 1,
        .colorAttachments = &imguiAttachment,
        .timestampWrites = webgpu::gpu_prof::pass_writes("ImGui"),
    };
    const auto imguiPass = encoder.BeginRenderPass(&imguiPassDesc);
    imguiPass.SetViewport(0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f);
    imguiPass.SetScissorRect(0, 0, width, height);
    imgui::render(imguiPass, request.imguiDrawData);
    imguiPass.End();
  }

  const auto bufferLabel = target.label + " command buffer";
  const wgpu::CommandBufferDescriptor bufferDesc{
      .label = bufferLabel.c_str(),
  };
  const auto buffer = encoder.Finish(&bufferDesc);
  {
    ZoneScopedN("Queue Submit");
    webgpu::g_queue.Submit(1, &buffer);
  }
  wgpu::ConvertibleStatus status = wgpu::Status::Error;
  {
    ZoneScopedN("Present");
    status = target.surface.Present();
  }
  if (status) {
    target.status = PresentTargetStatus::Ready;
    if (request.primary) {
      after_present();
    }
  } else {
    target.status = PresentTargetStatus::Lost;
    target.dirty = true;
  }

  target.lastSubmission = webgpu::g_queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
                                                              [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
}

void shutdown() {
  while (true) {
    PresentTargetId id;
    {
      std::lock_guard lock{g_targetsMutex};
      if (g_targets.empty()) {
        break;
      }
      id = g_targets.begin()->first;
    }
    destroy_present_target(id);
  }
}

void retire_sdl_targets() {
  std::vector<PresentTargetId> targets;
  {
    std::lock_guard lock{g_targetsMutex};
    for (const auto& [id, target] : g_targets) {
      if (target->sdlManaged.load(std::memory_order_acquire)) {
        targets.push_back(id);
      }
    }
  }

  for (const auto id : targets) {
    destroy_present_target(id);
  }
}

void set_vsync(bool enabled) noexcept {
  const auto target = find_target(xfb_target());
  if (!target) {
    return;
  }

  std::lock_guard lock{target->configMutex};
  if (target->vsync != enabled) {
    target->vsync = enabled;
    ++target->revision;
  }
}

void resize_sdl_target(PresentTargetId id, uint32_t width, uint32_t height, uint32_t nativeWidth, uint32_t nativeHeight,
                       float scale) noexcept {
  const auto target = find_target(id);
  if (!target) {
    return;
  }

  std::lock_guard lock{target->configMutex};
  scale = std::isfinite(scale) && scale > 0.f ? scale : 1.f;
  if (target->size.width == width && target->size.height == height && target->size.native_fb_width == nativeWidth &&
      target->size.native_fb_height == nativeHeight && target->size.scale == scale) {
    return;
  }

  target->size = {
      .width = width,
      .height = height,
      .native_fb_width = nativeWidth,
      .native_fb_height = nativeHeight,
      .scale = scale,
  };
  ++target->revision;
}

bool bind_xfb_surface(wgpu::Surface&& surface, const AuroraSurfaceDesc& desc) {
  const auto previous = xfb_target();
  PresentTargetId next = InvalidPresentTarget;
  if (desc.type != AURORA_SURFACE_HEADLESS) {
    if (!surface || (desc.type != AURORA_SURFACE_SDL_MANAGED && (!desc.width || !desc.height))) {
      return false;
    }

    const auto size = desc.type == AURORA_SURFACE_SDL_MANAGED ? window::refresh_window_size()
                                                              : target_size(desc.width, desc.height, desc.scale);
    next = create_present_target({
        .label = "Primary XFB",
        .surface = std::move(surface),
        .width = size.native_fb_width,
        .height = size.native_fb_height,
        .scale = size.scale,
        .preferredFormat = color_format(),
        .vsync = webgpu::vsync_enabled(),
    });
    if (next == InvalidPresentTarget) {
      return false;
    }
    const auto replacement = find_target(next);
    if (!replacement || !supports(*replacement, color_format())) {
      destroy_present_target(next);
      return false;
    }
    if (desc.type == AURORA_SURFACE_SDL_MANAGED) {
      {
        std::lock_guard lock{replacement->configMutex};
        replacement->sdlManaged = true;
      }
      resize_sdl_target(next, size.width, size.height, size.native_fb_width, size.native_fb_height, size.scale);
    }
  }

  destroy_present_target(previous);
  if (!set_xfb_target(next)) {
    return false;
  }
  if (desc.type != AURORA_SURFACE_SDL_MANAGED) {
    efb::set_display_size(desc.width, desc.height, desc.scale);
  }
  return true;
}

bool replace_xfb_surface(const AuroraSurfaceDesc& desc) {
  wgpu::Surface surface;
  if (desc.type != AURORA_SURFACE_HEADLESS) {
    if (desc.type != AURORA_SURFACE_SDL_MANAGED && (!desc.width || !desc.height)) {
      return false;
    }
    window::SurfaceLock activityLock{desc.type == AURORA_SURFACE_SDL_MANAGED};
    surface = webgpu::create_surface(webgpu::g_instance, desc, "Primary XFB");
  }
  return bind_xfb_surface(std::move(surface), desc);
}

bool ensure_sdl_target() {
  const auto id = xfb_target();
  const auto status = present_target_status(id);
  if (id && status != PresentTargetStatus::Lost && status != PresentTargetStatus::Error) {
    return true;
  }

  destroy_present_target(id);
  const auto size = window::refresh_window_size();
  return replace_xfb_surface({
      .type = AURORA_SURFACE_SDL_MANAGED,
      .width = size.native_fb_width,
      .height = size.native_fb_height,
      .scale = size.scale,
  });
}

} // namespace present
} // namespace aurora::gfx

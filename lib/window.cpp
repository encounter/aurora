#include "window.hpp"

#ifdef AURORA_ENABLE_GX
#include "imgui.hpp"
#include "webgpu/gpu.hpp"
#include "gfx/efb.hpp"
#include "gfx/present_target.hpp"
#include <aurora/webgpu.hpp>
#endif
#include "gamepad.hpp"
#include "input/router.hpp"
#include "input/sdl_input.hpp"
#include "internal.hpp"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_pixels.h>
#include <tracy/Tracy.hpp>

#if defined(SDL_PLATFORM_ANDROID)
#include <jni.h>
extern "C" void Android_LockActivityMutex(void);
extern "C" void Android_UnlockActivityMutex(void);
#endif

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#ifdef AURORA_ENABLE_RMLUI
#include "rmlui.hpp"
#endif
#include "time_internal.hpp"
#include "dolphin/vi/vi_internal.hpp"

namespace aurora::window {
namespace {
constexpr Module Log{"aurora::window"};

SDL_Window* g_window;
std::atomic g_surfaceType = AURORA_SURFACE_SDL_MANAGED;
SDL_Renderer* g_renderer;
#ifndef AURORA_ENABLE_GX
float g_frameBufferScale = 0.f;
bool g_frameBufferAspectFit = false;
#endif
SDL_InitFlags g_initializedSubsystems = 0;
std::mutex g_eventMutex;
std::vector<AuroraEvent> g_pendingEvents;
AuroraWindowSize g_windowSize;
std::vector<AuroraEvent> g_events;
std::deque<std::string> g_eventStrings;
std::atomic_bool g_backgrounded = false;
#if defined(SDL_PLATFORM_ANDROID)
std::atomic_bool g_surfaceReady = false;
#else
std::atomic_bool g_surfaceReady = true;
#endif
bool g_lastPaused = false;
bool g_gotFocus = false;
std::atomic_bool g_surfaceRestorePending = false;

#ifdef AURORA_ENABLE_GX
bool surface_restore_pending() noexcept { return g_surfaceRestorePending.load(std::memory_order_acquire); }

void retire_surface_for_restore() {
  const bool restore = surface_restore_pending() || gfx::present::is_sdl_xfb();
  g_surfaceRestorePending.store(restore, std::memory_order_release);
  gfx::present::retire_sdl_targets();
}
#endif

void retain_event_strings(SDL_Event& event) {
  switch (event.type) {
  case SDL_EVENT_DROP_BEGIN:
  case SDL_EVENT_DROP_FILE:
  case SDL_EVENT_DROP_TEXT:
  case SDL_EVENT_DROP_COMPLETE:
  case SDL_EVENT_DROP_POSITION:
    break;
  default:
    return;
  }
  if (event.drop.source != nullptr) {
    g_eventStrings.emplace_back(event.drop.source);
    event.drop.source = g_eventStrings.back().c_str();
  }
  if (event.drop.data != nullptr) {
    g_eventStrings.emplace_back(event.drop.data);
    event.drop.data = g_eventStrings.back().c_str();
  }
}

bool operator==(const AuroraWindowSize& lhs, const AuroraWindowSize& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && lhs.fb_width == rhs.fb_width &&
         lhs.fb_height == rhs.fb_height && lhs.native_fb_height == rhs.native_fb_height &&
         lhs.native_fb_width == rhs.native_fb_width && lhs.scale == rhs.scale;
}

#ifndef AURORA_ENABLE_GX
Vec2<int> scale_frame_buffer_to_aspect(int w, int h, float scale, float aspect) {
  if (w <= 0 || h <= 0 || scale <= 0.f || aspect <= 0.f) {
    return {std::max(w, 1), std::max(h, 1)};
  }
  const int baseW = std::max(1, static_cast<int>(std::lround(static_cast<float>(w) * scale)));
  const int baseH = std::max(1, static_cast<int>(std::lround(static_cast<float>(h) * scale)));
  if (aspect >= static_cast<float>(w) / static_cast<float>(h)) {
    return {std::max(1, static_cast<int>(std::lround(static_cast<float>(baseH) * aspect))), baseH};
  }
  return {baseW, std::max(1, static_cast<int>(std::lround(static_cast<float>(baseW) / aspect)))};
}

Vec2<int> fit_frame_buffer_to_aspect(int width, int height, float aspect) {
  if (width <= 0 || height <= 0 || aspect <= 0.f) {
    return {std::max(width, 1), std::max(height, 1)};
  }
  if (static_cast<float>(width) / static_cast<float>(height) > aspect) {
    return {std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * aspect))), height};
  }
  return {width, std::max(1, static_cast<int>(std::lround(static_cast<float>(width) / aspect)))};
}

#endif

void update_size() noexcept {
  const auto size = refresh_window_size();
  if (size == g_windowSize) {
    return;
  }
  if (size.scale != g_windowSize.scale) {
    if (g_windowSize.scale > 0.f) {
      Log.info("Display scale changed to {}", size.scale);
    }
  }
  g_windowSize = size;
  if (g_renderer != nullptr) {
    SDL_SetRenderLogicalPresentation(g_renderer, static_cast<int>(size.native_fb_width),
                                     static_cast<int>(size.native_fb_height), SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderScale(g_renderer, size.scale, size.scale);
  }
}

void set_window_icon() noexcept {
  if (g_config.iconRGBA8 == nullptr) {
    return;
  }
  auto* iconSurface =
      SDL_CreateSurfaceFrom(static_cast<int>(g_config.iconWidth), static_cast<int>(g_config.iconHeight),
                            SDL_GetPixelFormatForMasks(32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
                            g_config.iconRGBA8, static_cast<int>(4 * g_config.iconWidth));
  AURORA_ASSERT(iconSurface != nullptr, "Failed to create icon surface: {}", SDL_GetError());
  TRY_WARN(SDL_SetWindowIcon(g_window, iconSurface), "Failed to set window icon: {}", SDL_GetError());
  SDL_DestroySurface(iconSurface);
}

bool targets_primary_window(const SDL_Event* event) noexcept {
  const SDL_Window* eventWindow = SDL_GetWindowFromEvent(event);
  return eventWindow == nullptr || eventWindow == get_sdl_window();
}

bool SDLCALL lifecycle_event_watch(void*, SDL_Event* event) {
  if (targets_primary_window(event)) {
    switch (event->type) {
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_APPLE)
    case SDL_EVENT_WINDOW_MINIMIZED:
#ifdef AURORA_ENABLE_GX
      if (!gfx::present::is_sdl_xfb() && !surface_restore_pending()) {
        break;
      }
#endif
      time::internal::set_pause_reason(time::internal::PauseReason::Background, true);
      g_backgrounded.store(true, std::memory_order_relaxed);
      break;
    case SDL_EVENT_WINDOW_RESTORED:
      g_backgrounded.store(false, std::memory_order_relaxed);
      time::internal::set_pause_reason(time::internal::PauseReason::Background, false);
      break;
#endif
    default:
      break;
    }
  }
  return true;
}

void sync_paused() {
  const bool paused = is_paused();
  if (g_lastPaused == paused) {
    return;
  }
  g_lastPaused = paused;
  time::internal::set_pause_reason(time::internal::PauseReason::Window, paused);
  g_events.push_back(AuroraEvent{
      .type = paused ? AURORA_PAUSED : AURORA_UNPAUSED,
  });
}

void process_event(SDL_Event& event) {
  const bool primaryWindow = targets_primary_window(&event);
  const bool routed = primaryWindow && input::sdl::is_routed_event(event);
  if (primaryWindow && !routed) {
#ifdef AURORA_ENABLE_GX
    imgui::process_event(event);
#endif
#ifdef AURORA_ENABLE_RMLUI
    rmlui::handle_window_event(event);
#endif
  }

  switch (event.type) {
  case SDL_EVENT_WINDOW_MOVED: {
    if (!primaryWindow) {
      break;
    }
    g_events.push_back(AuroraEvent{
        .type = AURORA_WINDOW_MOVED,
        .windowPos = {.x = event.window.data1, .y = event.window.data2},
    });
    break;
  }
  case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
    if (!primaryWindow) {
      break;
    }
    update_size();
    g_events.push_back(AuroraEvent{
        .type = AURORA_DISPLAY_SCALE_CHANGED,
        .windowSize = refresh_window_size(),
    });
    break;
  }
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
    if (!primaryWindow) {
      break;
    }
    update_size();
    g_events.push_back(AuroraEvent{
        .type = AURORA_WINDOW_RESIZED,
        .windowSize = refresh_window_size(),
    });
    break;
  }
  case SDL_EVENT_GAMEPAD_ADDED: {
    auto instance = gamepad::add_controller(event.gdevice.which);
    if (instance != 0 && instance != static_cast<SDL_JoystickID>(-1)) {
      input::sdl::gamepad_added(instance);
    }
    g_events.push_back(AuroraEvent{
        .type = AURORA_CONTROLLER_ADDED,
        .controller = instance,
    });
    break;
  }
  case SDL_EVENT_GAMEPAD_REMOVED: {
    input::sdl::gamepad_removed(event.gdevice.which);
    gamepad::remove_controller(event.gdevice.which);
    g_events.push_back(AuroraEvent{
        .type = AURORA_CONTROLLER_REMOVED,
        .controller = event.gdevice.which,
    });
    break;
  }
  case SDL_EVENT_GAMEPAD_REMAPPED:
    input::sdl::gamepad_remapped(event.gdevice.which);
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    if (primaryWindow) {
      gamepad::set_mouse_scroll(event.wheel.x, event.wheel.y);
    }
    break;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    if (primaryWindow) {
      input::detail::focus_lost();
    }
    break;
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    if (primaryWindow) {
      g_events.push_back(AuroraEvent{
          .type = AURORA_EXIT,
      });
    }
    break;
  case SDL_EVENT_QUIT:
    g_events.push_back(AuroraEvent{
        .type = AURORA_EXIT,
    });
    break;
  default:
    if (event.type == g_sdlCustomEventsStart + CustomEvent::FrameBufferChanged) {
      update_size();
      g_events.push_back(AuroraEvent{
          .type = AURORA_WINDOW_RESIZED,
          .windowSize = refresh_window_size(),
      });
    }
    break;
  }

  if (routed) {
    input::sdl::dispatch(event);
  }
  if (primaryWindow) {
    sync_paused();
  }
  retain_event_strings(event);
  g_events.push_back(AuroraEvent{
      .type = AURORA_SDL_EVENT,
      .sdl = event,
  });
}
} // namespace

const AuroraEvent* poll_events() {
  ZoneScoped;
  {
    std::lock_guard lock{g_eventMutex};
    g_events = std::move(g_pendingEvents);
    g_pendingEvents.clear();
  }
  g_eventStrings.clear();
  const bool eventsInitialized = SDL_WasInit(SDL_INIT_EVENTS) != 0;

  SDL_Event event;
  // Clear out the previous scroll values to prevent ghost input
  gamepad::set_mouse_scroll(0, 0);
  if (eventsInitialized && is_paused()) {
    ZoneScopedN("SDL_WaitEvent (paused)");
    if (SDL_WaitEvent(&event)) {
      process_event(event);
    } else {
      Log.warn("SDL_WaitEvent failed: {}", SDL_GetError());
    }
  }
  while (eventsInitialized) {
    bool hasEvent = false;
    {
      ZoneScopedN("SDL_PollEvent");
      hasEvent = SDL_PollEvent(&event);
    }
    if (hasEvent) {
      process_event(event);
    } else {
      break;
    }
  }
  input::reconcile();
  input::sdl::update_cursor();
  g_events.push_back(AuroraEvent{
      .type = AURORA_NONE,
  });
  return g_events.data();
}

bool create_window(AuroraBackend backend) {
  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if TARGET_OS_IOS || TARGET_OS_TV
  flags |= SDL_WINDOW_FULLSCREEN;
#else
  flags |= SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
  if (g_config.startFullscreen) {
    flags |= SDL_WINDOW_FULLSCREEN;
  }
#endif
  auto width = static_cast<Sint32>(g_config.surface.width ? g_config.surface.width : g_config.windowWidth);
  auto height = static_cast<Sint32>(g_config.surface.height ? g_config.surface.height : g_config.windowHeight);
  if (width == 0 || height == 0) {
    width = 1280;
    height = 960;
  }
  if (width < 640) {
    width = 640;
  }
  if (height < 480) {
    height = 480;
  }

  Sint32 posX = g_config.windowPosX;
  Sint32 posY = g_config.windowPosY;
  if (posX < 0 || posY < 0) {
    posX = SDL_WINDOWPOS_UNDEFINED;
    posY = SDL_WINDOWPOS_UNDEFINED;
  }

  const auto props = SDL_CreateProperties();
  TRY(SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, g_config.appName), "Failed to set {}: {}",
      SDL_PROP_WINDOW_CREATE_TITLE_STRING, SDL_GetError());
  TRY(SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, posX), "Failed to set {}: {}",
      SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_GetError());
  TRY(SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, posY), "Failed to set {}: {}",
      SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_GetError());
  TRY(SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width), "Failed to set {}: {}",
      SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, SDL_GetError());
  TRY(SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height), "Failed to set {}: {}",
      SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, SDL_GetError());
  TRY(SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, flags), "Failed to set {}: {}",
      SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_GetError());
  TRY(SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, backend != BACKEND_NULL),
      "Failed to set {}: {}", SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, SDL_GetError());
  g_window = SDL_CreateWindowWithProperties(props);
  if (g_window == nullptr) {
    Log.error("Failed to create window: {}", SDL_GetError());
    return false;
  }
  SDL_SetWindowMinimumSize(g_window, 640, 480);
  set_window_icon();
  return true;
}

bool create_renderer() {
  if (g_window == nullptr) {
    return false;
  }
  const auto props = SDL_CreateProperties();
  TRY(SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, g_window), "Failed to set {}: {}",
      SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, SDL_GetError());
  TRY(SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, SDL_RENDERER_VSYNC_ADAPTIVE),
      "Failed to set {}: {}", SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, SDL_GetError());
  g_renderer = SDL_CreateRendererWithProperties(props);
  if (g_renderer == nullptr) {
    Log.error("Failed to create renderer: {}", SDL_GetError());
    return false;
  }
  return true;
}

void destroy_window() {
  if (g_renderer != nullptr) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window != nullptr) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
}

void show_window() {
  if (g_window != nullptr) {
    TRY_WARN(SDL_ShowWindow(g_window), "Failed to show window: {}", SDL_GetError());
  }
}

bool initialize() {
  set_surface_type(g_config.surface.type);
  /* We don't want to initialize anything input related here, otherwise the add events will get lost to the void */
  TRY(SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight"), "Error setting {}: {}", SDL_HINT_ORIENTATIONS,
      SDL_GetError());
  g_initializedSubsystems = surface_type() == AURORA_SURFACE_SDL_MANAGED ? SDL_INIT_EVENTS | SDL_INIT_VIDEO
                            : surface_type() == AURORA_SURFACE_HEADLESS  ? 0
                                                                         : SDL_INIT_EVENTS;
  if (g_initializedSubsystems) {
    TRY(SDL_InitSubSystem(g_initializedSubsystems), "Error initializing SDL: {}", SDL_GetError());
  }
  if (surface_type() != AURORA_SURFACE_SDL_MANAGED) {
    g_surfaceReady.store(true, std::memory_order_release);
  }
  time::internal::set_pause_reason(time::internal::PauseReason::Surface,
                                   !g_surfaceReady.load(std::memory_order_acquire));

#if !defined(_WIN32) && !defined(__APPLE__)
  TRY(SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"), "Error setting {}: {}",
      SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, SDL_GetError());
#endif
  TRY(SDL_SetHint(SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME, g_config.appName), "Error setting {}: {}",
      SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME, SDL_GetError());
  TRY(SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE_RUMBLE_BRAKE, "1"), "Error setting {}: {}",
      SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE_RUMBLE_BRAKE, SDL_GetError());
  // Treat touch and mouse as separate input sources
  TRY(SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"), "Error setting {}: {}", SDL_HINT_TOUCH_MOUSE_EVENTS,
      SDL_GetError());
  TRY(SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0"), "Error setting {}: {}", SDL_HINT_MOUSE_TOUCH_EVENTS,
      SDL_GetError());

  if ((g_initializedSubsystems & SDL_INIT_VIDEO) != 0) {
    TRY(SDL_DisableScreenSaver(), "Error disabling screensaver: {}", SDL_GetError());
  }
  if (g_config.allowJoystickBackgroundEvents) {
    TRY(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"), "Error setting {}: {}",
        SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, SDL_GetError());
  }

  return true;
}

bool initialize_event_watch() {
  if (!g_initializedSubsystems) {
    return true;
  }
  TRY(SDL_AddEventWatch(lifecycle_event_watch, nullptr), "Error adding SDL event watch: {}", SDL_GetError());
  return true;
}

void shutdown() {
  if (g_initializedSubsystems) {
    SDL_RemoveEventWatch(lifecycle_event_watch, nullptr);
  }
  destroy_window();
  if ((g_initializedSubsystems & SDL_INIT_VIDEO) != 0) {
    TRY_WARN(SDL_EnableScreenSaver(), "Error enabling screensaver: {}", SDL_GetError());
  }
  if (g_initializedSubsystems) {
    SDL_QuitSubSystem(g_initializedSubsystems);
  }
  g_initializedSubsystems = 0;
  g_windowSize = {};
  g_lastPaused = false;
  g_gotFocus = false;
  g_backgrounded.store(false, std::memory_order_release);
  clear_surface_restore();
  std::lock_guard lock{g_eventMutex};
  g_pendingEvents.clear();
}

AuroraWindowSize get_window_size() {
#ifdef AURORA_ENABLE_GX
  return gfx::efb::applied_size();
#else
  return refresh_window_size();
#endif
}

AuroraWindowSize refresh_window_size() {
  if (!g_window) {
#ifdef AURORA_ENABLE_GX
    return gfx::efb::pending_size();
#else
    const auto [w, h] = vi::configured_fb_size();
    return {
        .width = w,
        .height = h,
        .fb_width = w,
        .fb_height = h,
        .native_fb_width = w,
        .native_fb_height = h,
        .scale = 1.f,
    };
#endif
  }

  int width = 0;
  int height = 0;
  int native_fb_w = 0;
  int native_fb_h = 0;
  AURORA_ASSERT(SDL_GetWindowSize(g_window, &width, &height), "Failed to get window size: {}", SDL_GetError());
  AURORA_ASSERT(SDL_GetWindowSizeInPixels(g_window, &native_fb_w, &native_fb_h),
                "Failed to get window size in pixels: {}", SDL_GetError());

#ifdef AURORA_ENABLE_GX
  const auto scale = SDL_GetWindowDisplayScale(g_window);
  if (gfx::present::is_sdl_xfb()) {
    gfx::present::resize_sdl_target(gfx::xfb_target(), width, height, native_fb_w, native_fb_h, scale);
  }

  return gfx::efb::resolve_size({
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .native_fb_width = static_cast<uint32_t>(native_fb_w),
      .native_fb_height = static_cast<uint32_t>(native_fb_h),
      .scale = scale,
  });
#else
  int fb_w = native_fb_w;
  int fb_h = native_fb_h;
  if (g_frameBufferScale > 0.f) {
    const auto [baseW, baseH] = vi::configured_fb_size();
    const auto [scaledW, scaledH] =
        scale_frame_buffer_to_aspect(static_cast<int>(baseW), static_cast<int>(baseH), g_frameBufferScale,
                                     static_cast<float>(fb_w) / static_cast<float>(fb_h));
    fb_w = scaledW;
    fb_h = scaledH;
  }
  if (g_frameBufferAspectFit) {
    const auto [baseW, baseH] = vi::configured_fb_size();
    if (baseW > 0 && baseH > 0) {
      const auto [fitW, fitH] =
          fit_frame_buffer_to_aspect(fb_w, fb_h, static_cast<float>(baseW) / static_cast<float>(baseH));
      fb_w = fitW;
      fb_h = fitH;
    }
  }

  const float scale = SDL_GetWindowDisplayScale(g_window);
  return {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .fb_width = static_cast<uint32_t>(fb_w),
      .fb_height = static_cast<uint32_t>(fb_h),
      .native_fb_width = static_cast<uint32_t>(native_fb_w),
      .native_fb_height = static_cast<uint32_t>(native_fb_h),
      .scale = scale,
  };
#endif
}

void queue_size_events(const AuroraWindowSize& oldSize, const AuroraWindowSize& size) {
  if (oldSize == size) {
    return;
  }

  std::lock_guard lock{g_eventMutex};
  if (oldSize.scale != size.scale) {
    g_pendingEvents.push_back({
        .type = AURORA_DISPLAY_SCALE_CHANGED,
        .windowSize = size,
    });
  }
  g_pendingEvents.push_back({
      .type = AURORA_WINDOW_RESIZED,
      .windowSize = size,
  });
}

SDL_Window* get_sdl_window() { return surface_type() == AURORA_SURFACE_SDL_MANAGED ? g_window : nullptr; }

#ifdef AURORA_ENABLE_GX
wgpu::Surface create_sdl_surface(const wgpu::Instance& instance, const char* label) {
  return webgpu::create_window_surface(instance, g_window, label);
}
#endif

AuroraSurfaceType surface_type() noexcept { return g_surfaceType.load(std::memory_order_acquire); }
void set_surface_type(AuroraSurfaceType type) noexcept { g_surfaceType.store(type, std::memory_order_release); }

SDL_Renderer* get_sdl_renderer() { return g_renderer; }

bool is_paused() noexcept {
  if (surface_type() != AURORA_SURFACE_SDL_MANAGED || !g_window) {
    return false;
  }
#ifdef AURORA_ENABLE_GX
  if (!gfx::present::is_sdl_xfb() && !surface_restore_pending()) {
    return false;
  }
#endif
  if (!is_presentable()) {
    return true;
  }
  const auto flags = SDL_GetWindowFlags(g_window);
  if ((flags & SDL_WINDOW_HIDDEN) != 0u) {
    return true;
  }
  // Wait until the window has received focus before respecting pauseOnFocusLost
  if (!g_gotFocus) {
    g_gotFocus = (flags & SDL_WINDOW_INPUT_FOCUS) != 0u;
    return false;
  }
  return g_config.pauseOnFocusLost && ((flags & SDL_WINDOW_INPUT_FOCUS) == 0u || (flags & SDL_WINDOW_MINIMIZED) != 0u);
}

bool is_presentable() noexcept {
  return g_window != nullptr && !g_backgrounded.load(std::memory_order_acquire) &&
         g_surfaceReady.load(std::memory_order_acquire);
}

void set_surface_ready(bool ready) noexcept {
  g_surfaceReady.store(ready, std::memory_order_release);
#if defined(AURORA_ENABLE_GX) && defined(SDL_PLATFORM_ANDROID)
  // JNI calls this before SDL destroys the ANativeWindow. Pending scene work
  // owns EFB views only, so retire the target without draining that work.
  if (!ready && surface_type() == AURORA_SURFACE_SDL_MANAGED) {
    retire_surface_for_restore();
  }
#endif
  const bool managedOutput = surface_type() == AURORA_SURFACE_SDL_MANAGED &&
#ifdef AURORA_ENABLE_GX
                             (gfx::present::is_sdl_xfb() || surface_restore_pending());
#else
                             g_window != nullptr;
#endif
  time::internal::set_pause_reason(time::internal::PauseReason::Surface, managedOutput && !ready);
}

bool prepare_frame() {
#ifdef AURORA_ENABLE_GX
  if (gfx::present::is_sdl_xfb() || (surface_type() == AURORA_SURFACE_SDL_MANAGED && surface_restore_pending())) {
    if (!is_presentable()) {
      retire_surface_for_restore();
      return false;
    }
    if (is_paused()) {
      return false;
    }
    if (!gfx::present::ensure_sdl_target()) {
      return false;
    }
    clear_surface_restore();
  }
#endif
  return true;
}

void clear_surface_restore() noexcept { g_surfaceRestorePending.store(false, std::memory_order_release); }

SurfaceLock::SurfaceLock(bool enabled) noexcept : m_locked(enabled) {
#if defined(SDL_PLATFORM_ANDROID)
  if (m_locked) {
    Android_LockActivityMutex();
  }
#endif
}

SurfaceLock::~SurfaceLock() {
#if defined(SDL_PLATFORM_ANDROID)
  if (m_locked) {
    Android_UnlockActivityMutex();
  }
#endif
}

bool push_custom_event(CustomEvent eventType) {
  if (!g_sdlCustomEventsStart || !SDL_WasInit(SDL_INIT_EVENTS)) {
    return false;
  }
  SDL_Event event{.type = g_sdlCustomEventsStart + eventType};
  return SDL_PushEvent(&event);
}

void set_title(const char* title) {
  if (!get_sdl_window()) {
    return;
  }
  TRY_WARN(SDL_SetWindowTitle(g_window, title), "Failed to set window title: {}", SDL_GetError());
}

void set_fullscreen(bool fullscreen) {
  if (!get_sdl_window()) {
    return;
  }
  TRY_WARN(SDL_SetWindowFullscreen(g_window, fullscreen), "Failed to set window fullscreen: {}", SDL_GetError());
}

bool get_fullscreen() { return get_sdl_window() && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0u; }

void set_window_size(uint32_t width, uint32_t height) {
  if (!get_sdl_window()) {
    return;
  }
  TRY_WARN(SDL_RestoreWindow(g_window), "Failed to un-maximize window: {}", SDL_GetError());
  TRY_WARN(SDL_SetWindowSize(g_window, width, height), "Failed to set window size: {}", SDL_GetError());
}

void set_window_position(uint32_t x, uint32_t y) {
  if (!get_sdl_window()) {
    return;
  }
  TRY_WARN(SDL_SetWindowPosition(g_window, x, y), "Failed to set window position: {}", SDL_GetError());
}

void center_window() {
  if (!get_sdl_window()) {
    return;
  }
  TRY_WARN(SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED),
           "Failed to center window: {}", SDL_GetError());
}

void notify_frame_buffer_change() {
  if (get_sdl_window() != nullptr) {
    TRY_WARN(push_custom_event(CustomEvent::FrameBufferChanged), "Failed to push framebuffer change event: {}",
             SDL_GetError());
  } else {
#ifdef AURORA_ENABLE_GX
    queue_size_events(get_window_size(), gfx::efb::pending_size());
#endif
  }
}

void set_frame_buffer_scale(float scale) {
#ifdef AURORA_ENABLE_GX
  if (!gfx::efb::set_scale(scale)) {
    return;
  }
#else
  scale = std::max(0.f, scale);
  if (g_frameBufferScale == scale) {
    return;
  }
  g_frameBufferScale = scale;
#endif
  notify_frame_buffer_change();
}

void set_frame_buffer_aspect_fit(bool fit) {
#ifdef AURORA_ENABLE_GX
  if (!gfx::efb::set_aspect_fit(fit)) {
    return;
  }
#else
  if (g_frameBufferAspectFit == fit) {
    return;
  }
  g_frameBufferAspectFit = fit;
#endif
  notify_frame_buffer_change();
}

void set_background_input(bool value) { SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, value ? "1" : "0"); }

} // namespace aurora::window

#if defined(SDL_PLATFORM_ANDROID)
extern "C" JNIEXPORT void JNICALL Java_dev_encounter_aurora_AuroraSurface_nativeSetSurfaceReady(JNIEnv*, jclass,
                                                                                                jboolean ready) {
  aurora::window::set_surface_ready(ready == JNI_TRUE);
}
#endif

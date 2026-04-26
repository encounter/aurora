#include "window.hpp"

#ifdef AURORA_ENABLE_GX
#include "imgui.hpp"
#include "webgpu/gpu.hpp"
#endif
#include "input.hpp"
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

#include <algorithm>
#include <cmath>
#include <vector>

#include "rmlui.hpp"
#include "dolphin/vi/vi_internal.hpp"

namespace aurora::window {
namespace {
Module Log("aurora::window");

SDL_Window* g_window;
SDL_Renderer* g_renderer;
float g_frameBufferScale = 0.f;
AuroraWindowSize g_windowSize;
std::vector<AuroraEvent> g_events;

inline bool operator==(const AuroraWindowSize& lhs, const AuroraWindowSize& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && lhs.fb_width == rhs.fb_width &&
         lhs.fb_height == rhs.fb_height && lhs.native_fb_height == rhs.native_fb_height &&
         lhs.native_fb_width == rhs.native_fb_width && lhs.scale == rhs.scale;
}

Vec2<int> scale_frame_buffer_to_aspect(int base_width, int base_height, float scale, float aspect) {
  if (base_width <= 0 || base_height <= 0 || scale <= 0.f || aspect <= 0.f) {
    return {std::max(base_width, 1), std::max(base_height, 1)};
  }

  const int scaled_base_width = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_width) * scale)));
  const int scaled_base_height = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_height) * scale)));
  const float base_aspect = static_cast<float>(base_width) / static_cast<float>(base_height);
  if (aspect >= base_aspect) {
    return {
        std::max(1, static_cast<int>(std::lround(static_cast<float>(scaled_base_height) * aspect))),
        scaled_base_height,
    };
  }

  return {
      scaled_base_width,
      std::max(1, static_cast<int>(std::lround(static_cast<float>(scaled_base_width) / aspect))),
  };
}

void resize_swapchain() noexcept {
  const auto size = get_window_size();
  if (size == g_windowSize) {
    return;
  }
  if (size.scale != g_windowSize.scale) {
    if (g_windowSize.scale > 0.f) {
      Log.info("Display scale changed to {}", size.scale);
    }
  }
  g_windowSize = size;
#ifdef AURORA_ENABLE_GX
  webgpu::resize_swapchain(size.fb_width, size.fb_height, size.native_fb_width, size.native_fb_height);
#endif
}

void set_window_icon() noexcept {
  if (g_config.iconRGBA8 == nullptr) {
    return;
  }
  auto* iconSurface =
      SDL_CreateSurfaceFrom(static_cast<int>(g_config.iconWidth), static_cast<int>(g_config.iconHeight),
                            SDL_GetPixelFormatForMasks(32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
                            g_config.iconRGBA8, static_cast<int>(4 * g_config.iconWidth));
  ASSERT(iconSurface != nullptr, "Failed to create icon surface: {}", SDL_GetError());
  TRY_WARN(SDL_SetWindowIcon(g_window, iconSurface), "Failed to set window icon: {}", SDL_GetError());
  SDL_DestroySurface(iconSurface);
}
} // namespace

const AuroraEvent* poll_events() {
  g_events.clear();

  SDL_Event event;
  // Clear out the previous scroll values to prevent ghost input
  input::set_mouse_scroll(0, 0);
  while (SDL_PollEvent(&event)) {
#ifdef AURORA_ENABLE_GX
    imgui::process_event(event);
#endif

#ifdef AURORA_ENABLE_RMLUI
    rmlui::handle_event(event);
#endif

    switch (event.type) {
    case SDL_EVENT_WINDOW_MINIMIZED: {
      // Android/iOS: Application backgrounded
      g_events.push_back(AuroraEvent{
          .type = AURORA_PAUSED,
      });
      break;
    }
    case SDL_EVENT_WINDOW_RESTORED: {
      // Android/iOS: Application focused
      g_events.push_back(AuroraEvent{
          .type = AURORA_UNPAUSED,
      });
      break;
    }
    case SDL_EVENT_RENDER_DEVICE_RESET: {
      Log.info("Render device reset, recreating surface");
#ifdef AURORA_ENABLE_GX
      webgpu::refresh_surface(true);
#endif
      break;
    }
#if defined(ANDROID)
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
      g_events.push_back(AuroraEvent{
          .type = AURORA_PAUSED,
      });
      break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED: {
      g_events.push_back(AuroraEvent{
          .type = AURORA_UNPAUSED,
      });
      break;
    }
#endif
    case SDL_EVENT_WINDOW_MOVED: {
      g_events.push_back(AuroraEvent{
          .type = AURORA_WINDOW_MOVED,
          .windowPos = {.x = event.window.data1, .y = event.window.data2},
      });
      break;
    }
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
      resize_swapchain();
      g_events.push_back(AuroraEvent{
          .type = AURORA_DISPLAY_SCALE_CHANGED,
          .windowSize = get_window_size(),
      });
      break;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
      resize_swapchain();
      g_events.push_back(AuroraEvent{
          .type = AURORA_WINDOW_RESIZED,
          .windowSize = get_window_size(),
      });
      break;
    }
    case SDL_EVENT_GAMEPAD_ADDED: {
      auto instance = input::add_controller(event.gdevice.which);
      g_events.push_back(AuroraEvent{
          .type = AURORA_CONTROLLER_ADDED,
          .controller = instance,
      });
      break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
      input::remove_controller(event.gdevice.which);
      g_events.push_back(AuroraEvent{
          .type = AURORA_CONTROLLER_REMOVED,
          .controller = event.gdevice.which,
      });
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
      input::set_mouse_scroll(event.wheel.x, event.wheel.y);
      break;
    case SDL_EVENT_QUIT:
      g_events.push_back(AuroraEvent{
          .type = AURORA_EXIT,
      });
    default:
      if (event.type == g_sdlCustomEventsStart) {
        // Future resize event
        resize_swapchain();
        g_events.push_back(AuroraEvent{
            .type = AURORA_WINDOW_RESIZED,
            .windowSize = get_window_size(),
        });
      } else if (event.type == g_sdlCustomEventsStart + 1) {
        // Refresh surface (vsync changed)
#ifdef AURORA_ENABLE_GX
        webgpu::refresh_surface(false);
#endif
      }
      break;
    }

    g_events.push_back(AuroraEvent{
        .type = AURORA_SDL_EVENT,
        .sdl = event,
    });
  }
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
  switch (backend) {
#ifdef AURORA_ENABLE_GX
#ifdef DAWN_ENABLE_BACKEND_VULKAN
  case BACKEND_VULKAN:
    flags |= SDL_WINDOW_VULKAN;
    break;
#endif
#ifdef DAWN_ENABLE_BACKEND_METAL
  case BACKEND_METAL:
    flags |= SDL_WINDOW_METAL;
    break;
#endif
#ifdef DAWN_ENABLE_BACKEND_OPENGL
  case BACKEND_OPENGL:
  case BACKEND_OPENGLES:
    flags |= SDL_WINDOW_OPENGL;
    break;
#endif
#endif
  default:
    break;
  }
#ifdef __SWITCH__
  Sint32 width = 1280;
  Sint32 height = 720;
#else
  auto width = static_cast<Sint32>(g_config.windowWidth);
  auto height = static_cast<Sint32>(g_config.windowHeight);
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

#endif
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
  /* We don't want to initialize anything input related here, otherwise the add events will get lost to the void */
  TRY(SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO), "Error initializing SDL: {}", SDL_GetError());

#if !defined(_WIN32) && !defined(__APPLE__)
  TRY(SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"), "Error setting {}: {}",
      SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, SDL_GetError());
#endif
  TRY(SDL_SetHint(SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME, g_config.appName), "Error setting {}: {}",
      SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME, SDL_GetError());
  TRY(SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE_RUMBLE_BRAKE, "1"), "Error setting {}: {}",
      SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE_RUMBLE_BRAKE, SDL_GetError());

  TRY(SDL_DisableScreenSaver(), "Error disabling screensaver: {}", SDL_GetError());
  if (g_config.allowJoystickBackgroundEvents) {
    TRY(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"), "Error setting {}: {}",
        SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, SDL_GetError());
  }

  return true;
}

void shutdown() {
  destroy_window();
  TRY_WARN(SDL_EnableScreenSaver(), "Error enabling screensaver: {}", SDL_GetError());
  SDL_Quit();
}

AuroraWindowSize get_window_size() {
  int width = 0;
  int height = 0;
  int native_fb_w = 0;
  int native_fb_h = 0;
  ASSERT(SDL_GetWindowSize(g_window, &width, &height), "Failed to get window size: {}", SDL_GetError());
  ASSERT(SDL_GetWindowSizeInPixels(g_window, &native_fb_w, &native_fb_h), "Failed to get window size in pixels: {}",
         SDL_GetError());

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
}

SDL_Window* get_sdl_window() { return g_window; }

SDL_Renderer* get_sdl_renderer() { return g_renderer; }

void set_title(const char* title) {
  TRY_WARN(SDL_SetWindowTitle(g_window, title), "Failed to set window title: {}", SDL_GetError());
}

void set_fullscreen(bool fullscreen) {
  TRY_WARN(SDL_SetWindowFullscreen(g_window, fullscreen), "Failed to set window fullscreen: {}", SDL_GetError());
}

bool get_fullscreen() { return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0u; }

void set_window_size(uint32_t width, uint32_t height) {
  TRY_WARN(SDL_RestoreWindow(g_window), "Failed to un-maximize window: {}", SDL_GetError());
  TRY_WARN(SDL_SetWindowSize(g_window, width, height), "Failed to set window size: {}", SDL_GetError());
}

void set_window_position(uint32_t x, uint32_t y) {
  TRY_WARN(SDL_SetWindowPosition(g_window, x, y), "Failed to set window position: {}", SDL_GetError());
}

void center_window() {
  TRY_WARN(SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED),
           "Failed to center window: {}", SDL_GetError());
}

static void push_future_resize_event() {
  SDL_Event event{.type = g_sdlCustomEventsStart};
  TRY_WARN(SDL_PushEvent(&event), "Failed to push SDL event for future resize: {}", SDL_GetError());
}

void set_frame_buffer_scale(float scale) {
  if (scale < 0.f) {
    scale = 0.f;
  }
  if (g_frameBufferScale == scale) {
    return;
  }
  g_frameBufferScale = scale;
  if (g_window != nullptr) {
    // Defer so that we don't try to reconfigure the surface in the middle of game logic.
    push_future_resize_event();
  }
}

} // namespace aurora::window

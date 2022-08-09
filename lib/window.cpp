#include "window.hpp"

#include "imgui.hpp"
#include "webgpu/gpu.hpp"
#include "input.hpp"
#include "internal.hpp"

#include <aurora/event.h>
#include <SDL.h>

namespace aurora::window {
static Module Log("aurora::window");

static SDL_Window* g_window;
static SDL_Renderer* g_renderer;
static AuroraWindowSize g_windowSize;
static std::vector<AuroraEvent> g_events;

static inline bool operator==(const AuroraWindowSize& lhs, const AuroraWindowSize& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && lhs.fb_width == rhs.fb_width &&
         lhs.fb_height == rhs.fb_height && lhs.scale == rhs.scale;
}

static void resize_swapchain(bool force) noexcept {
  const auto size = get_window_size();
  if (!force && size == g_windowSize) {
    return;
  }
  if (size.scale != g_windowSize.scale) {
    if (g_windowSize.scale > 0.f) {
      Log.report(LOG_INFO, FMT_STRING("Display scale changed to {}"), size.scale);
    }
  }
  g_windowSize = size;
  webgpu::resize_swapchain(size.fb_width, size.fb_height);
}

const AuroraEvent* poll_events() {
  g_events.clear();

  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    imgui::process_event(event);

    switch (event.type) {
    case SDL_WINDOWEVENT: {
      switch (event.window.event) {
      case SDL_WINDOWEVENT_MINIMIZED: {
        // Android/iOS: Application backgrounded
        g_events.push_back(AuroraEvent{
            .type = AURORA_PAUSED,
        });
        break;
      }
      case SDL_WINDOWEVENT_RESTORED: {
        // Android/iOS: Application focused
        g_events.push_back(AuroraEvent{
            .type = AURORA_UNPAUSED,
        });
        break;
      }
      case SDL_WINDOWEVENT_SIZE_CHANGED: {
        resize_swapchain(false);
        g_events.push_back(AuroraEvent{
            .type = AURORA_WINDOW_RESIZED,
            .windowSize = get_window_size(),
        });
        break;
      }
      }
      break;
    }
    case SDL_CONTROLLERDEVICEADDED: {
      auto instance = input::add_controller(event.cdevice.which);
      g_events.push_back(AuroraEvent{
          .type = AURORA_CONTROLLER_ADDED,
          .controller = instance,
      });
      break;
    }
    case SDL_CONTROLLERDEVICEREMOVED: {
      input::remove_controller(event.cdevice.which);
      g_events.push_back(AuroraEvent{
          .type = AURORA_CONTROLLER_REMOVED,
          .controller = event.cdevice.which,
      });
      break;
    }
    case SDL_QUIT:
      g_events.push_back(AuroraEvent{
          .type = AURORA_EXIT,
      });
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

static void set_window_icon() noexcept {
  if (g_config.iconRGBA8 == nullptr) {
    return;
  }
  auto* iconSurface = SDL_CreateRGBSurfaceFrom(g_config.iconRGBA8, g_config.iconWidth, g_config.iconHeight, 32,
                                               4 * g_config.iconWidth, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
  ASSERT(iconSurface != nullptr, "Failed to create icon surface: {}", SDL_GetError());
  SDL_SetWindowIcon(g_window, iconSurface);
  SDL_FreeSurface(iconSurface);
}

bool create_window(AuroraBackend backend) {
  Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
#if TARGET_OS_IOS || TARGET_OS_TV
  flags |= SDL_WINDOW_FULLSCREEN;
#else
  flags |= SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
  if (g_config.startFullscreen) {
    flags |= SDL_WINDOW_FULLSCREEN;
  }
#endif
  switch (backend) {
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
  default:
    break;
  }
#ifdef __SWITCH__
  uint32_t width = 1280;
  uint32_t height = 720;
#else
  uint32_t width = g_config.windowWidth;
  uint32_t height = g_config.windowHeight;
  if (width == 0 || height == 0) {
    width = 1280;
    height = 960;
  }
#endif
  g_window = SDL_CreateWindow(g_config.appName, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
  if (g_window == nullptr) {
    return false;
  }
  set_window_icon();
  return true;
}

bool create_renderer() {
  if (g_window == nullptr) {
    return false;
  }
  const auto flags = SDL_RENDERER_PRESENTVSYNC;
  g_renderer = SDL_CreateRenderer(g_window, -1, flags | SDL_RENDERER_ACCELERATED);
  if (g_renderer == nullptr) {
    // Attempt fallback to SW renderer
    g_renderer = SDL_CreateRenderer(g_window, -1, flags);
  }
  return g_renderer != nullptr;
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
    SDL_ShowWindow(g_window);
  }
}

bool initialize() {
  ASSERT(SDL_Init(SDL_INIT_EVERYTHING & ~SDL_INIT_HAPTIC) == 0, "Error initializing SDL: {}", SDL_GetError());

#if !defined(_WIN32) && !defined(__APPLE__)
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 18)
  SDL_SetHint(SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME, g_config.appName);
#endif
#ifdef SDL_HINT_JOYSTICK_GAMECUBE_RUMBLE_BRAKE
  SDL_SetHint(SDL_HINT_JOYSTICK_GAMECUBE_RUMBLE_BRAKE, "1");
#endif

  SDL_DisableScreenSaver();
  /* TODO: Make this an option rather than hard coding it */
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

  return true;
}

void shutdown() {
  destroy_window();
  SDL_EnableScreenSaver();
  SDL_Quit();
}

AuroraWindowSize get_window_size() {
  int width, height, fb_w, fb_h;
  SDL_GetWindowSize(g_window, &width, &height);
#if DAWN_ENABLE_BACKEND_METAL
  SDL_Metal_GetDrawableSize(g_window, &fb_w, &fb_h);
#else
  SDL_GL_GetDrawableSize(g_window, &fb_w, &fb_h);
#endif
  float scale = static_cast<float>(fb_w) / static_cast<float>(width);
#ifndef __APPLE__
  if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(g_window), nullptr, &scale, nullptr) == 0) {
    scale /= 96.f;
  }
#endif
  return {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .fb_width = static_cast<uint32_t>(fb_w),
      .fb_height = static_cast<uint32_t>(fb_h),
      .scale = scale,
  };
}

SDL_Window* get_sdl_window() { return g_window; }

SDL_Renderer* get_sdl_renderer() { return g_renderer; }

void set_title(const char* title) { SDL_SetWindowTitle(g_window, title); }

void set_fullscreen(bool fullscreen) { SDL_SetWindowFullscreen(g_window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0); }

bool get_fullscreen() { return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0u; }

} // namespace aurora::window

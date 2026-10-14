#include "BackendBinding.hpp"

#include <aurora/webgpu.hpp>
#include "../window.hpp"

#include <memory>

#include <SDL3/SDL_video.h>
#if defined(SDL_PLATFORM_WIN32)
#include <windows.h>
#endif

namespace aurora::webgpu {
namespace utils {

std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptorCocoa(SDL_Window* window);
std::shared_ptr<wgpu::ChainedStruct> SetupNativeViewAndGetSurfaceDescriptorCocoa(void* view, float scale);

std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window* window) {
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
  return SetupWindowAndGetSurfaceDescriptorCocoa(window);
#else
  const auto props = SDL_GetWindowProperties(window);
#if defined(SDL_PLATFORM_ANDROID)
  std::shared_ptr<wgpu::SurfaceSourceAndroidNativeWindow> desc =
      std::make_shared<wgpu::SurfaceSourceAndroidNativeWindow>();
  desc->window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
  return std::move(desc);
#elif defined(SDL_PLATFORM_WIN32)
  std::shared_ptr<wgpu::SurfaceSourceWindowsHWND> desc = std::make_shared<wgpu::SurfaceSourceWindowsHWND>();
  desc->hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  desc->hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  return std::move(desc);
#elif defined(SDL_PLATFORM_LINUX)
  const char* driver = SDL_GetCurrentVideoDriver();
  if (SDL_strcmp(driver, "wayland") == 0) {
    std::shared_ptr<wgpu::SurfaceSourceWaylandSurface> desc = std::make_shared<wgpu::SurfaceSourceWaylandSurface>();
    desc->display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    desc->surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    return std::move(desc);
  }
  if (SDL_strcmp(driver, "x11") == 0) {
    std::shared_ptr<wgpu::SurfaceSourceXlibWindow> desc = std::make_shared<wgpu::SurfaceSourceXlibWindow>();
    desc->display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    desc->window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    return std::move(desc);
  }
#endif
  return nullptr;
#endif
}

} // namespace utils

wgpu::Surface create_window_surface(const wgpu::Instance& instance, SDL_Window* window, const char* label) {
  if (!instance || window == nullptr) {
    return nullptr;
  }
  const auto chainedDescriptor = utils::SetupWindowAndGetSurfaceDescriptor(window);
  if (!chainedDescriptor) {
    return nullptr;
  }
  const wgpu::SurfaceDescriptor descriptor{
      .nextInChain = chainedDescriptor.get(),
      .label = label,
  };
  return instance.CreateSurface(&descriptor);
}

wgpu::Surface create_surface(const wgpu::Instance& instance, const AuroraSurfaceDesc& desc, const char* label) {
  if (!instance) {
    return nullptr;
  }
  std::shared_ptr<wgpu::ChainedStruct> source;
  switch (desc.type) {
  case AURORA_SURFACE_SDL_MANAGED:
    return window::create_sdl_surface(instance, label);
#if defined(SDL_PLATFORM_WIN32)
  case AURORA_SURFACE_WIN32: {
    if (!desc.win32.hwnd) {
      return nullptr;
    }
    auto native = std::make_shared<wgpu::SurfaceSourceWindowsHWND>();
    native->hwnd = desc.win32.hwnd;
    native->hinstance = desc.win32.hinstance ? desc.win32.hinstance : GetModuleHandleW(nullptr);
    source = std::move(native);
    break;
  }
#endif
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
  case AURORA_SURFACE_METAL_LAYER: {
    if (!desc.metal.layer) {
      return nullptr;
    }
    auto native = std::make_shared<wgpu::SurfaceSourceMetalLayer>();
    native->layer = desc.metal.layer;
    source = std::move(native);
    break;
  }
#endif
#if defined(SDL_PLATFORM_MACOS)
  case AURORA_SURFACE_COCOA_VIEW:
    source = utils::SetupNativeViewAndGetSurfaceDescriptorCocoa(desc.cocoa.nsView, desc.scale);
    break;
#endif
#if defined(SDL_PLATFORM_LINUX)
  case AURORA_SURFACE_WAYLAND: {
    if (!desc.wayland.display || !desc.wayland.surface) {
      return nullptr;
    }
    auto native = std::make_shared<wgpu::SurfaceSourceWaylandSurface>();
    native->display = desc.wayland.display;
    native->surface = desc.wayland.surface;
    source = std::move(native);
    break;
  }
  case AURORA_SURFACE_X11: {
    if (!desc.x11.display || !desc.x11.window) {
      return nullptr;
    }
    auto native = std::make_shared<wgpu::SurfaceSourceXlibWindow>();
    native->display = desc.x11.display;
    native->window = desc.x11.window;
    source = std::move(native);
    break;
  }
#endif
#if defined(SDL_PLATFORM_ANDROID)
  case AURORA_SURFACE_ANDROID: {
    if (!desc.android.window) {
      return nullptr;
    }
    auto native = std::make_shared<wgpu::SurfaceSourceAndroidNativeWindow>();
    native->window = desc.android.window;
    source = std::move(native);
    break;
  }
#endif
  default:
    return nullptr;
  }
  if (!source) {
    return nullptr;
  }
  const wgpu::SurfaceDescriptor descriptor{
      .nextInChain = source.get(),
      .label = label,
  };
  return instance.CreateSurface(&descriptor);
}

} // namespace aurora::webgpu

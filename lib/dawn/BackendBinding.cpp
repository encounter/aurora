#include "BackendBinding.hpp"

#include <memory>

#if defined(DAWN_ENABLE_BACKEND_D3D11)
#include <dawn/native/D3D11Backend.h>
#endif
#if defined(DAWN_ENABLE_BACKEND_D3D12)
#include <dawn/native/D3D12Backend.h>
#endif
#if defined(DAWN_ENABLE_BACKEND_METAL)
#include <dawn/native/MetalBackend.h>
#endif
#if defined(DAWN_ENABLE_BACKEND_VULKAN)
#include <dawn/native/VulkanBackend.h>
#endif
#if defined(DAWN_ENABLE_BACKEND_OPENGL)
#include <dawn/native/OpenGLBackend.h>
#endif
#if defined(DAWN_ENABLE_BACKEND_NULL)
#include <dawn/native/NullBackend.h>
#endif

#if !defined(SDL_PLATFORM_MACOS)
#include <SDL3/SDL_video.h>
#endif

namespace aurora::webgpu::utils {
std::unique_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptorCocoa(SDL_Window* window);

std::unique_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window* window) {
#if defined(SDL_PLATFORM_MACOS)
  return SetupWindowAndGetSurfaceDescriptorCocoa(window);
#else
  const auto props = SDL_GetWindowProperties(window);
#if defined(SDL_PLATFORM_ANDROID)
  std::unique_ptr<wgpu::SurfaceSourceAndroidNativeWindow> desc =
      std::make_unique<wgpu::SurfaceSourceAndroidNativeWindow>();
  desc->window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
  return std::move(desc);
#elif defined(SDL_PLATFORM_WIN32)
  std::unique_ptr<wgpu::SurfaceSourceWindowsHWND> desc = std::make_unique<wgpu::SurfaceSourceWindowsHWND>();
  desc->hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  desc->hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  return std::move(desc);
#elif defined(SDL_PLATFORM_LINUX)
  const char* driver = SDL_GetCurrentVideoDriver();
  if (SDL_strcmp(driver, "wayland") == 0) {
    std::unique_ptr<wgpu::SurfaceSourceWaylandSurface> desc = std::make_unique<wgpu::SurfaceSourceWaylandSurface>();
    desc->display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    desc->surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    return std::move(desc);
  }
  if (SDL_strcmp(driver, "x11") == 0) {
    std::unique_ptr<wgpu::SurfaceSourceXlibWindow> desc = std::make_unique<wgpu::SurfaceSourceXlibWindow>();
    desc->display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    desc->window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    return std::move(desc);
  }
#endif
  return nullptr;
#endif
}

} // namespace aurora::webgpu::utils

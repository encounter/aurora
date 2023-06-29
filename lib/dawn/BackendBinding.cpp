#include "BackendBinding.hpp"

#include <SDL_syswm.h>
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
#include <SDL_video.h>
#include <dawn/native/OpenGLBackend.h>
#endif
#if defined(DAWN_ENABLE_BACKEND_NULL)
#include <dawn/native/NullBackend.h>
#endif

namespace aurora::webgpu::utils {

#if defined(DAWN_ENABLE_BACKEND_OPENGL)
struct GLUserData {
  SDL_Window* window;
  SDL_GLContext context;
};
void GLMakeCurrent(void* userData) {
  auto* data = static_cast<GLUserData*>(userData);
  SDL_GL_MakeCurrent(data->window, data->context);
}
void GLDestroy(void* userData) {
  auto* data = static_cast<GLUserData*>(userData);
  SDL_GL_DeleteContext(data->context);
  delete data;
}
#endif

bool DiscoverAdapter(dawn::native::Instance* instance, [[maybe_unused]] SDL_Window* window, wgpu::BackendType type) {
  switch (type) {
#if defined(DAWN_ENABLE_BACKEND_D3D11)
  case wgpu::BackendType::D3D11: {
    dawn::native::d3d11::PhysicalDeviceDiscoveryOptions options;
    return instance->DiscoverPhysicalDevices(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_D3D12)
  case wgpu::BackendType::D3D12: {
    dawn::native::d3d12::PhysicalDeviceDiscoveryOptions options;
    return instance->DiscoverPhysicalDevices(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_METAL)
  case wgpu::BackendType::Metal: {
    dawn::native::metal::PhysicalDeviceDiscoveryOptions options;
    return instance->DiscoverPhysicalDevices(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_VULKAN)
  case wgpu::BackendType::Vulkan: {
    dawn::native::vulkan::PhysicalDeviceDiscoveryOptions options;
    return instance->DiscoverPhysicalDevices(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_DESKTOP_GL)
  case wgpu::BackendType::OpenGL: {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    dawn::native::opengl::PhysicalDeviceDiscoveryOptions options{WGPUBackendType_OpenGL};
    options.getProc = SDL_GL_GetProcAddress;
    options.makeCurrent = GLMakeCurrent;
    options.destroy = GLDestroy;
    options.userData = new GLUserData{
        .window = window,
        .context = context,
    };
    return instance->DiscoverPhysicalDevices(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_OPENGLES)
  case wgpu::BackendType::OpenGLES: {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    dawn::native::opengl::PhysicalDeviceDiscoveryOptions options{WGPUBackendType_OpenGLES};
    options.getProc = SDL_GL_GetProcAddress;
    options.makeCurrent = GLMakeCurrent;
    options.destroy = GLDestroy;
    options.userData = new GLUserData{
        .window = window,
        .context = context,
    };
    return instance->DiscoverPhysicalDevices(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_NULL)
  case wgpu::BackendType::Null:
    instance->DiscoverDefaultPhysicalDevices();
    return true;
#endif
  default:
    return false;
  }
}

std::unique_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptorCocoa(SDL_Window* window);

std::unique_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window* window) {
#if defined(SDL_VIDEO_DRIVER_COCOA)
  return SetupWindowAndGetSurfaceDescriptorCocoa(window);
#else
  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version);
  if (SDL_GetWindowWMInfo(window, &wmInfo) == SDL_FALSE) {
    return nullptr;
  }
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
  std::unique_ptr<wgpu::SurfaceDescriptorFromWindowsHWND> desc =
      std::make_unique<wgpu::SurfaceDescriptorFromWindowsHWND>();
  desc->hwnd = wmInfo.info.win.window;
  desc->hinstance = wmInfo.info.win.hinstance;
  return std::move(desc);
#elif defined(SDL_VIDEO_DRIVER_WAYLAND) || defined(SDL_VIDEO_DRIVER_X11)
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
  if (wmInfo.subsystem == SDL_SYSWM_WAYLAND) {
    std::unique_ptr<wgpu::SurfaceDescriptorFromWaylandSurface> desc =
        std::make_unique<wgpu::SurfaceDescriptorFromWaylandSurface>();
    desc->display = wmInfo.info.wl.display;
    desc->surface = wmInfo.info.wl.surface;
    return std::move(desc);
  }
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
if (wmInfo.subsystem == SDL_SYSWM_X11) {
    std::unique_ptr<wgpu::SurfaceDescriptorFromXlibWindow> desc =
        std::make_unique<wgpu::SurfaceDescriptorFromXlibWindow>();
    desc->display = wmInfo.info.x11.display;
    desc->window = wmInfo.info.x11.window;
    return std::move(desc);
  }
#endif
  return nullptr;
#else
  return nullptr;
#endif
#endif
}

} // namespace aurora::webgpu::utils

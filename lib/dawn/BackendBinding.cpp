#include "BackendBinding.hpp"

#include <SDL_syswm.h>
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
    dawn::native::opengl::AdapterDiscoveryOptions adapterOptions{WGPUBackendType_OpenGL};
    adapterOptions.getProc = SDL_GL_GetProcAddress;
    adapterOptions.makeCurrent = GLMakeCurrent;
    adapterOptions.destroy = GLDestroy;
    adapterOptions.userData = new GLUserData{
        .window = window,
        .context = context,
    };
    return instance->DiscoverAdapters(&adapterOptions);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_OPENGLES)
  case wgpu::BackendType::OpenGLES: {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    dawn::native::opengl::AdapterDiscoveryOptions adapterOptions{WGPUBackendType_OpenGLES};
    adapterOptions.getProc = SDL_GL_GetProcAddress;
    adapterOptions.makeCurrent = GLMakeCurrent;
    adapterOptions.destroy = GLDestroy;
    adapterOptions.userData = new GLUserData{
        .window = window,
        .context = context,
    };
    return instance->DiscoverAdapters(&adapterOptions);
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
#if _WIN32
  std::unique_ptr<wgpu::SurfaceDescriptorFromWindowsHWND> desc =
      std::make_unique<wgpu::SurfaceDescriptorFromWindowsHWND>();
  desc->hwnd = glfwGetWin32Window(window);
  desc->hinstance = GetModuleHandle(nullptr);
  return std::move(desc);
#elif defined(DAWN_ENABLE_BACKEND_METAL)
  return SetupWindowAndGetSurfaceDescriptorCocoa(window);
#elif defined(DAWN_USE_WAYLAND) || defined(DAWN_USE_X11)
#if defined(GLFW_PLATFORM_WAYLAND) && defined(DAWN_USE_WAYLAND)
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    std::unique_ptr<wgpu::SurfaceDescriptorFromWaylandSurface> desc =
        std::make_unique<wgpu::SurfaceDescriptorFromWaylandSurface>();
    desc->display = glfwGetWaylandDisplay();
    desc->surface = glfwGetWaylandWindow(window);
    return std::move(desc);
  } else  // NOLINT(readability/braces)
#endif
#if defined(DAWN_USE_X11)
  {
    std::unique_ptr<wgpu::SurfaceDescriptorFromXlibWindow> desc =
        std::make_unique<wgpu::SurfaceDescriptorFromXlibWindow>();
    desc->display = glfwGetX11Display();
    desc->window = glfwGetX11Window(window);
    return std::move(desc);
  }
#else
  {
    return nullptr;
  }
#endif
#else
  return nullptr;
#endif
}

} // namespace aurora::webgpu::utils

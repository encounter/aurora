#include "BackendBinding.hpp"

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

#if defined(DAWN_ENABLE_BACKEND_D3D12)
BackendBinding* CreateD3D12Binding(SDL_Window* window, WGPUDevice device);
#endif
#if defined(DAWN_ENABLE_BACKEND_METAL)
BackendBinding* CreateMetalBinding(SDL_Window* window, WGPUDevice device);
#endif
#if defined(DAWN_ENABLE_BACKEND_NULL)
BackendBinding* CreateNullBinding(SDL_Window* window, WGPUDevice device);
#endif
#if defined(DAWN_ENABLE_BACKEND_OPENGL)
BackendBinding* CreateOpenGLBinding(SDL_Window* window, WGPUDevice device);
#endif
#if defined(DAWN_ENABLE_BACKEND_VULKAN)
BackendBinding* CreateVulkanBinding(SDL_Window* window, WGPUDevice device);
#endif

BackendBinding::BackendBinding(SDL_Window* window, WGPUDevice device) : m_window(window), m_device(device) {}

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

bool DiscoverAdapter(dawn::native::Instance* instance, SDL_Window* window, wgpu::BackendType type) {
  switch (type) {
#if defined(DAWN_ENABLE_BACKEND_D3D12)
  case wgpu::BackendType::D3D12: {
    dawn::native::d3d12::AdapterDiscoveryOptions options;
    return instance->DiscoverAdapters(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_METAL)
  case wgpu::BackendType::Metal: {
    dawn::native::metal::AdapterDiscoveryOptions options;
    return instance->DiscoverAdapters(&options);
  }
#endif
#if defined(DAWN_ENABLE_BACKEND_VULKAN)
  case wgpu::BackendType::Vulkan: {
    dawn::native::vulkan::AdapterDiscoveryOptions options;
    return instance->DiscoverAdapters(&options);
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
    instance->DiscoverDefaultAdapters();
    return true;
#endif
  default:
    return false;
  }
}

BackendBinding* CreateBinding(wgpu::BackendType type, SDL_Window* window, WGPUDevice device) {
  switch (type) {
#if defined(DAWN_ENABLE_BACKEND_D3D12)
  case wgpu::BackendType::D3D12:
    return CreateD3D12Binding(window, device);
#endif
#if defined(DAWN_ENABLE_BACKEND_METAL)
  case wgpu::BackendType::Metal:
    return CreateMetalBinding(window, device);
#endif
#if defined(DAWN_ENABLE_BACKEND_NULL)
  case wgpu::BackendType::Null:
    return CreateNullBinding(window, device);
#endif
#if defined(DAWN_ENABLE_BACKEND_DESKTOP_GL)
  case wgpu::BackendType::OpenGL:
    return CreateOpenGLBinding(window, device);
#endif
#if defined(DAWN_ENABLE_BACKEND_OPENGLES)
  case wgpu::BackendType::OpenGLES:
    return CreateOpenGLBinding(window, device);
#endif
#if defined(DAWN_ENABLE_BACKEND_VULKAN)
  case wgpu::BackendType::Vulkan:
    return CreateVulkanBinding(window, device);
#endif
  default:
    return nullptr;
  }
}

} // namespace aurora::webgpu::utils

#pragma once

#include <dawn/native/DawnNative.h>
#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

namespace aurora::webgpu::utils {

bool DiscoverAdapter(dawn::native::Instance* instance, SDL_Window* window, wgpu::BackendType type);
std::unique_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window* window);

} // namespace aurora::webgpu::utils

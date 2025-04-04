#pragma once

#include <memory>
#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

namespace aurora::webgpu::utils {

std::unique_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window* window);

} // namespace aurora::webgpu::utils

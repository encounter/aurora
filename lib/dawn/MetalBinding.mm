#include "BackendBinding.hpp"

#include <SDL3/SDL_metal.h>

namespace aurora::webgpu::utils {
std::unique_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptorCocoa(SDL_Window* window) {
  SDL_MetalView view = SDL_Metal_CreateView(window);
  std::unique_ptr<wgpu::SurfaceSourceMetalLayer> desc = std::make_unique<wgpu::SurfaceSourceMetalLayer>();
  desc->layer = SDL_Metal_GetLayer(view);
  return std::move(desc);
}
} // namespace aurora::webgpu::utils

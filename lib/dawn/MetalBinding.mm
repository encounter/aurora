#include "BackendBinding.hpp"

#import <Foundation/Foundation.h>
#include <SDL3/SDL_metal.h>

namespace aurora::webgpu::utils {
std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptorCocoa(SDL_Window* window) {
  SDL_MetalView view = SDL_Metal_CreateView(window);
  std::shared_ptr<wgpu::SurfaceSourceMetalLayer> desc = std::make_shared<wgpu::SurfaceSourceMetalLayer>();
  desc->layer = SDL_Metal_GetLayer(view);
  return std::move(desc);
}
} // namespace aurora::webgpu::utils

#include "BackendBinding.hpp"

#import <Foundation/Foundation.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_properties.h>

namespace aurora::webgpu::utils {
namespace {
constexpr const char* MetalViewProperty = "aurora.metal_view";

void SDLCALL destroy_metal_view(void*, void* value) {
  if (value != nullptr) {
    SDL_Metal_DestroyView(static_cast<SDL_MetalView>(value));
  }
}
} // namespace

std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptorCocoa(SDL_Window* window) {
  const auto properties = SDL_GetWindowProperties(window);
  auto view = static_cast<SDL_MetalView>(SDL_GetPointerProperty(properties, MetalViewProperty, nullptr));
  if (view == nullptr) {
    view = SDL_Metal_CreateView(window);
    if (view == nullptr ||
        !SDL_SetPointerPropertyWithCleanup(properties, MetalViewProperty, view, destroy_metal_view, nullptr)) {
      if (view != nullptr) {
        SDL_Metal_DestroyView(view);
      }
      return nullptr;
    }
  }
  std::shared_ptr<wgpu::SurfaceSourceMetalLayer> desc = std::make_shared<wgpu::SurfaceSourceMetalLayer>();
  desc->layer = SDL_Metal_GetLayer(view);
  if (desc->layer == nullptr) {
    return nullptr;
  }
  return std::move(desc);
}
} // namespace aurora::webgpu::utils

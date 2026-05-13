#include "BackendBinding.hpp"

#import <Foundation/Foundation.h>

// Shim for macOS 15+ Metal classes to allow running on macOS 14
#ifndef __MAC_15_0
@interface MTLLogStateDescriptor : NSObject
@end
@interface MTLLogState : NSObject
@end
#endif

@implementation MTLLogStateDescriptor
@end

@implementation MTLLogState
@end

#include <SDL3/SDL_metal.h>

namespace aurora::webgpu::utils {
std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptorCocoa(SDL_Window* window) {
  SDL_MetalView view = SDL_Metal_CreateView(window);
  std::shared_ptr<wgpu::SurfaceSourceMetalLayer> desc = std::make_shared<wgpu::SurfaceSourceMetalLayer>();
  desc->layer = SDL_Metal_GetLayer(view);
  return std::move(desc);
}
} // namespace aurora::webgpu::utils

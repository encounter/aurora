#include <webgpu/webgpu_cpp.h>
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

static inline bool operator==(const wgpu::Extent3D& lhs, const wgpu::Extent3D& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && lhs.depthOrArrayLayers == rhs.depthOrArrayLayers;
}
static inline bool operator!=(const wgpu::Extent3D& lhs, const wgpu::Extent3D& rhs) { return !(lhs == rhs); }

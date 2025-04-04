#include <webgpu/webgpu_cpp.h>
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include <string_view>

static inline bool operator==(const wgpu::Extent3D& lhs, const wgpu::Extent3D& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && lhs.depthOrArrayLayers == rhs.depthOrArrayLayers;
}
static inline bool operator!=(const wgpu::Extent3D& lhs, const wgpu::Extent3D& rhs) { return !(lhs == rhs); }

namespace wgpu {
inline std::string_view format_as(wgpu::StringView str) { return str; }
} // namespace wgpu

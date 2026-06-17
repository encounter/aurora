#include "TracyPlatform.hpp"

#if defined(TRACY_ENABLE) && __has_include(<dawn/platform/DawnPlatform.h>)

#include <chrono>
#include <cstring>
#include <vector>

#include <dawn/platform/DawnPlatform.h>
#include <tracy/TracyC.h>

namespace aurora::webgpu {
namespace {
class TracyDawnPlatform final : public dawn::platform::Platform {
public:
  const unsigned char* GetTraceCategoryEnabledFlag(dawn::platform::TraceCategory) override {
    static const unsigned char enabled = 1;
    return &enabled;
  }
  double MonotonicallyIncreasingTime() override {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  uint64_t AddTraceEvent(char phase, const unsigned char*, const char* name, uint64_t, double, int, const char**,
                         const unsigned char*, const uint64_t*, unsigned char) override {
    thread_local std::vector<TracyCZoneCtx> zoneStack;
    if (phase == 'B') {
      const uint64_t srcloc = ___tracy_alloc_srcloc_name(0, "dawn", 4, "", 0, name, std::strlen(name), 0);
      zoneStack.push_back(___tracy_emit_zone_begin_alloc(srcloc, 1));
    } else if (phase == 'E') {
      if (!zoneStack.empty()) {
        ___tracy_emit_zone_end(zoneStack.back());
        zoneStack.pop_back();
      }
    }
    return 0;
  }
};
TracyDawnPlatform g_tracyDawnPlatform;
} // namespace

dawn::platform::Platform* tracy_dawn_platform() { return &g_tracyDawnPlatform; }
} // namespace aurora::webgpu

#else

namespace aurora::webgpu {
dawn::platform::Platform* tracy_dawn_platform() { return nullptr; }
} // namespace aurora::webgpu

#endif

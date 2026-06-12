#pragma once

namespace dawn::platform {
class Platform;
} // namespace dawn::platform

namespace aurora::webgpu {

// A Dawn platform that forwards Dawn's trace events to Tracy zones, so Dawn
// internals (queue submits, command recording, pipeline creation, ...) show
// up in captures without patching Dawn. Returns nullptr when Tracy is
// disabled or the Dawn platform headers are unavailable (prebuilt packages
// don't ship them).
dawn::platform::Platform* tracy_dawn_platform();

} // namespace aurora::webgpu

#pragma once

namespace dawn::platform {
class Platform;
} // namespace dawn::platform

namespace aurora::webgpu {
dawn::platform::Platform* tracy_dawn_platform();
} // namespace aurora::webgpu

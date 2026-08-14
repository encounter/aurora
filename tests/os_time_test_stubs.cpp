#include <aurora/aurora.h>
#include <dolphin/os.h>

#include <cstdint>

uintptr_t OSBaseAddress = 0;

namespace aurora {

AuroraConfig g_config{};

void log_internal(AuroraLogLevel, const char*, const char*, unsigned int) noexcept {}

} // namespace aurora

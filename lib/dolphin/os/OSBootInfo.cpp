#include "internal.hpp"
#include <dolphin/os.h>

void AuroraFillBootInfo() {
  if (OSBaseAddress == 0) {
    return;
  }

  const auto info = static_cast<OSBootInfo*>(OSPhysicalToCached(0));

  info->memorySize = aurora::g_config.mem1Size;
}

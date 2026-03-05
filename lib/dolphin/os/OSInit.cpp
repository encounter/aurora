#include "internal.hpp"

#include <dolphin/os.h>

static bool AlreadyInitialized;

void OSInit() {
  if (AlreadyInitialized) {
    return;
  }

  AlreadyInitialized = true;

  AuroraOSInitMemory();
  AuroraFillBootInfo();
  AuroraInitClock();
  AuroraInitArena();
}

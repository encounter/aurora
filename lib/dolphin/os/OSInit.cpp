#include "internal.hpp"

#include <dolphin/os.h>

void OSInit() {
  AuroraOSInitMemory();
  AuroraFillBootInfo();
  AuroraInitClock();
  AuroraInitArena();
}

#include "internal.hpp"
#include "../../internal.hpp"
#include "dolphin/types.h"

#include <cassert>

#if NDEBUG
#undef OSPhysicalToCached
#undef OSPhysicalToUncached
#undef OSCachedToPhysical
#undef OSUncachedToPhysical
#undef OSCachedToUncached
#undef OSUncachedToCached
#endif

void* OSPhysicalToCached(u32 paddr) {
  assert (paddr <= aurora::g_config.mem1Size);
  return POINTER_ADD(MEM1Start, paddr);
}

void* OSPhysicalToUncached(u32 paddr) {
  return OSPhysicalToCached(paddr);
}

u32 OSCachedToPhysical(void* caddr) {
  assert (caddr >= MEM1Start && caddr <= MEM1End);

  return (reinterpret_cast<uintptr_t>(caddr) - reinterpret_cast<uintptr_t>(MEM1Start));
}

u32 OSUncachedToPhysical(void* ucaddr) {
  return OSCachedToPhysical(ucaddr);
}

void* OSCachedToUncached(void* caddr) {
  return caddr;
}

void* OSUncachedToCached(void* ucaddr) {
  return ucaddr;
}


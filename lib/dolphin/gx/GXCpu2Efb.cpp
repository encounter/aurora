#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/depth_peek.hpp"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXCpu2Efb.h>

void GXPeekZ(u16 x, u16 y, u32* z) {
  if (z != nullptr) {
    u32 value = 0;
    if (aurora::gfx::depth_peek::read_latest(x, y, value)) {
      *z = value;
    } else {
      *z = 0;
    }
  }

  GX_WRITE_AURORA(GX_AURORA_REQUEST_DEPTH_SNAPSHOT);
}

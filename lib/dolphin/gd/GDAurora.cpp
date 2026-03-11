#include <limits>

#include "gd.hpp"
#include "dolphin/gd/GDAurora.h"
#include "dolphin/gd/GDBase.h"
#include "dolphin/gx/GXAurora.h"

static void GDWriteString(const char* label) {
  auto length = strlen(label);

  if (length > std::numeric_limits<u16>::max()) {
    Log.warn("Debug marker size over u16 max, truncating");
    length = std::numeric_limits<u16>::max();
  }

  GDWrite_u16(length);
  GDWrite_data(label, length);
}

void GDPushDebugGroup(const char* label) {
  GDWriteAuroraCmd(GX_LOAD_AURORA_DEBUG_GROUP_PUSH);
  GDWriteString(label);
}

void GDPopDebugGroup() {
  GDWriteAuroraCmd(GX_LOAD_AURORA_DEBUG_GROUP_POP);
}

void GDInsertDebugMarker(const char* label) {
  GDWriteAuroraCmd(GX_LOAD_AURORA_DEBUG_MARKER_INSERT);
  GDWriteString(label);
}

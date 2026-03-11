#include "dolphin/gx/GXAurora.h"

#include <limits>

#include "__gx.h"
#include "gx.hpp"

static void GXWriteString(const char* label) {
  auto length = strlen(label);

  if (length > std::numeric_limits<u16>::max()) {
    Log.warn("Debug marker size over u16 max, truncating");
    length = std::numeric_limits<u16>::max();
  }

  GX_WRITE_U16(length);
  GX_WRITE_DATA(label, length);
}

void GXPushDebugGroup(const char* label) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_DEBUG_GROUP_PUSH);
  GXWriteString(label);
}

void GXPopDebugGroup() {
  GX_WRITE_AURORA(GX_LOAD_AURORA_DEBUG_GROUP_POP);
}

void GXInsertDebugMarker(const char* label) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_DEBUG_MARKER_INSERT);
  GXWriteString(label);
}

#include <limits>

#include "gd.hpp"
#include "../../gfx/texture.hpp"
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
  GDWriteAuroraCmd(GX_AURORA_DEBUG_GROUP_PUSH);
  GDWriteString(label);
}

void GDPopDebugGroup() {
  GDWriteAuroraCmd(GX_AURORA_DEBUG_GROUP_POP);
}

void GDInsertDebugMarker(const char* label) {
  GDWriteAuroraCmd(GX_AURORA_DEBUG_MARKER_INSERT);
  GDWriteString(label);
}

void GDLoadTexObj(const GXTexObj* obj_, GXTexMapID id) {
  const auto* obj = reinterpret_cast<const GXTexObj_*>(obj_);
  GDWriteAuroraCmd(GX_AURORA_LOAD_TEXOBJ);
  GDWrite_u8(static_cast<u8>(id));
  GDWrite_u64(reinterpret_cast<u64>(obj->data));
  GDWrite_u32(obj->width());
  GDWrite_u32(obj->height());
  GDWrite_u32(obj->format());
  GDWrite_u32(static_cast<u32>(obj->tlut));
  GDWrite_u8(static_cast<u8>(obj->has_mips()));
  GDWrite_u32(obj->texObjId);
  GDWrite_u32(obj->texDataVersion);
}

void GDLoadTlut(const GXTlutObj* obj_, u32 idx) {
  const auto* obj = reinterpret_cast<const GXTlutObj_*>(obj_);
  GDWriteAuroraCmd(GX_AURORA_LOAD_TLUT);
  GDWrite_u8(static_cast<u8>(idx));
  GDWrite_u64(reinterpret_cast<u64>(obj->data));
  GDWrite_u32(static_cast<u32>(obj->format));
  GDWrite_u16(obj->numEntries);
  GDWrite_u32(obj->tlutObjId);
  GDWrite_u32(obj->tlutDataVersion);
}

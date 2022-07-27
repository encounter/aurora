#include "gx.hpp"

#include "../gfx/model/shader.hpp"

void GXBeginDisplayList(void* list, u32 size) {
  // TODO
}

u32 GXEndDisplayList() {
  // TODO
  return 0;
}

void GXCallDisplayList(const void* data, u32 nbytes) {
  // TODO CElementGen needs fixing
  for (const auto& type : aurora::gfx::gx::g_gxState.vtxDesc) {
    if (type == GX_DIRECT) {
      Log.report(LOG_WARNING, FMT_STRING("Direct attributes in surface config!"));
      return;
    }
  }
  aurora::gfx::model::queue_surface(static_cast<const u8*>(data), nbytes);
}

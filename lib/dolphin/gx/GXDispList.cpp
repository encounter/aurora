#include "gx.hpp"

#include "../../gfx/model/shader.hpp"

extern "C" {
void GXBeginDisplayList(void* list, u32 size) {
  // TODO
}

u32 GXEndDisplayList() {
  // TODO
  return 0;
}

void GXCallDisplayList(const void* data, u32 nbytes) {
  aurora::gfx::model::queue_surface(static_cast<const u8*>(data), nbytes);
}
}
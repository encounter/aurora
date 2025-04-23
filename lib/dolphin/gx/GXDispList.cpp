#include "gx.hpp"

#include "../../gfx/model/shader.hpp"

#define ROUNDUP32(x) (((x) + 31) & ~31)

extern "C" {
void GXBeginDisplayList(void* list, u32 size) {
  CHECK(!g_gxState.dynamicDlBuf, "Display list began twice!");
  g_gxState.dynamicDlBuf.emplace(static_cast<u8*>(list), size);
}

u32 GXEndDisplayList() {
  auto &dlBuf = g_gxState.dynamicDlBuf;
  size_t size = dlBuf->size();
  size_t paddedSize = ROUNDUP32(size);
  dlBuf->append_zeroes(paddedSize - size);
  dlBuf.reset();
  return paddedSize;
}

void GXCallDisplayListLE(const void* data, u32 nbytes) {
  aurora::gfx::model::queue_surface(static_cast<const u8*>(data), nbytes, false);
}

void GXCallDisplayList(const void* data, u32 nbytes) {
  aurora::gfx::model::queue_surface(static_cast<const u8*>(data), nbytes, true);
}
}

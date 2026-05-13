#include "dolphin/gx/GXAurora.h"

#include <limits>

#include "__gx.h"
#include "gx.hpp"
#include "../../window.hpp"

#include "../../gfx/common.hpp"
#include "../../gx/fifo.hpp"

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

void GXPopDebugGroup() { GX_WRITE_AURORA(GX_LOAD_AURORA_DEBUG_GROUP_POP); }

void GXInsertDebugMarker(const char* label) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_DEBUG_MARKER_INSERT);
  GXWriteString(label);
}

void AuroraSetViewportPolicy(AuroraViewportPolicy policy) {
  g_gxState.viewportPolicy = policy;
  aurora::window::set_frame_buffer_aspect_fit(policy == AURORA_VIEWPORT_FIT);
}

void AuroraGetRenderSize(u32* width, u32* height) {
  const auto windowSize = aurora::window::get_window_size();
  if (width != nullptr) {
    *width = windowSize.fb_width;
  }
  if (height != nullptr) {
    *height = windowSize.fb_height;
  }
}

void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_VIEWPORT_RENDER);
  GX_WRITE_F32(left);
  GX_WRITE_F32(top);
  GX_WRITE_F32(wd);
  GX_WRITE_F32(ht);
  GX_WRITE_F32(nearz);
  GX_WRITE_F32(farz);
}

void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_SCISSOR_RENDER);
  GX_WRITE_U32(left);
  GX_WRITE_U32(top);
  GX_WRITE_U32(wd);
  GX_WRITE_U32(ht);
}

void GXCreateFrameBuffer(u32 width, u32 height) {
  aurora::gx::fifo::drain();
  aurora::gfx::begin_offscreen(width, height);
}

void GXRestoreFrameBuffer() {
  aurora::gx::fifo::drain();
  aurora::gfx::end_offscreen();
}

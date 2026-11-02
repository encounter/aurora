#include "dolphin/gx/GXAurora.h"

#include <limits>

#include "__gx.h"
#include "gx.hpp"
#include "../../window.hpp"

#include "../../gfx/readback/readback.hpp"
#include "../../gx/fifo.hpp"
#include "../../gx/readback.hpp"

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
  GX_WRITE_AURORA(GX_AURORA_DEBUG_GROUP_PUSH);
  GXWriteString(label);
}

void GXPopDebugGroup() { GX_WRITE_AURORA(GX_AURORA_DEBUG_GROUP_POP); }

void GXInsertDebugMarker(const char* label) {
  GX_WRITE_AURORA(GX_AURORA_DEBUG_MARKER_INSERT);
  GXWriteString(label);
}

void AuroraSetViewportPolicy(AuroraViewportPolicy policy) { aurora::gx::set_viewport_policy(policy); }

void AuroraGetRenderSize(u32* width, u32* height) {
  const auto windowSize = aurora::window::get_window_size();
  if (width != nullptr) {
    *width = windowSize.fb_width;
  }
  if (height != nullptr) {
    *height = windowSize.fb_height;
  }
}

void AuroraGXSync() {
  GXFlush();
  aurora::gx::fifo::drain();
}

void AuroraSetDrawsBlocking(bool blocking) {
  GX_WRITE_AURORA(GX_AURORA_SET_DRAWS_BLOCKING);
  GX_WRITE_U8(blocking);
}

void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
  GX_WRITE_AURORA(GX_AURORA_LOAD_VIEWPORT_RENDER);
  GX_WRITE_F32(left);
  GX_WRITE_F32(top);
  GX_WRITE_F32(wd);
  GX_WRITE_F32(ht);
  GX_WRITE_F32(nearz);
  GX_WRITE_F32(farz);
}

void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht) {
  GX_WRITE_AURORA(GX_AURORA_LOAD_SCISSOR_RENDER);
  GX_WRITE_U32(left);
  GX_WRITE_U32(top);
  GX_WRITE_U32(wd);
  GX_WRITE_U32(ht);
}

void GXSetProjectionFull(const void* mtx) {
  const f32* values = reinterpret_cast<const f32*>(mtx);
  GX_WRITE_AURORA(GX_AURORA_LOAD_PROJECTION_FULL);
  for (int i = 0; i < 16; ++i) {
    GX_WRITE_F32(values[i]);
  }
}

void GX2SetPolygonOffset(f32 mFrontOffset, f32 mFrontScale, f32 mBackOffset, f32 mBackScale, f32 mClamp) {
  GX_WRITE_AURORA(GX2_SET_POLYGON_OFFSET);
  GX_WRITE_F32(mFrontOffset);
  GX_WRITE_F32(mFrontScale);
  GX_WRITE_F32(mBackOffset);
  GX_WRITE_F32(mBackScale);
  GX_WRITE_F32(mClamp);
}

void GXCreateFrameBuffer(u32 width, u32 height) {
  GX_WRITE_AURORA(GX_AURORA_BEGIN_OFFSCREEN);
  GX_WRITE_U32(width);
  GX_WRITE_U32(height);
  aurora::gx::fifo::publish();
}

void GXRestoreFrameBuffer() {
  GX_WRITE_AURORA(GX_AURORA_END_OFFSCREEN);
  aurora::gx::fifo::publish();
}

static void GXWriteClear(bool wholeTarget, GXBool color, GXBool alpha, GXBool depth, GXColor clearColor, u32 clearZ,
                         u32 left, u32 top, u32 width, u32 height) {
  GX_WRITE_AURORA(GX_AURORA_CLEAR);
  GX_WRITE_U8((color ? 0x01 : 0) | (alpha ? 0x02 : 0) | (depth ? 0x04 : 0) | (wholeTarget ? 0x08 : 0));
  GX_WRITE_U32(left);
  GX_WRITE_U32(top);
  GX_WRITE_U32(width);
  GX_WRITE_U32(height);
  GX_WRITE_U8(clearColor.r);
  GX_WRITE_U8(clearColor.g);
  GX_WRITE_U8(clearColor.b);
  GX_WRITE_U8(clearColor.a);
  GX_WRITE_U32(clearZ);
}

void GXClear(GXBool color, GXBool alpha, GXBool depth, GXColor clearColor, u32 clearZ) {
  GXWriteClear(true, color, alpha, depth, clearColor, clearZ, 0, 0, 0, 0);
}

void GXClearSubrect(GXBool color, GXBool alpha, GXBool depth, GXColor clearColor, u32 clearZ, u32 left, u32 top,
                    u32 width, u32 height) {
  GXWriteClear(false, color, alpha, depth, clearColor, clearZ, left, top, width, height);
}

AuroraReadbackStream AuroraReadbackCreateStream() { return aurora::gfx::readback::create_stream(); }

void AuroraReadbackDestroyStream(AuroraReadbackStream stream) { aurora::gfx::readback::destroy_stream(stream); }

GXBool AuroraReadbackCapturePeeks(AuroraReadbackStream stream, const AuroraReadbackPeekRequest* requests, u32 count) {
  return aurora::gx::readback::capture_peeks(stream, requests, count, __gx->alphaRead);
}

GXBool AuroraReadbackCaptureCopies(AuroraReadbackStream stream, const AuroraReadbackCopySrc* srcs, u32 count) {
  return aurora::gx::readback::capture_copies(stream, srcs, count);
}

GXBool AuroraReadbackCaptureColor(AuroraReadbackStream stream, u64 key, u32* width, u32* height) {
  return aurora::gfx::readback::capture_color(stream, key, width, height) ? GX_TRUE : GX_FALSE;
}

GXBool AuroraReadbackGet(AuroraReadbackStream stream, AuroraReadbackDest* dests, u32 count) {
  return aurora::gx::readback::get(stream, dests, count);
}

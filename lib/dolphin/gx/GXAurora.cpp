#include "dolphin/gx/GXAurora.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "__gx.h"
#include "gx.hpp"
#include "../../window.hpp"

#include "../../gfx/common.hpp"
#include "../../gx/fifo.hpp"
#include "../vi/vi_internal.hpp"

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

void AuroraSetViewportPolicy(AuroraViewportPolicy policy) { g_gxState.viewportPolicy = policy; }

void AuroraGetRenderSize(u32* width, u32* height) {
  const auto windowSize = aurora::window::get_window_size();
  u32 renderWidth = windowSize.fb_width;
  u32 renderHeight = windowSize.fb_height;

  if (g_gxState.viewportPolicy == AURORA_VIEWPORT_FIT) {
    const auto efbSize = aurora::vi::configured_fb_size();
    if (efbSize.x != 0 && efbSize.y != 0 && renderWidth != 0 && renderHeight != 0) {
      const double targetAspect = static_cast<double>(renderWidth) / static_cast<double>(renderHeight);
      const double contentAspect = static_cast<double>(efbSize.x) / static_cast<double>(efbSize.y);
      if (targetAspect > contentAspect) {
        renderWidth =
            std::max<u32>(1u, static_cast<u32>(std::lround(static_cast<double>(renderHeight) * contentAspect)));
      } else {
        renderHeight =
            std::max<u32>(1u, static_cast<u32>(std::lround(static_cast<double>(renderWidth) / contentAspect)));
      }
    }
  }

  if (width != nullptr) {
    *width = renderWidth;
  }
  if (height != nullptr) {
    *height = renderHeight;
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

void GXSetPixelLighting(bool enabled) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_PIXEL_LIGHTING);
  GX_WRITE_U8(enabled ? 1 : 0);
}

void GXCreateFrameBuffer(u32 width, u32 height) {
  aurora::gx::fifo::drain();
  aurora::gfx::begin_offscreen(width, height);
}

void GXRestoreFrameBuffer() {
  aurora::gx::fifo::drain();
  aurora::gfx::end_offscreen();
}

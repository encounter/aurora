#include "gx.hpp"

#include "../window.hpp"
#include "../webgpu/wgpu.hpp"

extern "C" {
GXRenderModeObj GXNtsc480IntDf = {
    VI_TVMODE_NTSC_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0,
};
GXRenderModeObj GXPal528IntDf = {
    VI_TVMODE_PAL_INT, 704, 528, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0,
};
GXRenderModeObj GXMpal480IntDf = {
    VI_TVMODE_PAL_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0,
};
}

void GXAdjustForOverscan(GXRenderModeObj* rmin, GXRenderModeObj* rmout, u16 hor, u16 ver) {
  *rmout = *rmin;
  const auto size = aurora::window::get_window_size();
  rmout->fbWidth = size.fb_width;
  rmout->efbHeight = size.fb_height;
  rmout->xfbHeight = size.fb_height;
}

void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) {}

void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) { g_gxState.texCopySrc = {left, top, wd, ht}; }

void GXSetDispCopyDst(u16 wd, u16 ht) {}

void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap) {
  CHECK(wd == g_gxState.texCopySrc.width && ht == g_gxState.texCopySrc.height, "Texture copy scaling unimplemented");
  g_gxState.texCopyFmt = fmt;
}

// TODO GXSetDispCopyFrame2Field
// TODO GXSetCopyClamp

u32 GXSetDispCopyYScale(f32 vscale) { return 0; }

void GXSetCopyClear(GXColor color, u32 depth) { update_gx_state(g_gxState.clearColor, from_gx_color(color)); }

void GXSetCopyFilter(GXBool aa, u8 sample_pattern[12][2], GXBool vf, u8 vfilter[7]) {}

void GXSetDispCopyGamma(GXGamma gamma) {}

void GXCopyDisp(void* dest, GXBool clear) {}

void GXCopyTex(void* dest, GXBool clear) {
  const auto& rect = g_gxState.texCopySrc;
  const wgpu::Extent3D size{
      .width = static_cast<uint32_t>(rect.width),
      .height = static_cast<uint32_t>(rect.height),
      .depthOrArrayLayers = 1,
  };
  aurora::gfx::TextureHandle handle;
  const auto it = g_gxState.copyTextures.find(dest);
  if (it == g_gxState.copyTextures.end() || it->second->size != size) {
    handle = aurora::gfx::new_render_texture(rect.width, rect.height, g_gxState.texCopyFmt, "Resolved Texture");
    g_gxState.copyTextures[dest] = handle;
  } else {
    handle = it->second;
  }
  aurora::gfx::resolve_pass(handle, rect, clear, g_gxState.clearColor);
}

// TODO GXGetYScaleFactor
// TODO GXGetNumXfbLines
// TODO GXClearBoundingBox
// TODO GXReadBoundingBox

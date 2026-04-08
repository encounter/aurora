#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/tex_copy_conv.hpp"
#include "../../gfx/texture.hpp"
#include "../../window.hpp"
#include "../../gfx/clear.hpp"
#include "../../webgpu/wgpu.hpp"
#include "../../webgpu/gpu.hpp"

extern "C" {
GXRenderModeObj GXNtsc480IntDf = {
    VI_TVMODE_NTSC_INT,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {8, 8, 10, 12, 10, 8, 8},
};
GXRenderModeObj GXNtsc480Int = {
    VI_TVMODE_NTSC_INT,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {0, 0, 21, 22, 21, 0, 0},
};
GXRenderModeObj GXPal528IntDf = {
    VI_TVMODE_PAL_INT,
    704,
    528,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {8, 8, 10, 12, 10, 8, 8},
};
GXRenderModeObj GXMpal480IntDf = {
    VI_TVMODE_PAL_INT,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {8, 8, 10, 12, 10, 8, 8},
};

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
  g_gxState.texCopyFmt = fmt;
  g_gxState.texCopyDstWidth = wd;
  g_gxState.texCopyDstHeight = ht;
}

// TODO GXSetDispCopyFrame2Field
// TODO GXSetCopyClamp

u32 GXSetDispCopyYScale(f32 vscale) { return 0; }

void GXSetCopyClear(GXColor color, u32 depth) {
  // BP 0x4F: clear color R + A
  u32 reg0 = 0;
  SET_REG_FIELD(0, reg0, 8, 0, color.r);
  SET_REG_FIELD(0, reg0, 8, 8, color.a);
  SET_REG_FIELD(0, reg0, 8, 24, 0x4F);
  GX_WRITE_RAS_REG(reg0);

  // BP 0x50: clear color B + G
  u32 reg1 = 0;
  SET_REG_FIELD(0, reg1, 8, 0, color.b);
  SET_REG_FIELD(0, reg1, 8, 8, color.g);
  SET_REG_FIELD(0, reg1, 8, 24, 0x50);
  GX_WRITE_RAS_REG(reg1);

  // BP 0x51: clear Z (24-bit)
  u32 reg2 = 0;
  SET_REG_FIELD(0, reg2, 24, 0, depth);
  SET_REG_FIELD(0, reg2, 8, 24, 0x51);
  GX_WRITE_RAS_REG(reg2);
  __gx->bpSent = 1;
}

void GXSetCopyFilter(GXBool aa, u8 sample_pattern[12][2], GXBool vf, u8 vfilter[7]) {}

void GXSetDispCopyGamma(GXGamma gamma) {}

void GXCopyDisp(void* dest, GXBool clear) {}

void GXCopyTex(void* dest, GXBool clear) {
  const auto& rect = g_gxState.texCopySrc;
  const u32 dstWidth = g_gxState.texCopyDstWidth;
  const u32 dstHeight = g_gxState.texCopyDstHeight;
  const auto texCopyFmt = g_gxState.texCopyFmt;

  const aurora::gx::GXState::CopyTextureKey key{
      .dest = dest,
      .width = dstWidth,
      .height = dstHeight,
      .format = texCopyFmt,
  };
  auto it = g_gxState.copyTextureCache.find(key);
  if (it == g_gxState.copyTextureCache.end()) {
    aurora::gfx::TextureHandle handle;
    if (aurora::gfx::tex_copy_conv::needs_conversion(texCopyFmt)) {
      handle = aurora::gfx::new_conv_texture(dstWidth, dstHeight, texCopyFmt, "Copy Conv Texture");
    } else {
      // Configure the texture swizzle to use alpha 1.0 if targeting RGB565 or EFB doesn't have alpha
      const auto fmt = texCopyFmt == GX_TF_RGB565 || g_gxState.pixelFmt == GX_PF_RGB8_Z24 ||
                               g_gxState.pixelFmt == GX_PF_RGB565_Z16
                           ? GX_TF_RGB565
                           : GX_TF_RGBA8;
      handle = aurora::gfx::new_render_texture(dstWidth, dstHeight, fmt, "Resolved Texture");
    }
    it = g_gxState.copyTextureCache.emplace(key, aurora::gx::GXState::CopyTextureRef{.handle = handle, .revision = 0}).first;
  }
  auto& handle = it->second;

  if (g_gxState.alphaUpdate && g_gxState.dstAlpha != UINT32_MAX) {
    if (!clear) {
      // TODO: figure out the right behavior here.
      // should the copy have a specific alpha value but the EFB remains untouched?
    }
    // Overwrite alpha before resolving
    aurora::gfx::push_draw_command(aurora::gfx::clear::DrawData{
        .pipeline = aurora::gfx::pipeline_ref(aurora::gfx::clear::PipelineConfig{
            .clearColor = false,
            .clearAlpha = true,
            .clearDepth = false,
        }),
        .color = wgpu::Color{0.f, 0.f, 0.f, g_gxState.dstAlpha / 255.f},
    });
  }
  const auto clearColor = clear && g_gxState.colorUpdate;
  const auto clearAlpha = clear && g_gxState.alphaUpdate;
  const auto clearDepth = clear && g_gxState.depthUpdate;
  aurora::gfx::resolve_pass(handle.handle, rect, clearColor, clearAlpha, clearDepth, g_gxState.clearColor,
                            aurora::gx::clear_depth_value(), texCopyFmt);
  ++handle.revision;
  g_gxState.copyTextures[dest] = handle;
}

// TODO GXGetYScaleFactor
// TODO GXGetNumXfbLines
// TODO GXClearBoundingBox
// TODO GXReadBoundingBox
}

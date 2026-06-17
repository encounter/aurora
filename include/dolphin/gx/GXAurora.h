#ifndef DOLPHIN_GXAURORA_H
#define DOLPHIN_GXAURORA_H

#include <dolphin/types.h>
#include <dolphin/gx/GXEnum.h>

#if __cplusplus
extern "C" {
#endif

//
// Subcommands for GX_AURORA.
//

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Must be followed by six f32 values: left, top, width, height, nearz, farz.
 */
#define GX_AURORA_LOAD_VIEWPORT_RENDER 0x0001

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Must be followed by four u32 values: left, top, width, height.
 */
#define GX_AURORA_LOAD_SCISSOR_RENDER 0x0002

/**
 * Aurora equivalent of CP_REG_ARRAYBASE_ID: sets the base address and size of a vertex array.
 * This command must be followed by a 64-bit memory address, 32-bit size, and 1-byte little-endian flag.
 * The index of the vertex array is given by the lowest 4 bits of the command ID,
 * e.g. writing GX_AURORA_LOAD_ARRAYBASE + 5 will set the vertex array for the sixth vertex attribute.
 * To set strides, use the normal CP_REG_ARRAYSTRIDE_ID register.
 */
#define GX_AURORA_LOAD_ARRAYBASE 0x0010

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
#define GX_AURORA_DEBUG_GROUP_PUSH 0x0020

/**
 * Pops a previously pushed debug group.
 * Followed by nothing.
 */
#define GX_AURORA_DEBUG_GROUP_POP 0x0021

/**
 * Sends a debug marker to the backend graphics API.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 */
#define GX_AURORA_DEBUG_MARKER_INSERT 0x0022

#define GX_AURORA_LOAD_TEXOBJ 0x0030

#define GX_AURORA_LOAD_TLUT 0x0031

#define GX_AURORA_DESTROY_TEXOBJ 0x0032

#define GX_AURORA_DESTROY_TLUT 0x0033

#define GX_AURORA_DESTROY_COPY_TEX 0x0034

/**
 * Draw primitives with the vertex count derived from a byte length, as written by
 * GXBegin(prim, fmt, GX_AUTO). Must be followed by a u8 draw opcode (vtxfmt|prim),
 * a u32 vertex data byte length, then that many bytes of vertex data. The byte length
 * must be a whole multiple of the current vertex size or zero (no draw).
 */
#define GX_AURORA_DRAW_SIZED 0x0040

/**
 * Draw pre-merged triangles with a prebuilt index buffer, as written by the display
 * list optimizer (aurora::gx::dl::optimize). Must be followed by a u8 draw opcode
 * (vtxfmt | GX_TRIANGLES), a u16 vertex count, a u32 index count, that many u16
 * indices, then vertex count * vertex size bytes of packed vertex data. Index data
 * is always host-endian regardless of stream endianness.
 */
#define GX_AURORA_DRAW_INDEXED 0x0041

#define GX2_SET_POLYGON_OFFSET 0x1000

/**
 * GX2SetStencilMask: compare masks, write masks and reference values for both faces.
 * Must be followed by six u8 values: pre_mask_front, write_mask_front, ref_front,
 * pre_mask_back, write_mask_back, ref_back. (Aurora uses the front-face values.)
 */
#define GX2_SET_STENCIL_MASK 0x1001

/**
 * GX2SetDepthStencilControl: depth test/write/func + stencil enable/funcs/ops, one register.
 * Must be followed by thirteen u8 values: depth_test, depth_write, depth_func (GXCompare),
 * stencil_test, back_stencil_enable, front_func (GXCompare), front_zpass, front_zfail, front_fail
 * (GXStencilOp), back_func, back_zpass, back_zfail, back_fail. (Aurora uses the front face.)
 * NOTE: like on GX2, any GXSetZMode (BP zmode) write also disables the stencil test.
 */
#define GX2_SET_DEPTH_STENCIL_CONTROL 0x1002


/*
 * Debug marker stuff
 */

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
void GXPushDebugGroup(const char* label);

/**
 * Pop a debug group previously pushed via GXPushDebugGroup().
 */
void GXPopDebugGroup();

/**
 * Sends a debug marker to the backend graphics API. These may show in debugging tools such as RenderDoc.
 */
void GXInsertDebugMarker(const char* label);

typedef enum _AuroraViewportPolicy {
  AURORA_VIEWPORT_FIT = 0,     // Preserve logical aspect in the content framebuffer
  AURORA_VIEWPORT_STRETCH = 1, // Match content framebuffer aspect to the native surface
  AURORA_VIEWPORT_NATIVE = 2,  // Use active framebuffer pixels directly
} AuroraViewportPolicy;

/**
 * Configures content framebuffer sizing and how GXSetViewport/GXSetScissor parameters are applied to rendering.
 * When AURORA_VIEWPORT_NATIVE is used, GXSetTexCopySrc/GXSetTexCopyDst will use native framebuffer resolution.
 */
void AuroraSetViewportPolicy(AuroraViewportPolicy policy);

/**
 * Retrieves the current content framebuffer size.
 */
void AuroraGetRenderSize(u32* width, u32* height);

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetViewport.
 */
void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz);

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetScissor.
 */
void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht);

void GX2SetPolygonOffset(f32 mFrontOffset, f32 mFrontScale, f32 mBackOffset, f32 mBackScale, f32 mClamp);

typedef enum GXStencilOp {
  GX_ST_KEEP = 0,
  GX_ST_ZERO = 1,
  GX_ST_REPLACE = 2,
  GX_ST_INCR = 3,
  GX_ST_DECR = 4,
  GX_ST_INVERT = 5,
} GXStencilOp;

/**
 * GX2SetStencilMask analogue (compare/write masks + refs; front face used).
 * u8 parameters so decompiled GX2 call sites transcribe verbatim.
 */
void GX2SetStencilMask(u8 pre_mask_front, u8 write_mask_front, u8 ref_front, u8 pre_mask_back,
                       u8 write_mask_back, u8 ref_back);

/**
 * GX2SetDepthStencilControl analogue. depth_func/front_func take GXCompare values,
 * the op parameters GXStencilOp values (both numerically identical to GX2 — decomp args are
 * valid verbatim). A subsequent GXSetZMode write disables the stencil test, as on GX2.
 */
void GX2SetDepthStencilControl(u8 depth_test, u8 depth_write, u8 depth_func, u8 stencil_test,
                               u8 back_stencil_enable, u8 front_func, u8 front_zpass,
                               u8 front_zfail, u8 front_fail, u8 back_func, u8 back_zpass,
                               u8 back_zfail, u8 back_fail);

/**
 * Returns whether the backend depth buffer carries a stencil aspect.
 */
GXBool GX2SupportsStencil(void);

/**
 * Create an offscreen framebuffer and switch rendering to it.
 * All subsequent GX rendering will target this framebuffer until GXRestoreFrameBuffer() is called.
 * Use GXCopyTex to resolve the offscreen content into a texture.
 */
void GXCreateFrameBuffer(u32 width, u32 height);

/**
 * Restore rendering to the main EFB framebuffer.
 * Must be called after GXCreateFrameBuffer() to resume normal rendering.
 */
void GXRestoreFrameBuffer(void);

#if __cplusplus
}
#endif

#endif

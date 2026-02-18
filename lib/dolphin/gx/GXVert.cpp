#include "gx.hpp"
#include "__gx.h"
#include "../../gfx/fifo.hpp"

// Track vertex count between GXBegin/GXEnd for mismatch detection
static u16 sBeginNVerts = 0;
static u32 sBeginFifoSize = 0;
static bool sInBegin = false;

extern "C" {

void GXBegin(GXPrimitive primitive, GXVtxFmt vtxFmt, u16 nVerts) {
  CHECK(!sInBegin, "GXBegin: called without matching GXEnd");

  // Flush dirty state before starting a draw
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // Flush pending primitives if needed
  if (*reinterpret_cast<u32*>(&__gx->vNum) != 0) {
    __GXSendFlushPrim();
  }

  GX_WRITE_U8(vtxFmt | primitive);
  GX_WRITE_U16(nVerts);

  // Record state for vertex count validation in GXEnd
  sBeginNVerts = nVerts;
  sBeginFifoSize = aurora::gfx::fifo::get_buffer_size();
  sInBegin = true;
}

void GXEnd() {
  if (sInBegin) {
    u32 bytesWritten = aurora::gfx::fifo::get_buffer_size() - sBeginFifoSize;
    if (sBeginNVerts > 0 && bytesWritten > 0) {
      u32 vtxSize = bytesWritten / sBeginNVerts;
      u32 remainder = bytesWritten % sBeginNVerts;
      if (remainder != 0) {
        Log.warn("GXEnd: vertex data not evenly divisible: {} bytes for {} vertices", bytesWritten, sBeginNVerts);
      }
      u32 actualVerts = (vtxSize > 0) ? bytesWritten / vtxSize : 0;
      CHECK(actualVerts == sBeginNVerts,
            "GXEnd: vertex count mismatch: GXBegin declared {} vertices ({}B each, {}B total) "
            "but {} bytes were written ({} vertices)",
            sBeginNVerts, vtxSize, sBeginNVerts * vtxSize, bytesWritten, actualVerts);
    }
    sInBegin = false;
  }
  if (!aurora::gfx::fifo::in_display_list()) {
    aurora::gfx::fifo::drain();
  }
}

void GXPosition3f32(f32 x, f32 y, f32 z) {
  GX_WRITE_F32(x);
  GX_WRITE_F32(y);
  GX_WRITE_F32(z);
}

void GXPosition3u16(u16 x, u16 y, u16 z) {
  GX_WRITE_U16(x);
  GX_WRITE_U16(y);
  GX_WRITE_U16(z);
}

void GXPosition3s16(s16 x, s16 y, s16 z) {
  GX_WRITE_U16(static_cast<u16>(x));
  GX_WRITE_U16(static_cast<u16>(y));
  GX_WRITE_U16(static_cast<u16>(z));
}

void GXPosition3u8(u8 x, u8 y, u8 z) {
  GX_WRITE_U8(x);
  GX_WRITE_U8(y);
  GX_WRITE_U8(z);
}

void GXPosition3s8(s8 x, s8 y, s8 z) {
  GX_WRITE_U8(static_cast<u8>(x));
  GX_WRITE_U8(static_cast<u8>(y));
  GX_WRITE_U8(static_cast<u8>(z));
}

void GXPosition2f32(f32 x, f32 y) {
  GX_WRITE_F32(x);
  GX_WRITE_F32(y);
}

void GXPosition2u16(u16 x, u16 y) {
  GX_WRITE_U16(x);
  GX_WRITE_U16(y);
}

void GXPosition2s16(s16 x, s16 y) {
  GX_WRITE_U16(static_cast<u16>(x));
  GX_WRITE_U16(static_cast<u16>(y));
}

void GXPosition2u8(u8 x, u8 y) {
  GX_WRITE_U8(x);
  GX_WRITE_U8(y);
}

void GXPosition2s8(s8 x, s8 y) {
  GX_WRITE_U8(static_cast<u8>(x));
  GX_WRITE_U8(static_cast<u8>(y));
}

void GXPosition1x16(u16 idx) { GX_WRITE_U16(idx); }

void GXPosition1x8(u8 idx) { GX_WRITE_U8(idx); }

void GXNormal3f32(f32 x, f32 y, f32 z) {
  GX_WRITE_F32(x);
  GX_WRITE_F32(y);
  GX_WRITE_F32(z);
}

void GXNormal3s16(s16 x, s16 y, s16 z) {
  GX_WRITE_U16(static_cast<u16>(x));
  GX_WRITE_U16(static_cast<u16>(y));
  GX_WRITE_U16(static_cast<u16>(z));
}

void GXNormal3s8(s8 x, s8 y, s8 z) {
  GX_WRITE_U8(static_cast<u8>(x));
  GX_WRITE_U8(static_cast<u8>(y));
  GX_WRITE_U8(static_cast<u8>(z));
}

void GXNormal1x16(u16 index) { GX_WRITE_U16(index); }

void GXNormal1x8(u8 index) { GX_WRITE_U8(index); }

void GXColor4f32(f32 r, f32 g, f32 b, f32 a) {
  GX_WRITE_U8(static_cast<u8>(r * 255.0));
  GX_WRITE_U8(static_cast<u8>(g * 255.0));
  GX_WRITE_U8(static_cast<u8>(b * 255.0));
  GX_WRITE_U8(static_cast<u8>(a * 255.0));
}

void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
  GX_WRITE_U8(r);
  GX_WRITE_U8(g);
  GX_WRITE_U8(b);
  GX_WRITE_U8(a);
}

void GXColor3u8(u8 r, u8 g, u8 b) {
  GX_WRITE_U8(r);
  GX_WRITE_U8(g);
  GX_WRITE_U8(b);
}

void GXColor1u32(u32 clr) { GX_WRITE_U32(clr); }

void GXColor1u16(u16 clr) { GX_WRITE_U16(clr); }

void GXColor1x16(u16 index) { GX_WRITE_U16(index); }

void GXColor1x8(u8 index) { GX_WRITE_U8(index); }

void GXTexCoord2f32(f32 s, f32 t) {
  GX_WRITE_F32(s);
  GX_WRITE_F32(t);
}

void GXTexCoord2u16(u16 s, u16 t) {
  GX_WRITE_U16(s);
  GX_WRITE_U16(t);
}

void GXTexCoord2s16(s16 s, s16 t) {
  GX_WRITE_U16(static_cast<u16>(s));
  GX_WRITE_U16(static_cast<u16>(t));
}

void GXTexCoord2u8(u8 s, u8 t) {
  GX_WRITE_U8(s);
  GX_WRITE_U8(t);
}

void GXTexCoord2s8(s8 s, s8 t) {
  GX_WRITE_U8(static_cast<u8>(s));
  GX_WRITE_U8(static_cast<u8>(t));
}

void GXTexCoord1f32(f32 s) { GX_WRITE_F32(s); }

void GXTexCoord1u16(u16 s) { GX_WRITE_U16(s); }

void GXTexCoord1s16(s16 s) { GX_WRITE_U16(static_cast<u16>(s)); }

void GXTexCoord1u8(u8 s) { GX_WRITE_U8(s); }

void GXTexCoord1s8(s8 s) { GX_WRITE_U8(static_cast<u8>(s)); }

void GXTexCoord1x16(u16 index) { GX_WRITE_U16(index); }

void GXTexCoord1x8(u8 index) { GX_WRITE_U8(index); }

} // extern "C"

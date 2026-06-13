#pragma once

#include <dolphin/gx.h>

#include <array>
#include <optional>
#include <vector>

namespace aurora::gx::dl {

struct VtxLayout {
  struct Attr {
    u8 offset = 0;
    u8 size = 0;
    GXAttrType type = GX_NONE;
  };
  Attr attrs[GX_VA_MAX_ATTR]{};
  u8 stride = 0;
};

using VtxFmtLists = std::array<const GXVtxAttrFmtList*, GX_MAX_VTXFMT>;

struct DrawCmd {
  GXPrimitive prim;
  GXVtxFmt fmt;
  u16 vtxCount;
  const u8* vertices;
  const VtxLayout* layout;
  const u8* indices; // DrawIndexed only: native-endian u16s
  u32 indexCount;

  const u8* vtx(u32 vtx) const { return vertices + vtx * layout->stride; }
  /** Read an indexed attribute (GX_INDEX8 / GX_INDEX16) or a GX_DIRECT MTXIDX. */
  u16 attr_idx(u32 vtx, GXAttr attr) const;
  /** Read one prebuilt index (DrawIndexed only). */
  u16 index(u32 i) const;
};

struct Command {
  enum class Kind {
    Draw,        // GX draw prim
    DrawIndexed, // GX_AURORA_DRAW_INDEXED
    Passthrough, // everything else
  };
  Kind kind;
  const u8* data; // full command bytes, including the opcode
  u32 size;
  DrawCmd draw; // if Draw or DrawIndexed
};

/**
 * Parses a big-endian GX display list command by command.
 *
 * Vertex layouts are computed per vertex format from the desc list.
 * GX_VA_*MTXIDX attributes are always one byte.
 * Any other GX_DIRECT attribute requires a GXVtxAttrFmtList for the referenced
 * GX_VTXFMT; if not present, the reader will return failure.
 */
class Reader {
public:
  Reader(const u8* dl, u32 size, const GXVtxDescList* desc, const VtxFmtLists* fmts = nullptr);
  /**
   * Stride-only reader for callers that know the vertex stride but not the
   * attribute format. DrawCmd::attr_idx will always return 0.
   */
  Reader(const u8* dl, u32 size, u8 stride);

  std::optional<Command> next();
  bool failed() const { return mFailed; }
  u32 pos() const { return mPos; }

private:
  const u8* mData;
  u32 mSize;
  u32 mPos = 0;
  const GXVtxDescList* mDesc = nullptr;
  const VtxFmtLists* mFmts = nullptr;
  bool mFailed = false;
  std::optional<VtxLayout> mLayouts[GX_MAX_VTXFMT];
  bool mLayoutComputed[GX_MAX_VTXFMT] = {};

  const VtxLayout* layout(GXVtxFmt fmt);
};

template <typename F>
bool expand_triangles(GXPrimitive prim, u16 vtxCount, F&& f) {
  switch (prim) {
  case GX_TRIANGLES:
    if (vtxCount < 3 || vtxCount % 3 != 0) {
      return false;
    }
    for (u16 v = 0; v < vtxCount; v += 3) {
      f(v, static_cast<u16>(v + 1), static_cast<u16>(v + 2));
    }
    return true;
  case GX_TRIANGLESTRIP:
    if (vtxCount < 3) {
      return false;
    }
    for (u16 v = 2; v < vtxCount; ++v) {
      if ((v & 1) == 0) {
        f(static_cast<u16>(v - 2), static_cast<u16>(v - 1), v);
      } else {
        f(static_cast<u16>(v - 1), static_cast<u16>(v - 2), v);
      }
    }
    return true;
  case GX_TRIANGLEFAN:
    if (vtxCount < 3) {
      return false;
    }
    for (u16 v = 2; v < vtxCount; ++v) {
      f(0, static_cast<u16>(v - 1), v);
    }
    return true;
  case GX_QUADS:
    if (vtxCount < 4 || vtxCount % 4 != 0) {
      return false;
    }
    for (u16 v = 0; v < vtxCount; v += 4) {
      f(v, static_cast<u16>(v + 1), static_cast<u16>(v + 2));
      f(static_cast<u16>(v + 2), static_cast<u16>(v + 3), v);
    }
    return true;
  default:
    return false;
  }
}

/**
 * Rewrite a display list, merging adjacent triangulable draws into prebuilt GX_AURORA_DRAW_INDEXED commands.
 * State commands are passed through untouched, NOPs are dropped.
 * Returns nullopt if the display list contains anything unsupported.
 */
std::optional<std::vector<u8>> optimize(const u8* dl, u32 size, const GXVtxDescList* desc,
                                        const VtxFmtLists* fmts = nullptr);

} // namespace aurora::gx::dl

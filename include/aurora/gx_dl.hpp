#pragma once

#include <dolphin/gx.h>

#include <array>
#include <optional>
#include <vector>

namespace aurora::gx::dl {

/**
 * Byte layout of one packed display-list vertex, derived from a GXVtxDescList
 * (plus a GXVtxAttrFmtList when non-matrix GX_DIRECT attributes are present).
 */
struct VtxLayout {
  struct Attr {
    u8 offset = 0;
    u8 size = 0;
    GXAttrType type = GX_NONE;
  };
  Attr attrs[GX_VA_MAX_ATTR]{};
  u8 stride = 0;
};

/**
 * Per-vertex-format attribute format lists, used to size and decode GX_DIRECT
 * attributes (and to detect 3-index GX_NRM_NBT3 normals). Entries may be null;
 * a format slot is only consulted when a draw command references it.
 */
using VtxFmtLists = std::array<const GXVtxAttrFmtList*, GX_MAX_VTXFMT>;

struct DrawCmd {
  GXPrimitive prim;
  GXVtxFmt fmt;
  u16 vtxCount;
  const u8* vertices;      // packed vertex tuples, big-endian
  const VtxLayout* layout; // layout for fmt; attrs are empty for stride-only readers
  const u8* indices;       // DrawIndexed only: host-endian u16s (unaligned), else null
  u32 indexCount;

  const u8* vtx(u32 vtx) const { return vertices + vtx * layout->stride; }
  /** Read an indexed attribute (GX_INDEX8/GX_INDEX16) or a GX_DIRECT matrix index. */
  u16 attr_idx(u32 vtx, GXAttr attr) const;
  /** Read one prebuilt index (DrawIndexed only). */
  u16 index(u32 i) const;
};

struct Command {
  enum class Kind {
    Draw,        // standard GX draw primitive
    DrawIndexed, // GX_LOAD_AURORA_DRAW_INDEXED
    Passthrough, // NOP / BP / CP / XF / INDX / CALL_DL: sized but not decoded
  };
  Kind kind;
  const u8* data; // full command bytes, including the opcode
  u32 size;
  DrawCmd draw;   // valid when kind != Passthrough
};

/**
 * Walks a big-endian GX display list command by command.
 *
 * Vertex layouts are computed per vertex format from the descriptor list. Matrix
 * index attributes (GX_VA_PNMTXIDX..GX_VA_TEX7MTXIDX) are always one direct byte.
 * Any other GX_DIRECT attribute requires a GXVtxAttrFmtList for the referenced
 * format slot; a draw is unsizable without one and fails the walk.
 *
 * A failed walk (unknown opcode, command overrun, unsizable draw) ends iteration
 * with failed() == true; callers should fall back to the original display list.
 */
class Reader {
public:
  Reader(const u8* dl, u32 size, const GXVtxDescList* desc, const VtxFmtLists* fmts = nullptr);
  /**
   * Stride-only reader for callers that know the vertex size but not the
   * attribute breakdown. Commands are walked and sized normally, but
   * DrawCmd::attr_idx is unusable (every attribute reads as absent).
   */
  Reader(const u8* dl, u32 size, u8 stride);

  /** Next command, or nullopt at the end of the list or on failure. */
  std::optional<Command> next();
  bool failed() const { return mFailed; }
  /** Byte offset of the next command (the failing command after a failure). */
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

/**
 * Expand a draw primitive into triangles, invoking f(i0, i1, i2) with vertex
 * ordinals (0-based within the draw) per triangle, preserving winding. Returns
 * false for non-triangle primitives and degenerate vertex counts.
 */
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
 * Rewrite a display list, merging runs of adjacent triangle-compatible draws
 * (quads/triangles/strips/fans) into single GX_LOAD_AURORA_DRAW_INDEXED commands
 * with prebuilt index buffers. State commands pass through verbatim and act as
 * merge barriers; NOPs are dropped. Runs that remain pure triangle lists are
 * emitted as plain GX_TRIANGLES draws (drawn non-indexed at runtime).
 *
 * Returns nullopt if the display list contains anything unsupported; callers
 * should keep the original display list in that case.
 */
std::optional<std::vector<u8>> optimize(const u8* dl, u32 size, const GXVtxDescList* desc,
                                        const VtxFmtLists* fmts = nullptr);

} // namespace aurora::gx::dl

#include "display_list.hpp"

#include "gx.hpp"
#include "gx_fmt.hpp"

namespace aurora::gfx::gx {
static Module Log("aurora::gfx::model");

struct DisplayListCache {
  ByteBuffer vtxBuf;
  ByteBuffer idxBuf;
  GXVtxFmt fmt;

  DisplayListCache(ByteBuffer&& vtxBuf, ByteBuffer&& idxBuf, GXVtxFmt fmt)
  : vtxBuf(std::move(vtxBuf)), idxBuf(std::move(idxBuf)), fmt(fmt) {}
};

static absl::flat_hash_map<HashType, DisplayListCache> sCachedDisplayLists;

static u32 prepare_vtx_buffer(ByteBuffer& buf, GXVtxFmt vtxfmt, const u8* ptr, u16 vtxCount) {
  using gx::g_gxState;
  struct {
    u8 count;
    GXCompType type;
  } attrArrays[GX_VA_MAX_ATTR] = {};
  u32 vtxSize = 0;
  u32 outVtxSize = 0;

  // Calculate attribute offsets and vertex size
  for (int attr = 0; attr < GX_VA_MAX_ATTR; attr++) {
    const auto& attrFmt = g_gxState.vtxFmts[vtxfmt].attrs[attr];
    switch (g_gxState.vtxDesc[attr]) {
      DEFAULT_FATAL("unhandled attribute type {}", g_gxState.vtxDesc[attr]);
    case GX_NONE:
      break;
    case GX_DIRECT:
#define COMBINE(val1, val2, val3) (((val1) << 16) | ((val2) << 8) | (val3))
      switch (COMBINE(attr, attrFmt.cnt, attrFmt.type)) {
        DEFAULT_FATAL("not handled: attr {}, cnt {}, type {}", attr, attrFmt.cnt, attrFmt.type);
      case COMBINE(GX_VA_POS, GX_POS_XYZ, GX_F32):
      case COMBINE(GX_VA_NRM, GX_NRM_XYZ, GX_F32):
        attrArrays[attr].count = 3;
        attrArrays[attr].type = GX_F32;
        vtxSize += 12;
        outVtxSize += 12;
        break;
      case COMBINE(GX_VA_POS, GX_POS_XYZ, GX_S16):
      case COMBINE(GX_VA_NRM, GX_NRM_XYZ, GX_S16):
        attrArrays[attr].count = 3;
        attrArrays[attr].type = GX_S16;
        vtxSize += 6;
        outVtxSize += 12;
        break;
      case COMBINE(GX_VA_TEX0, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX1, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX2, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX3, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX4, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX5, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX6, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX7, GX_TEX_ST, GX_F32):
        attrArrays[attr].count = 2;
        attrArrays[attr].type = GX_F32;
        vtxSize += 8;
        outVtxSize += 8;
        break;
      case COMBINE(GX_VA_TEX0, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX1, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX2, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX3, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX4, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX5, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX6, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX7, GX_TEX_ST, GX_S16):
        attrArrays[attr].count = 2;
        attrArrays[attr].type = GX_S16;
        vtxSize += 4;
        outVtxSize += 8;
        break;
      case COMBINE(GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8):
      case COMBINE(GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8):
        attrArrays[attr].count = 4;
        attrArrays[attr].type = GX_RGBA8;
        vtxSize += 4;
        outVtxSize += 16;
        break;
      }
#undef COMBINE
      break;
    case GX_INDEX8:
      ++vtxSize;
      outVtxSize += 2;
      break;
    case GX_INDEX16:
      vtxSize += 2;
      outVtxSize += 2;
      break;
    }
  }
  // Align to 4
  int rem = outVtxSize % 4;
  int padding = 0;
  if (rem != 0) {
    padding = 4 - rem;
    outVtxSize += padding;
  }

  // Build vertex buffer
  buf.reserve_extra(vtxCount * outVtxSize);
  std::array<f32, 4> out{};
  for (u32 v = 0; v < vtxCount; ++v) {
    for (int attr = 0; attr < GX_VA_MAX_ATTR; attr++) {
      if (g_gxState.vtxDesc[attr] == GX_INDEX8) {
        buf.append(static_cast<u16>(*ptr));
        ++ptr;
      } else if (g_gxState.vtxDesc[attr] == GX_INDEX16) {
        buf.append(bswap(*reinterpret_cast<const u16*>(ptr)));
        ptr += 2;
      }
      if (g_gxState.vtxDesc[attr] != GX_DIRECT) {
        continue;
      }
      const auto& attrFmt = g_gxState.vtxFmts[vtxfmt].attrs[attr];
      u8 count = attrArrays[attr].count;
      switch (attrArrays[attr].type) {
      case GX_U8:
        for (int i = 0; i < count; ++i) {
          const auto value = reinterpret_cast<const u8*>(ptr)[i];
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count;
        break;
      case GX_S8:
        for (int i = 0; i < count; ++i) {
          const auto value = reinterpret_cast<const s8*>(ptr)[i];
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count;
        break;
      case GX_U16:
        for (int i = 0; i < count; ++i) {
          const auto value = bswap(reinterpret_cast<const u16*>(ptr)[i]);
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(u16);
        break;
      case GX_S16:
        for (int i = 0; i < count; ++i) {
          const auto value = bswap(reinterpret_cast<const s16*>(ptr)[i]);
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(s16);
        break;
      case GX_F32:
        for (int i = 0; i < count; ++i) {
          out[i] = bswap(reinterpret_cast<const f32*>(ptr)[i]);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(f32);
        break;
      case GX_RGBA8:
        out[0] = static_cast<f32>(ptr[0]) / 255.f;
        out[1] = static_cast<f32>(ptr[1]) / 255.f;
        out[2] = static_cast<f32>(ptr[2]) / 255.f;
        out[3] = static_cast<f32>(ptr[3]) / 255.f;
        buf.append(out.data(), sizeof(f32) * 4);
        ptr += sizeof(u32);
        break;
      }
    }
    if (padding > 0) {
      buf.append_zeroes(padding);
    }
  }

  return vtxSize;
}

static u16 prepare_idx_buffer(ByteBuffer& buf, GXPrimitive prim, u16 vtxStart, u16 vtxCount) {
  u16 numIndices = 0;
  if (prim == GX_TRIANGLES) {
    buf.reserve_extra(vtxCount * sizeof(u16));
    for (u16 v = 0; v < vtxCount; ++v) {
      const u16 idx = vtxStart + v;
      buf.append(idx);
      ++numIndices;
    }
  } else if (prim == GX_TRIANGLEFAN) {
    buf.reserve_extra(((u32(vtxCount) - 3) * 3 + 3) * sizeof(u16));
    for (u16 v = 0; v < vtxCount; ++v) {
      const u16 idx = vtxStart + v;
      if (v < 3) {
        buf.append(idx);
        ++numIndices;
        continue;
      }
      buf.append(std::array{vtxStart, static_cast<u16>(idx - 1), idx});
      numIndices += 3;
    }
  } else if (prim == GX_TRIANGLESTRIP) {
    buf.reserve_extra(((static_cast<u32>(vtxCount) - 3) * 3 + 3) * sizeof(u16));
    for (u16 v = 0; v < vtxCount; ++v) {
      const u16 idx = vtxStart + v;
      if (v < 3) {
        buf.append(idx);
        ++numIndices;
        continue;
      }
      if ((v & 1) == 0) {
        buf.append(std::array{static_cast<u16>(idx - 2), static_cast<u16>(idx - 1), idx});
      } else {
        buf.append(std::array{static_cast<u16>(idx - 1), static_cast<u16>(idx - 2), idx});
      }
      numIndices += 3;
    }
  } else
    UNLIKELY FATAL("unsupported primitive type {}", static_cast<u32>(prim));
  return numIndices;
}

auto process_display_list(const u8* dlStart, u32 dlSize) -> DisplayListResult {
  const auto hash = xxh3_hash_s(dlStart, dlSize, 0);
  Range vertRange, idxRange;
  u32 numIndices = 0;
  GXVtxFmt fmt = GX_MAX_VTXFMT;
  auto it = sCachedDisplayLists.find(hash);
  if (it != sCachedDisplayLists.end()) {
    const auto& cache = it->second;
    numIndices = cache.idxBuf.size() / 2;
    vertRange = push_verts(cache.vtxBuf.data(), cache.vtxBuf.size());
    idxRange = push_indices(cache.idxBuf.data(), cache.idxBuf.size());
    fmt = cache.fmt;
  } else {
    const u8* data = dlStart;
    u32 pos = 0;
    ByteBuffer vtxBuf;
    ByteBuffer idxBuf;
    u16 vtxStart = 0;

    while (pos < dlSize) {
      u8 cmd = data[pos++];

      u8 opcode = cmd & GX_OPCODE_MASK;
      switch (opcode) {
        DEFAULT_FATAL("unimplemented opcode: {}", opcode);
      case GX_NOP:
        continue;
      case GX_DRAW_QUADS:
      case GX_DRAW_TRIANGLES:
      case GX_DRAW_TRIANGLE_STRIP:
      case GX_DRAW_TRIANGLE_FAN: {
        const auto prim = static_cast<GXPrimitive>(opcode);
        const auto newFmt = static_cast<GXVtxFmt>(cmd & GX_VAT_MASK);
        if (fmt != GX_MAX_VTXFMT && fmt != newFmt) {
          FATAL("Vertex format changed mid-display list: {} -> {}", fmt, newFmt);
        }
        fmt = newFmt;
        u16 vtxCount = bswap(*reinterpret_cast<const u16*>(data + pos));
        pos += 2;
        pos += vtxCount * prepare_vtx_buffer(vtxBuf, fmt, data + pos, vtxCount);
        numIndices += prepare_idx_buffer(idxBuf, prim, vtxStart, vtxCount);
        vtxStart += vtxCount;
        break;
      }
      case GX_DRAW_LINES:
      case GX_DRAW_LINE_STRIP:
      case GX_DRAW_POINTS:
        FATAL("unimplemented prim type: {}", opcode);
        break;
      }
    }
    vertRange = push_verts(vtxBuf.data(), vtxBuf.size());
    idxRange = push_indices(idxBuf.data(), idxBuf.size());
    sCachedDisplayLists.try_emplace(hash, std::move(vtxBuf), std::move(idxBuf), fmt);
  }

  return {
      .vertRange = vertRange,
      .idxRange = idxRange,
      .numIndices = numIndices,
      .fmt = fmt,
  };
}
} // namespace aurora::gfx::gx
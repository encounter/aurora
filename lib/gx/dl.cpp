#include "aurora/dl.hpp"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXCommandList.h>

#include "../internal.hpp"
#include "gx.hpp"

namespace aurora::gx::dl {
static Module Log("aurora::gx::dl");

static bool is_draw_opcode(u8 opcode) {
  return opcode == GX_QUADS || opcode == GX_TRIANGLES || opcode == GX_TRIANGLESTRIP || opcode == GX_TRIANGLEFAN ||
         opcode == GX_LINES || opcode == GX_LINESTRIP || opcode == GX_POINTS;
}

static u16 read_be16(const u8* data) { return static_cast<u16>(data[0]) << 8 | data[1]; }

static u32 read_be32(const u8* data) {
  return static_cast<u32>(data[0]) << 24 | static_cast<u32>(data[1]) << 16 | static_cast<u32>(data[2]) << 8 | data[3];
}

static const GXVtxAttrFmtList* find_attr_fmt(const GXVtxAttrFmtList* list, GXAttr attr) {
  if (list == nullptr) {
    return nullptr;
  }
  for (; list->attr != GX_VA_NULL; ++list) {
    if (list->attr == attr) {
      return list;
    }
  }
  return nullptr;
}

static std::optional<VtxLayout> compute_layout(const GXVtxDescList* desc, const GXVtxAttrFmtList* fmt) {
  VtxLayout out{};
  u32 stride = 0;
  for (; desc->attr != GX_VA_NULL; ++desc) {
    if (desc->attr >= GX_VA_MAX_ATTR) {
      return std::nullopt;
    }
    u32 size = 0;
    switch (desc->type) {
    case GX_NONE:
      continue;
    case GX_DIRECT: {
      if (desc->attr >= GX_VA_PNMTXIDX && desc->attr <= GX_VA_TEX7MTXIDX) {
        size = 1;
        break;
      }
      const auto* attrFmt = find_attr_fmt(fmt, desc->attr);
      if (attrFmt == nullptr) {
        return std::nullopt;
      }
      size = comp_type_size(desc->attr, attrFmt->type) * comp_cnt_count(desc->attr, attrFmt->cnt);
      break;
    }
    case GX_INDEX8:
      size = 1;
      break;
    case GX_INDEX16:
      size = 2;
      break;
    default:
      return std::nullopt;
    }
    if (desc->attr == GX_VA_NRM && (desc->type == GX_INDEX8 || desc->type == GX_INDEX16)) {
      // GX_NRM_NBT3 normals are three separate indices
      const auto* attrFmt = find_attr_fmt(fmt, GX_VA_NRM);
      if (attrFmt != nullptr && attrFmt->cnt == GX_NRM_NBT3) {
        size *= 3;
      }
    }
    auto& attr = out.attrs[desc->attr];
    attr.offset = static_cast<u8>(stride);
    attr.size = static_cast<u8>(size);
    attr.type = desc->type;
    stride += size;
    if (stride > 0xFF) {
      return std::nullopt;
    }
  }
  if (stride == 0) {
    return std::nullopt;
  }
  out.stride = static_cast<u8>(stride);
  return out;
}

u16 DrawCmd::attr_idx(u32 vtxIdx, GXAttr attr) const {
  const auto& a = layout->attrs[attr];
  const u8* ptr = vertices + vtxIdx * layout->stride + a.offset;
  switch (a.type) {
  case GX_INDEX8:
    return ptr[0];
  case GX_INDEX16:
    return read_be16(ptr);
  case GX_DIRECT: // *MTXIDX only
    return ptr[0];
  default:
    return 0;
  }
}

u16 DrawCmd::index(u32 i) const {
  u16 value;
  std::memcpy(&value, indices + i * sizeof(u16), sizeof(u16));
  return value;
}

Reader::Reader(const u8* dl, u32 size, const GXVtxDescList* desc, const VtxFmtLists* fmts)
: mData(dl), mSize(size), mDesc(desc), mFmts(fmts) {}

Reader::Reader(const u8* dl, u32 size, u8 stride) : mData(dl), mSize(size) {
  VtxLayout layout{};
  layout.stride = stride;
  for (u32 fmt = 0; fmt < GX_MAX_VTXFMT; ++fmt) {
    mLayouts[fmt] = layout;
    mLayoutComputed[fmt] = true;
  }
}

const VtxLayout* Reader::layout(GXVtxFmt fmt) {
  if (!mLayoutComputed[fmt]) {
    mLayoutComputed[fmt] = true;
    if (mDesc != nullptr) {
      mLayouts[fmt] = compute_layout(mDesc, mFmts != nullptr ? (*mFmts)[fmt] : nullptr);
    }
  }
  return mLayouts[fmt].has_value() ? &*mLayouts[fmt] : nullptr;
}

std::optional<Command> Reader::next() {
  if (mFailed || mPos >= mSize) {
    return std::nullopt;
  }

  const u32 start = mPos;
  const u8 cmd = mData[start];
  const u8 opcode = cmd & GX_OPCODE_MASK;

  const auto fail = [&](const char* what) -> std::optional<Command> {
    Log.warn("Reader: {} (opcode 0x{:02X} at offset {})", what, cmd, start);
    mFailed = true;
    return std::nullopt;
  };
  const auto passthrough = [&](const u32 cmdSize) -> std::optional<Command> {
    if (start + cmdSize > mSize) {
      return fail("command overrun");
    }
    mPos = start + cmdSize;
    return Command{Command::Kind::Passthrough, mData + start, cmdSize, {}};
  };

  switch (opcode) {
  case GX_NOP:
  case GX_CMD_INVL_VC:
    return passthrough(1);
  case GX_LOAD_BP_REG & GX_OPCODE_MASK:
    return passthrough(5);
  case GX_LOAD_CP_REG:
    return passthrough(6);
  case GX_LOAD_XF_REG: {
    if (start + 5 > mSize) {
      return fail("XF load overrun");
    }
    const u32 count = read_be16(mData + start + 1) + 1;
    return passthrough(5 + count * 4);
  }
  case GX_LOAD_INDX_A:
  case GX_LOAD_INDX_B:
  case GX_LOAD_INDX_C:
  case GX_LOAD_INDX_D:
    return passthrough(5);
  case GX_CMD_CALL_DL:
    return passthrough(9);
  case GX_AURORA: {
    if (start + 3 > mSize) {
      return fail("Aurora subcommand overrun");
    }
    if (read_be16(mData + start + 1) != GX_AURORA_DRAW_INDEXED) {
      return fail("unsupported Aurora subcommand");
    }
    if (start + 10 > mSize) {
      return fail("DRAW_INDEXED header overrun");
    }
    const u8 drawCmd = mData[start + 3];
    const auto fmt = static_cast<GXVtxFmt>(drawCmd & GX_VAT_MASK);
    const u16 vtxCount = read_be16(mData + start + 4);
    const u32 indexCount = read_be32(mData + start + 6);
    const VtxLayout* lo = layout(fmt);
    if (lo == nullptr) {
      return fail("no layout for DRAW_INDEXED vertex format");
    }
    const u32 idxBytes = indexCount * sizeof(u16);
    const u32 cmdSize = 10 + idxBytes + vtxCount * lo->stride;
    if (start + cmdSize > mSize) {
      return fail("DRAW_INDEXED data overrun");
    }
    mPos = start + cmdSize;
    return Command{
        Command::Kind::DrawIndexed,
        mData + start,
        cmdSize,
        DrawCmd{
            static_cast<GXPrimitive>(drawCmd & GX_OPCODE_MASK),
            fmt,
            vtxCount,
            mData + start + 10 + idxBytes,
            lo,
            mData + start + 10,
            indexCount,
        },
    };
  }
  default: {
    if (!is_draw_opcode(opcode)) {
      return fail("unknown opcode");
    }
    if (start + 3 > mSize) {
      return fail("draw header overrun");
    }
    const auto fmt = static_cast<GXVtxFmt>(cmd & GX_VAT_MASK);
    const u16 vtxCount = read_be16(mData + start + 1);
    const VtxLayout* lo = layout(fmt);
    if (lo == nullptr) {
      return fail("no layout for draw vertex format");
    }
    const u32 cmdSize = 3 + vtxCount * lo->stride;
    if (start + cmdSize > mSize) {
      return fail("draw data overrun");
    }
    mPos = start + cmdSize;
    return Command{
        Command::Kind::Draw,
        mData + start,
        cmdSize,
        DrawCmd{
            static_cast<GXPrimitive>(opcode),
            fmt,
            vtxCount,
            mData + start + 3,
            lo,
            nullptr,
            0,
        },
    };
  }
  }
}

namespace {

struct DrawBatch {
  GXVtxFmt fmt = GX_VTXFMT0;
  u16 vtxCount = 0;
  bool allTriangles = true;
  std::vector<u8> verts;
  std::vector<u16> indices;
};

void push_be16(std::vector<u8>& out, u16 value) {
  out.push_back(value >> 8);
  out.push_back(value & 0xFF);
}

void push_be32(std::vector<u8>& out, u32 value) {
  out.push_back(value >> 24);
  out.push_back(value >> 16 & 0xFF);
  out.push_back(value >> 8 & 0xFF);
  out.push_back(value & 0xFF);
}

void flush_batch(std::vector<u8>& out, DrawBatch& batch) {
  if (batch.vtxCount != 0) {
    if (batch.allTriangles) {
      // plain triangle draw does not require an index buffer
      out.push_back(static_cast<u8>(GX_TRIANGLES) | static_cast<u8>(batch.fmt));
      push_be16(out, batch.vtxCount);
    } else {
      out.push_back(GX_AURORA);
      push_be16(out, GX_AURORA_DRAW_INDEXED);
      out.push_back(static_cast<u8>(GX_TRIANGLES) | static_cast<u8>(batch.fmt));
      push_be16(out, batch.vtxCount);
      push_be32(out, static_cast<u32>(batch.indices.size()));
      // index data is host-endian; see GX_AURORA_DRAW_INDEXED
      const auto* idxData = reinterpret_cast<const u8*>(batch.indices.data());
      out.insert(out.end(), idxData, idxData + batch.indices.size() * sizeof(u16));
    }
    out.insert(out.end(), batch.verts.begin(), batch.verts.end());
  }
  batch.vtxCount = 0;
  batch.allTriangles = true;
  batch.verts.clear();
  batch.indices.clear();
}

} // namespace

std::optional<std::vector<u8>> optimize(const u8* dl, u32 size, const GXVtxDescList* desc, const VtxFmtLists* fmts) {
  Reader reader{dl, size, desc, fmts};
  std::vector<u8> out;
  out.reserve(size);
  DrawBatch batch;

  while (const auto cmd = reader.next()) {
    const auto copy_verbatim = [&] {
      flush_batch(out, batch);
      out.insert(out.end(), cmd->data, cmd->data + cmd->size);
    };

    switch (cmd->kind) {
    case Command::Kind::Passthrough:
      if (cmd->data[0] != GX_NOP) {
        copy_verbatim();
      }
      break;
    case Command::Kind::DrawIndexed:
      copy_verbatim();
      break;
    case Command::Kind::Draw: {
      const auto& draw = cmd->draw;
      if (batch.vtxCount != 0 && (batch.fmt != draw.fmt || static_cast<u32>(batch.vtxCount) + draw.vtxCount > 0xFFFF)) {
        flush_batch(out, batch);
      }
      const u16 base = batch.vtxCount;
      const bool expanded = expand_triangles(draw.prim, draw.vtxCount, [&](u16 i0, u16 i1, u16 i2) {
        batch.indices.push_back(base + i0);
        batch.indices.push_back(base + i1);
        batch.indices.push_back(base + i2);
      });
      if (!expanded) {
        // lines/points or degenerate counts; expand_triangles validates before emitting
        copy_verbatim();
        break;
      }
      if (batch.vtxCount == 0) {
        batch.fmt = draw.fmt;
      }
      if (draw.prim != GX_TRIANGLES) {
        batch.allTriangles = false;
      }
      batch.verts.insert(batch.verts.end(), draw.vertices, draw.vertices + draw.vtxCount * draw.layout->stride);
      batch.vtxCount += draw.vtxCount;
      break;
    }
    }
  }

  if (reader.failed()) {
    return std::nullopt;
  }
  flush_batch(out, batch);
  return out;
}

} // namespace aurora::gx::dl

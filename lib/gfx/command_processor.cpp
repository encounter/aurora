#include "command_processor.hpp"

#include "gx.hpp"
#include "gx_fmt.hpp"
#include "model/shader.hpp"
#include "shader_info.hpp"
#include "../internal.hpp"

#include <absl/container/flat_hash_map.h>

#include <cmath>
#include <cstring>

static aurora::Module Log("aurora::gfx::cp");

using aurora::gfx::gx::g_gxState;

namespace aurora::gfx::command_processor {

struct DisplayListCache {
  ByteBuffer vtxBuf;
  ByteBuffer idxBuf;
  GXVtxFmt fmt;

  DisplayListCache(ByteBuffer&& vtxBuf, ByteBuffer&& idxBuf, GXVtxFmt fmt)
  : vtxBuf(std::move(vtxBuf)), idxBuf(std::move(idxBuf)), fmt(fmt) {}
};

static absl::flat_hash_map<HashType, DisplayListCache> sCachedDisplayLists;

static u32 prepare_vtx_buffer(ByteBuffer* outBuf, GXVtxFmt vtxfmt, const u8* ptr, u16 vtxCount, bool bigEndian) {
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
        DEFAULT_FATAL("not handled: attr {}, cnt {}, type {}", static_cast<GXAttr>(attr), attrFmt.cnt, attrFmt.type);
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
      case COMBINE(GX_VA_POS, GX_POS_XYZ, GX_U8):
        attrArrays[attr].count = 3;
        attrArrays[attr].type = GX_U8;
        vtxSize += 3;
        outVtxSize += 12;
        break;
	  case COMBINE(GX_VA_POS, GX_POS_XY, GX_U16):
        attrArrays[attr].count = 2;
        attrArrays[attr].type = GX_16;
        vtxSize += 4;
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
      case COMBINE(GX_VA_TEX0, GX_TEX_ST, GX_U16):
      case COMBINE(GX_VA_TEX1, GX_TEX_ST, GX_U16):
      case COMBINE(GX_VA_TEX2, GX_TEX_ST, GX_U16):
      case COMBINE(GX_VA_TEX3, GX_TEX_ST, GX_U16):
      case COMBINE(GX_VA_TEX4, GX_TEX_ST, GX_U16):
      case COMBINE(GX_VA_TEX5, GX_TEX_ST, GX_U16):
      case COMBINE(GX_VA_TEX6, GX_TEX_ST, GX_U16):
      case COMBINE(GX_VA_TEX7, GX_TEX_ST, GX_U16):
        attrArrays[attr].count = 2;
        attrArrays[attr].type = GX_U16;
        vtxSize += 4;
        outVtxSize += 8;
        break;
      case COMBINE(GX_VA_TEX0, GX_TEX_ST, GX_S8):
      case COMBINE(GX_VA_TEX1, GX_TEX_ST, GX_S8):
      case COMBINE(GX_VA_TEX2, GX_TEX_ST, GX_S8):
      case COMBINE(GX_VA_TEX3, GX_TEX_ST, GX_S8):
      case COMBINE(GX_VA_TEX4, GX_TEX_ST, GX_S8):
      case COMBINE(GX_VA_TEX5, GX_TEX_ST, GX_S8):
      case COMBINE(GX_VA_TEX6, GX_TEX_ST, GX_S8):
      case COMBINE(GX_VA_TEX7, GX_TEX_ST, GX_S8):
        attrArrays[attr].count = 2;
        attrArrays[attr].type = GX_S8;
        vtxSize += 2;
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

  // Just checking size
  if (outBuf == nullptr || ptr == nullptr) {
    return vtxSize;
  }

  // Build vertex buffer
  ByteBuffer& buf = *outBuf;
  buf.reserve_extra(vtxCount * outVtxSize);
  std::array<f32, 4> out{};
  for (u32 v = 0; v < vtxCount; ++v) {
    for (int attr = 0; attr < GX_VA_MAX_ATTR; attr++) {
      if (g_gxState.vtxDesc[attr] == GX_INDEX8) {
        buf.append(static_cast<u16>(*ptr));
        ++ptr;
      } else if (g_gxState.vtxDesc[attr] == GX_INDEX16) {
        const auto value = *reinterpret_cast<const u16*>(ptr);
        buf.append(bigEndian ? bswap(value) : value);
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
          auto value = reinterpret_cast<const u16*>(ptr)[i];
          out[i] = static_cast<f32>(bigEndian ? bswap(value) : value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(u16);
        break;
      case GX_S16:
        for (int i = 0; i < count; ++i) {
          const auto value = reinterpret_cast<const s16*>(ptr)[i];
          out[i] = static_cast<f32>(bigEndian ? bswap(value) : value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(s16);
        break;
      case GX_F32:
        for (int i = 0; i < count; ++i) {
          const auto value = reinterpret_cast<const f32*>(ptr)[i];
          out[i] = bigEndian ? bswap(value) : value;
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
      if (attr == GX_VA_POS) {
        for (int i = count; i < 3; ++i) {
          buf.append(0.0f);
        }
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
  if (prim == GX_QUADS) {
    buf.reserve_extra((vtxCount / 4) * 6 * sizeof(u16));

    for (u16 v = 0; v < vtxCount; v += 4) {
      u16 idx0 = vtxStart + v;
      u16 idx1 = vtxStart + v + 1;
      u16 idx2 = vtxStart + v + 2;
      u16 idx3 = vtxStart + v + 3;

      buf.append(idx0);
      buf.append(idx1);
      buf.append(idx2);
      numIndices += 3;

      buf.append(idx2);
      buf.append(idx3);
      buf.append(idx0);
      numIndices += 3;
    }
  } else if (prim == GX_TRIANGLES) {
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

// GX FIFO opcodes - use CP_ prefix to avoid clashing with GXCommandList.h macros
static constexpr u8 CP_CMD_NOP = GX_NOP;
static constexpr u8 CP_CMD_LOAD_CP_REG = GX_LOAD_CP_REG;
static constexpr u8 CP_CMD_LOAD_XF_REG = GX_LOAD_XF_REG;
static constexpr u8 CP_CMD_LOAD_INDX_A = GX_LOAD_INDX_A;
static constexpr u8 CP_CMD_LOAD_INDX_B = GX_LOAD_INDX_B;
static constexpr u8 CP_CMD_LOAD_INDX_C = GX_LOAD_INDX_C;
static constexpr u8 CP_CMD_LOAD_INDX_D = GX_LOAD_INDX_D;
static constexpr u8 CP_CMD_CALL_DL = GX_CMD_CALL_DL;
static constexpr u8 CP_CMD_INVAL_VTX = GX_CMD_INVL_VC;
static constexpr u8 CP_CMD_LOAD_BP_REG = GX_LOAD_BP_REG & GX_OPCODE_MASK;

// Primitive type mask
static constexpr u8 CP_OPCODE_MASK = GX_OPCODE_MASK;
static constexpr u8 CP_VAT_MASK = GX_VAT_MASK;

// Read helpers for big/little endian
static inline u16 read_u16(const u8* ptr, bool bigEndian) {
  if (bigEndian) {
    return static_cast<u16>(ptr[0] << 8 | ptr[1]);
  }
  return static_cast<u16>(ptr[1] << 8 | ptr[0]);
}

static inline u32 read_u32(const u8* ptr, bool bigEndian) {
  if (bigEndian) {
    return static_cast<u32>(ptr[0]) << 24 | static_cast<u32>(ptr[1]) << 16 | static_cast<u32>(ptr[2]) << 8 |
           static_cast<u32>(ptr[3]);
  }
  return static_cast<u32>(ptr[3]) << 24 | static_cast<u32>(ptr[2]) << 16 | static_cast<u32>(ptr[1]) << 8 |
         static_cast<u32>(ptr[0]);
}

static inline f32 read_f32(const u8* ptr, bool bigEndian) {
  u32 bits = read_u32(ptr, bigEndian);
  f32 val;
  std::memcpy(&val, &bits, sizeof(val));
  return val;
}

// Forward declarations for register handlers (implemented in later phases)
static void handle_bp(u32 value, bool bigEndian);
static void handle_cp(u8 addr, u32 value, bool bigEndian);
static void handle_xf(const u8* data, u32& pos, u32 size, bool bigEndian);
static void handle_draw(u8 cmd, const u8* data, u32& pos, u32 size, bool bigEndian);

void process(const u8* data, u32 size, bool bigEndian) {
  u32 pos = 0;

  while (pos < size) {
    u8 cmd = data[pos++];
    u8 opcode = cmd & CP_OPCODE_MASK;
    // Log.warn("Processing opcode {:02x} at pos {} (size {})", opcode, pos - 1, size);

    switch (opcode) {
    case CP_CMD_NOP:
      continue;

    case CP_CMD_LOAD_BP_REG: {
      CHECK(pos + 4 <= size, "BP reg read overrun");
      u32 value = read_u32(data + pos, bigEndian);
      pos += 4;
      handle_bp(value, bigEndian);
      break;
    }

    case CP_CMD_LOAD_CP_REG: {
      CHECK(pos + 5 <= size, "CP reg read overrun");
      u8 addr = data[pos++];
      u32 value = read_u32(data + pos, bigEndian);
      pos += 4;
      handle_cp(addr, value, bigEndian);
      break;
    }

    case CP_CMD_LOAD_XF_REG: {
      handle_xf(data, pos, size, bigEndian);
      break;
    }

    case CP_CMD_LOAD_INDX_A:
    case CP_CMD_LOAD_INDX_B:
    case CP_CMD_LOAD_INDX_C:
    case CP_CMD_LOAD_INDX_D: {
      // Indexed XF load: 4 bytes of data
      CHECK(pos + 4 <= size, "indexed XF read overrun");
      Log.warn("Unimplemented indexed XF load (opcode 0x{:02X})", opcode);
      pos += 4;
      break;
    }

    case CP_CMD_CALL_DL: {
      // Call display list: 8 bytes (address + size)
      CHECK(pos + 8 <= size, "call DL read overrun");
      Log.warn("Ignoring nested GX_CMD_CALL_DL");
      pos += 8;
      break;
    }

    case CP_CMD_INVAL_VTX: {
      // Invalidate vertex cache
      break;
    }

    // Draw commands: 0x80-0xBF
    case GX_DRAW_QUADS:
    case GX_DRAW_TRIANGLES:
    case GX_DRAW_TRIANGLE_STRIP:
    case GX_DRAW_TRIANGLE_FAN:
    case GX_DRAW_LINES:
    case GX_DRAW_LINE_STRIP:
    case GX_DRAW_POINTS: {
      handle_draw(cmd, data, pos, size, bigEndian);
      break;
    }

    default:
      // Check if it's a draw command (0x80-0xBF range)
      if (cmd >= 0x80) {
        handle_draw(cmd, data, pos, size, bigEndian);
      } else {
        // Hex dump surrounding bytes for debugging
        {
          u32 dumpStart = (pos > 17) ? pos - 17 : 0;
          u32 dumpEnd = (pos + 16 < size) ? pos + 16 : size;
          std::string hex;
          for (u32 i = dumpStart; i < dumpEnd; i++) {
            if (i == pos - 1)
              hex += fmt::format("[{:02x}]", data[i]);
            else
              hex += fmt::format(" {:02x}", data[i]);
          }
          Log.error("  hex dump (pos {}-{}):{}",dumpStart, dumpEnd - 1, hex);
        }
        FATAL("command_processor: unknown opcode 0x{:02X} at pos {}", cmd, pos - 1);
      }
      break;
    }
  }
}

// Helper to extract bit fields from a 32-bit register
static inline u32 bp_get(u32 reg, u32 size, u32 shift) {
  return (reg >> shift) & ((1u << size) - 1);
}

// Helper to convert packed RGBA8 to Vec4<float>
static inline aurora::Vec4<float> unpack_color(u32 packed) {
  return {
      static_cast<float>((packed >> 24) & 0xFF) / 255.f,
      static_cast<float>((packed >> 16) & 0xFF) / 255.f,
      static_cast<float>((packed >> 8) & 0xFF) / 255.f,
      static_cast<float>(packed & 0xFF) / 255.f,
  };
}

// BP register handler - decodes BP (RAS/pixel engine) register writes and updates g_gxState
static void handle_bp(u32 value, bool bigEndian) {
  u32 regId = (value >> 24) & 0xFF;
  // Mask off the register ID from the value for field extraction
  // (the regId is stored in bits 24-31, data is in bits 0-23)

  // TEV color combiner stages (0xC0, 0xC2, 0xC4, ... 0xDE)
  if (regId >= 0xC0 && regId <= 0xDE && (regId & 1) == 0) {
    u32 stage = (regId - 0xC0) / 2;
    if (stage < gx::MaxTevStages) {
      auto& s = g_gxState.tevStages[stage];
      s.colorPass.d = static_cast<GXTevColorArg>(bp_get(value, 4, 0));
      s.colorPass.c = static_cast<GXTevColorArg>(bp_get(value, 4, 4));
      s.colorPass.b = static_cast<GXTevColorArg>(bp_get(value, 4, 8));
      s.colorPass.a = static_cast<GXTevColorArg>(bp_get(value, 4, 12));
      s.colorOp.clamp = bp_get(value, 1, 19) != 0;
      s.colorOp.outReg = static_cast<GXTevRegID>(bp_get(value, 2, 22));
      if (bp_get(value, 2, 16) == 3) {
        // Bias==3 means compare mode: reconstruct GXTevOp enum (8 + 3-bit hw value)
        u32 hwOp = bp_get(value, 1, 18) | (bp_get(value, 2, 20) << 1);
        s.colorOp.op = static_cast<GXTevOp>(hwOp + 8);
        s.colorOp.bias = GX_TB_ZERO;
        s.colorOp.scale = GX_CS_SCALE_1;
      } else {
        // Normal mode: bit18 is op (0=ADD, 1=SUB), bits16-17 is bias, bits20-21 is scale
        s.colorOp.op = static_cast<GXTevOp>(bp_get(value, 1, 18));
        s.colorOp.bias = static_cast<GXTevBias>(bp_get(value, 2, 16));
        s.colorOp.scale = static_cast<GXTevScale>(bp_get(value, 2, 20));
      }
      g_gxState.stateDirty = true;
    }
    return;
  }

  // TEV alpha combiner stages (0xC1, 0xC3, 0xC5, ... 0xDF)
  if (regId >= 0xC1 && regId <= 0xDF && (regId & 1) == 1) {
    u32 stage = (regId - 0xC1) / 2;
    if (stage < gx::MaxTevStages) {
      auto& s = g_gxState.tevStages[stage];
      s.tevSwapRas = static_cast<GXTevSwapSel>(bp_get(value, 2, 0));
      s.tevSwapTex = static_cast<GXTevSwapSel>(bp_get(value, 2, 2));
      s.alphaPass.d = static_cast<GXTevAlphaArg>(bp_get(value, 3, 4));
      s.alphaPass.c = static_cast<GXTevAlphaArg>(bp_get(value, 3, 7));
      s.alphaPass.b = static_cast<GXTevAlphaArg>(bp_get(value, 3, 10));
      s.alphaPass.a = static_cast<GXTevAlphaArg>(bp_get(value, 3, 13));
      s.alphaOp.clamp = bp_get(value, 1, 19) != 0;
      s.alphaOp.outReg = static_cast<GXTevRegID>(bp_get(value, 2, 22));
      if (bp_get(value, 2, 16) == 3) {
        u32 hwOp = bp_get(value, 1, 18) | (bp_get(value, 2, 20) << 1);
        s.alphaOp.op = static_cast<GXTevOp>(hwOp + 8);
        s.alphaOp.bias = GX_TB_ZERO;
        s.alphaOp.scale = GX_CS_SCALE_1;
      } else {
        s.alphaOp.op = static_cast<GXTevOp>(bp_get(value, 1, 18));
        s.alphaOp.bias = static_cast<GXTevBias>(bp_get(value, 2, 16));
        s.alphaOp.scale = static_cast<GXTevScale>(bp_get(value, 2, 20));
      }
      g_gxState.stateDirty = true;
    }
    return;
  }

  switch (regId) {
  // genMode (0x00)
  case 0x00: {
    g_gxState.numTexGens = bp_get(value, 4, 0);
    g_gxState.numChans = bp_get(value, 3, 4);
    g_gxState.numTevStages = bp_get(value, 4, 10) + 1;
    u32 hwCull = bp_get(value, 2, 14);
    // Swap front/back to match GX convention
    switch (hwCull) {
    case GX_CULL_FRONT:
      g_gxState.cullMode = GX_CULL_BACK;
      break;
    case GX_CULL_BACK:
      g_gxState.cullMode = GX_CULL_FRONT;
      break;
    default:
      g_gxState.cullMode = static_cast<GXCullMode>(hwCull);
      break;
    }
    g_gxState.numIndStages = bp_get(value, 3, 16);
    g_gxState.stateDirty = true;
    break;
  }

  // BP mask (0x0F) - internal, applies to next BP write
  case 0x0F:
    // The BP mask is used by the hardware to selectively update fields.
    Log.warn("BP mask set to {:06x}, but selective updates are not implemented", value & 0xFFFFFF);
    break;

  // TEV indirect stages (0x10-0x1F)
  case 0x10: case 0x11: case 0x12: case 0x13:
  case 0x14: case 0x15: case 0x16: case 0x17:
  case 0x18: case 0x19: case 0x1A: case 0x1B:
  case 0x1C: case 0x1D: case 0x1E: case 0x1F: {
    u32 stage = regId - 0x10;
    if (stage < gx::MaxTevStages) {
      auto& s = g_gxState.tevStages[stage];
      s.indTexStage = static_cast<GXIndTexStageID>(bp_get(value, 2, 0));
      s.indTexFormat = static_cast<GXIndTexFormat>(bp_get(value, 2, 2));
      s.indTexBiasSel = static_cast<GXIndTexBiasSel>(bp_get(value, 3, 4));
      s.indTexAlphaSel = static_cast<GXIndTexAlphaSel>(bp_get(value, 2, 7));
      s.indTexMtxId = static_cast<GXIndTexMtxID>(bp_get(value, 4, 9));
      s.indTexWrapS = static_cast<GXIndTexWrap>(bp_get(value, 3, 13));
      s.indTexWrapT = static_cast<GXIndTexWrap>(bp_get(value, 3, 16));
      s.indTexUseOrigLOD = bp_get(value, 1, 19) != 0;
      s.indTexAddPrev = bp_get(value, 1, 20) != 0;
      g_gxState.stateDirty = true;
    }
    break;
  }

  // Scissor registers (0x20, 0x21)
  case 0x20: case 0x21: {
    Log.warn("Unimplemented: BP register {:x} (scissor)", regId);
    break;
  }

  // Line/point size (0x22)
  case 0x22: {
    Log.warn("Unimplemented: BP register {:x} (line/point size)", regId);
    break;
  }

  // Indirect texture scale (0x25, 0x26)
  case 0x25: {
    if (gx::MaxIndStages > 0) {
      g_gxState.indStages[0].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 0));
      g_gxState.indStages[0].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 4));
    }
    if (gx::MaxIndStages > 1) {
      g_gxState.indStages[1].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 8));
      g_gxState.indStages[1].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 12));
    }
    g_gxState.stateDirty = true;
    break;
  }
  case 0x26: {
    if (gx::MaxIndStages > 2) {
      g_gxState.indStages[2].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 0));
      g_gxState.indStages[2].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 4));
    }
    if (gx::MaxIndStages > 3) {
      g_gxState.indStages[3].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 8));
      g_gxState.indStages[3].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 12));
    }
    g_gxState.stateDirty = true;
    break;
  }

  // Indirect texture reference (0x27)
  case 0x27: {
    for (u32 i = 0; i < gx::MaxIndStages; i++) {
      g_gxState.indStages[i].texMapId = static_cast<GXTexMapID>(bp_get(value, 3, i * 6));
      g_gxState.indStages[i].texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, i * 6 + 3));
    }
    g_gxState.stateDirty = true;
    break;
  }

  // TEV order / tref (0x28-0x2F) - 2 stages per register
  case 0x28: case 0x29: case 0x2A: case 0x2B:
  case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
    u32 idx = regId - 0x28;
    u32 stage0 = idx * 2;
    u32 stage1 = idx * 2 + 1;

    // Channel ID reverse mapping from hardware to GX
    static const GXChannelID r2c[] = {GX_COLOR0A0, GX_COLOR1A1, GX_COLOR0A0, GX_COLOR1A1,
                                       GX_COLOR0A0, GX_COLOR_ZERO, GX_COLOR_ZERO, GX_COLOR_NULL};

    if (stage0 < gx::MaxTevStages) {
      auto& s = g_gxState.tevStages[stage0];
      s.texMapId = static_cast<GXTexMapID>(bp_get(value, 3, 0));
      s.texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, 3));
      // bit 6 = tex enable
      if (!bp_get(value, 1, 6)) {
        s.texMapId = GX_TEXMAP_NULL;
      }
      u32 chanHw = bp_get(value, 3, 7);
      s.channelId = (chanHw < 8) ? r2c[chanHw] : GX_COLOR_NULL;
    }
    if (stage1 < gx::MaxTevStages) {
      auto& s = g_gxState.tevStages[stage1];
      s.texMapId = static_cast<GXTexMapID>(bp_get(value, 3, 12));
      s.texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, 15));
      if (!bp_get(value, 1, 18)) {
        s.texMapId = GX_TEXMAP_NULL;
      }
      u32 chanHw = bp_get(value, 3, 19);
      s.channelId = (chanHw < 8) ? r2c[chanHw] : GX_COLOR_NULL;
    }
    g_gxState.stateDirty = true;
    break;
  }

  // Z mode (0x40)
  case 0x40: {
    g_gxState.depthCompare = bp_get(value, 1, 0) != 0;
    g_gxState.depthFunc = static_cast<GXCompare>(bp_get(value, 3, 1));
    g_gxState.depthUpdate = bp_get(value, 1, 4) != 0;
    g_gxState.stateDirty = true;
    break;
  }

  // Blend mode / cmode0 (0x41)
  case 0x41: {
    bool blendEn = bp_get(value, 1, 0) != 0;
    bool logicEn = bp_get(value, 1, 1) != 0;
    bool dither = bp_get(value, 1, 2) != 0;
    g_gxState.colorUpdate = bp_get(value, 1, 3) != 0;
    g_gxState.alphaUpdate = bp_get(value, 1, 4) != 0;
    g_gxState.blendFacDst = static_cast<GXBlendFactor>(bp_get(value, 3, 5));
    g_gxState.blendFacSrc = static_cast<GXBlendFactor>(bp_get(value, 3, 8));
    bool subtract = bp_get(value, 1, 11) != 0;
    g_gxState.blendOp = static_cast<GXLogicOp>(bp_get(value, 4, 12));

    if (subtract) {
      g_gxState.blendMode = GX_BM_SUBTRACT;
    } else if (blendEn) {
      g_gxState.blendMode = GX_BM_BLEND;
    } else if (logicEn) {
      g_gxState.blendMode = GX_BM_LOGIC;
    } else {
      g_gxState.blendMode = GX_BM_NONE;
    }
    g_gxState.stateDirty = true;
    break;
  }

  // Dst alpha / cmode1 (0x42)
  case 0x42: {
    u8 alpha = bp_get(value, 8, 0);
    bool enabled = bp_get(value, 1, 8) != 0;
    g_gxState.dstAlpha = enabled ? alpha : UINT32_MAX;
    break;
  }

  // PE control (0x43) - zcomp location
  case 0x43: {
    // Log.warn("Unimplemented: BP register {:x} (zcomp loc)", regId);
    break;
  }

  // Alpha compare (0xF3)
  case 0xF3: {
    g_gxState.alphaCompare.ref0 = bp_get(value, 8, 0);
    g_gxState.alphaCompare.ref1 = bp_get(value, 8, 8);
    g_gxState.alphaCompare.comp0 = static_cast<GXCompare>(bp_get(value, 3, 16));
    g_gxState.alphaCompare.comp1 = static_cast<GXCompare>(bp_get(value, 3, 19));
    g_gxState.alphaCompare.op = static_cast<GXAlphaOp>(bp_get(value, 2, 22));
    g_gxState.stateDirty = true;
    break;
  }

  // TEV K color/alpha select (0xF6-0xFD)
  case 0xF6: case 0xF7: case 0xF8: case 0xF9:
  case 0xFA: case 0xFB: case 0xFC: case 0xFD: {
    u32 kselIdx = regId - 0xF6;
    // Swap table entries (packed into pairs of ksel registers)
    if (kselIdx < gx::MaxTevSwap * 2) {
      u32 swapIdx = kselIdx / 2;
      if (kselIdx & 1) {
        g_gxState.tevSwapTable[swapIdx].blue = static_cast<GXTevColorChan>(bp_get(value, 2, 0));
        g_gxState.tevSwapTable[swapIdx].alpha = static_cast<GXTevColorChan>(bp_get(value, 2, 2));
      } else {
        g_gxState.tevSwapTable[swapIdx].red = static_cast<GXTevColorChan>(bp_get(value, 2, 0));
        g_gxState.tevSwapTable[swapIdx].green = static_cast<GXTevColorChan>(bp_get(value, 2, 2));
      }
    }
    // K color/alpha selection for 2 stages per register
    u32 stage0 = kselIdx * 2;
    u32 stage1 = kselIdx * 2 + 1;
    if (stage0 < gx::MaxTevStages) {
      g_gxState.tevStages[stage0].kcSel = static_cast<GXTevKColorSel>(bp_get(value, 5, 4));
      g_gxState.tevStages[stage0].kaSel = static_cast<GXTevKAlphaSel>(bp_get(value, 5, 9));
    }
    if (stage1 < gx::MaxTevStages) {
      g_gxState.tevStages[stage1].kcSel = static_cast<GXTevKColorSel>(bp_get(value, 5, 14));
      g_gxState.tevStages[stage1].kaSel = static_cast<GXTevKAlphaSel>(bp_get(value, 5, 19));
    }
    g_gxState.stateDirty = true;
    break;
  }

  // Fog A/B parameters (0xEE-0xF0)
  // FOG0 (0xEE): A parameter - sign(1)|exp(8)|mantissa(11) partial IEEE 754 float
  case 0xEE: {
    g_gxState.fog.fog0Raw = value;
    // Reconstruct A = a_encoded * 2^b_s
    u32 a_mant = bp_get(value, 11, 0);
    u32 a_exp = bp_get(value, 8, 11);
    u32 a_sign = bp_get(value, 1, 19);
    u32 a_bits = (a_sign << 31) | (a_exp << 23) | (a_mant << 12);
    float a_encoded;
    std::memcpy(&a_encoded, &a_bits, sizeof(a_encoded));
    u32 b_s = g_gxState.fog.fog2Raw & 0x1F;
    g_gxState.fog.a = std::ldexp(a_encoded, static_cast<int>(b_s));
    g_gxState.stateDirty = true;
    break;
  }
  // FOG1 (0xEF): B mantissa (24-bit)
  case 0xEF: {
    g_gxState.fog.fog1Raw = value;
    u32 b_m = bp_get(value, 24, 0);
    u32 b_s = g_gxState.fog.fog2Raw & 0x1F;
    float B_mant = static_cast<float>(b_m) / 8388638.0f;
    g_gxState.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
    g_gxState.stateDirty = true;
    break;
  }
  // FOG2 (0xF0): B shift/exponent (5-bit)
  case 0xF0: {
    g_gxState.fog.fog2Raw = value;
    u32 b_s = bp_get(value, 5, 0);
    // Recompute A with updated b_s
    u32 a_mant = bp_get(g_gxState.fog.fog0Raw, 11, 0);
    u32 a_exp = bp_get(g_gxState.fog.fog0Raw, 8, 11);
    u32 a_sign = bp_get(g_gxState.fog.fog0Raw, 1, 19);
    u32 a_bits = (a_sign << 31) | (a_exp << 23) | (a_mant << 12);
    float a_encoded;
    std::memcpy(&a_encoded, &a_bits, sizeof(a_encoded));
    g_gxState.fog.a = std::ldexp(a_encoded, static_cast<int>(b_s));
    // Recompute B with updated b_s
    u32 b_m = bp_get(g_gxState.fog.fog1Raw, 24, 0);
    float B_mant = static_cast<float>(b_m) / 8388638.0f;
    g_gxState.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
    g_gxState.stateDirty = true;
    break;
  }

  // Fog type + C parameter from FOG3 (0xF1)
  case 0xF1: {
    GXFogType fogType = static_cast<GXFogType>(bp_get(value, 3, 21));
    g_gxState.fog.type = fogType;
    // Decode C parameter (same partial float encoding as A)
    u32 c_mant = bp_get(value, 11, 0);
    u32 c_exp = bp_get(value, 8, 11);
    u32 c_sign = bp_get(value, 1, 19);
    u32 c_bits = (c_sign << 31) | (c_exp << 23) | (c_mant << 12);
    std::memcpy(&g_gxState.fog.c, &c_bits, sizeof(g_gxState.fog.c));
    g_gxState.stateDirty = true;
    break;
  }

  // Fog color from FOGCLR (0xF2)
  case 0xF2: {
    u8 b = bp_get(value, 8, 0);
    u8 g = bp_get(value, 8, 8);
    u8 r = bp_get(value, 8, 16);
    g_gxState.fog.color = {
        static_cast<float>(r) / 255.f,
        static_cast<float>(g) / 255.f,
        static_cast<float>(b) / 255.f,
        1.f,
    };
    g_gxState.stateDirty = true;
    break;
  }

  // TEV color registers / K color registers (0xE0-0xE7)
  // RA registers: 0xE0, 0xE2, 0xE4, 0xE6 (even)
  // BG registers: 0xE1, 0xE3, 0xE5, 0xE7 (odd)
  // Bit 23 distinguishes: 0 = TEV color register, 1 = K color register
  case 0xE0: case 0xE1: case 0xE2: case 0xE3:
  case 0xE4: case 0xE5: case 0xE6: case 0xE7: {
    u32 idx = (regId - 0xE0) / 2;
    bool isRA = (regId & 1) == 0;
    bool isKColor = bp_get(value, 1, 23) != 0;

    if (isKColor) {
      // K color register (8-bit components)
      if (idx < GX_MAX_KCOLOR) {
        auto& kc = g_gxState.kcolors[idx];
        if (isRA) {
          kc[0] = static_cast<float>(bp_get(value, 8, 0)) / 255.f;  // R
          kc[3] = static_cast<float>(bp_get(value, 8, 12)) / 255.f; // A
        } else {
          kc[2] = static_cast<float>(bp_get(value, 8, 0)) / 255.f;  // B
          kc[1] = static_cast<float>(bp_get(value, 8, 12)) / 255.f; // G
        }
        g_gxState.stateDirty = true;
      }
    } else {
      // TEV color register (11-bit signed components)
      if (idx < gx::MaxTevRegs) {
        auto& cr = g_gxState.colorRegs[idx];
        if (isRA) {
          // 11-bit signed: sign-extend from 11 bits
          s32 r = bp_get(value, 11, 0);
          if (r & 0x400) r |= ~0x7FF; // sign extend
          s32 a = bp_get(value, 11, 12);
          if (a & 0x400) a |= ~0x7FF;
          cr[0] = static_cast<float>(r) / 255.f;
          cr[3] = static_cast<float>(a) / 255.f;
        } else {
          s32 b = bp_get(value, 11, 0);
          if (b & 0x400) b |= ~0x7FF;
          s32 g = bp_get(value, 11, 12);
          if (g & 0x400) g |= ~0x7FF;
          cr[2] = static_cast<float>(b) / 255.f;
          cr[1] = static_cast<float>(g) / 255.f;
        }
        g_gxState.stateDirty = true;
      }
    }
    break;
  }

  // Indirect texture matrices (0x06-0x0E)
  // Each matrix uses 3 consecutive registers (one per row of the 3x2 matrix).
  // Matrix 0: 0x06-0x08, Matrix 1: 0x09-0x0B, Matrix 2: 0x0C-0x0E
  case 0x06: case 0x07: case 0x08:
  case 0x09: case 0x0A: case 0x0B:
  case 0x0C: case 0x0D: case 0x0E: {
    u32 idx = (regId - 0x06) / 3; // matrix index (0-2)
    u32 row = (regId - 0x06) % 3; // row index (0-2)
    auto& info = g_gxState.indTexMtxs[idx];

    // Decode 11-bit signed matrix elements (scaled by 1024)
    s32 col0 = bp_get(value, 11, 0);
    if (col0 & 0x400) col0 |= ~0x7FF; // sign-extend from 11 bits
    s32 col1 = bp_get(value, 11, 11);
    if (col1 & 0x400) col1 |= ~0x7FF;

    auto& r = row == 0 ? info.mtx.m0 : (row == 1 ? info.mtx.m1 : info.mtx.m2);
    r.x = static_cast<float>(col0) / 1024.0f;
    r.y = static_cast<float>(col1) / 1024.0f;

    // Accumulate 2-bit scale exponent part (adjScale = scaleExp + 17, split across 3 registers)
    u32 scaleBits = bp_get(value, 2, 22);
    u32 shift = row * 2;
    info.adjScaleRaw = (info.adjScaleRaw & ~(3u << shift)) | (scaleBits << shift);
    info.scaleExp = static_cast<s8>(info.adjScaleRaw) - 17;

    g_gxState.stateDirty = true;
    break;
  }

  // SU texture coordinate scale registers (0x30-0x3F)
  // Even registers (suTs0): S-axis scale, bias, cyl wrap, line/point offset
  // Odd registers (suTs1): T-axis scale, bias, cyl wrap
  case 0x30: case 0x31: case 0x32: case 0x33:
  case 0x34: case 0x35: case 0x36: case 0x37:
  case 0x38: case 0x39: case 0x3A: case 0x3B:
  case 0x3C: case 0x3D: case 0x3E: case 0x3F: {
    u32 coordIdx = (regId - 0x30) / 2;
    bool isT = (regId & 1) != 0;
    auto& tcs = g_gxState.texCoordScales[coordIdx];
    if (isT) {
      tcs.scaleT = static_cast<u16>(bp_get(value, 16, 0));
      tcs.biasT = bp_get(value, 1, 16) != 0;
      tcs.cylWrapT = bp_get(value, 1, 17) != 0;
    } else {
      tcs.scaleS = static_cast<u16>(bp_get(value, 16, 0));
      tcs.biasS = bp_get(value, 1, 16) != 0;
      tcs.cylWrapS = bp_get(value, 1, 17) != 0;
      tcs.lineOffset = bp_get(value, 1, 18) != 0;
      tcs.pointOffset = bp_get(value, 1, 19) != 0;
    }
    g_gxState.stateDirty = true;
    break;
  }

  // Copy clear color (0x4F-0x50) and depth (0x51)
  case 0x4F: {
    u8 r = bp_get(value, 8, 0);
    u8 a = bp_get(value, 8, 8);
    g_gxState.clearColor[0] = static_cast<float>(r) / 255.f;
    g_gxState.clearColor[3] = static_cast<float>(a) / 255.f;
    g_gxState.stateDirty = true;
    break;
  }
  case 0x50: {
    u8 b = bp_get(value, 8, 0);
    u8 g = bp_get(value, 8, 8);
    g_gxState.clearColor[2] = static_cast<float>(b) / 255.f;
    g_gxState.clearColor[1] = static_cast<float>(g) / 255.f;
    g_gxState.stateDirty = true;
    break;
  }
  case 0x51: {
    g_gxState.clearDepth = bp_get(value, 24, 0);
    g_gxState.stateDirty = true;
    break;
  }

  // Texture mode/image registers (0x80-0xBB) - texture config
  default:
    if (regId >= 0x80 && regId <= 0xBB) {
      // Texture format/wrap/filter configuration.
      // These are handled pragmatically - GXLoadTexObj sets texture handles directly.
    } else {
      Log.warn("Unhandled BP register 0x{:02X} (value 0x{:06X})", regId, value & 0xFFFFFF);
    }
    break;
  }
}

// CP register handler - decodes CP register writes and updates g_gxState
static void handle_cp(u8 addr, u32 value, bool bigEndian) {
  switch (addr) {
  // VCD low (0x50)
  case 0x50: {
    auto& vd = g_gxState.vtxDesc;
    vd[GX_VA_PNMTXIDX]   = static_cast<GXAttrType>(bp_get(value, 1, 0));
    vd[GX_VA_TEX0MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 1));
    vd[GX_VA_TEX1MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 2));
    vd[GX_VA_TEX2MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 3));
    vd[GX_VA_TEX3MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 4));
    vd[GX_VA_TEX4MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 5));
    vd[GX_VA_TEX5MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 6));
    vd[GX_VA_TEX6MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 7));
    vd[GX_VA_TEX7MTXIDX]  = static_cast<GXAttrType>(bp_get(value, 1, 8));
    vd[GX_VA_POS]          = static_cast<GXAttrType>(bp_get(value, 2, 9));
    vd[GX_VA_NRM]          = static_cast<GXAttrType>(bp_get(value, 2, 11));
    vd[GX_VA_CLR0]         = static_cast<GXAttrType>(bp_get(value, 2, 13));
    vd[GX_VA_CLR1]         = static_cast<GXAttrType>(bp_get(value, 2, 15));
    g_gxState.stateDirty = true;
    break;
  }

  // VCD high (0x60)
  case 0x60: {
    auto& vd = g_gxState.vtxDesc;
    vd[GX_VA_TEX0] = static_cast<GXAttrType>(bp_get(value, 2, 0));
    vd[GX_VA_TEX1] = static_cast<GXAttrType>(bp_get(value, 2, 2));
    vd[GX_VA_TEX2] = static_cast<GXAttrType>(bp_get(value, 2, 4));
    vd[GX_VA_TEX3] = static_cast<GXAttrType>(bp_get(value, 2, 6));
    vd[GX_VA_TEX4] = static_cast<GXAttrType>(bp_get(value, 2, 8));
    vd[GX_VA_TEX5] = static_cast<GXAttrType>(bp_get(value, 2, 10));
    vd[GX_VA_TEX6] = static_cast<GXAttrType>(bp_get(value, 2, 12));
    vd[GX_VA_TEX7] = static_cast<GXAttrType>(bp_get(value, 2, 14));
    g_gxState.stateDirty = true;
    break;
  }

  // Matrix index A (0x30)
  case 0x30: {
    g_gxState.currentPnMtx = bp_get(value, 6, 0) / 3;
    break;
  }

  // Matrix index B (0x40)
  case 0x40:
    // Texture matrix indices - used for multi-matrix texgen
    break;

  default:
    // VAT A registers (0x70-0x77)
    if (addr >= 0x70 && addr <= 0x77) {
      u32 fmt = addr - 0x70;
      auto& vf = g_gxState.vtxFmts[fmt];
      vf.attrs[GX_VA_POS].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 0));
      vf.attrs[GX_VA_POS].type = static_cast<GXCompType>(bp_get(value, 3, 1));
      vf.attrs[GX_VA_POS].frac = static_cast<u8>(bp_get(value, 5, 4));
      vf.attrs[GX_VA_NRM].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 9));
      vf.attrs[GX_VA_NRM].type = static_cast<GXCompType>(bp_get(value, 3, 10));
      vf.attrs[GX_VA_NRM].frac = 0;
      vf.attrs[GX_VA_CLR0].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 13));
      vf.attrs[GX_VA_CLR0].type = static_cast<GXCompType>(bp_get(value, 3, 14));
      vf.attrs[GX_VA_CLR0].frac = 0;
      vf.attrs[GX_VA_CLR1].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 17));
      vf.attrs[GX_VA_CLR1].type = static_cast<GXCompType>(bp_get(value, 3, 18));
      vf.attrs[GX_VA_CLR1].frac = 0;
      vf.attrs[GX_VA_TEX0].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 21));
      vf.attrs[GX_VA_TEX0].type = static_cast<GXCompType>(bp_get(value, 3, 22));
      vf.attrs[GX_VA_TEX0].frac = static_cast<u8>(bp_get(value, 5, 25));
      g_gxState.stateDirty = true;
    }
    // VAT B registers (0x80-0x87)
    else if (addr >= 0x80 && addr <= 0x87) {
      u32 fmt = addr - 0x80;
      auto& vf = g_gxState.vtxFmts[fmt];
      vf.attrs[GX_VA_TEX1].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 0));
      vf.attrs[GX_VA_TEX1].type = static_cast<GXCompType>(bp_get(value, 3, 1));
      vf.attrs[GX_VA_TEX1].frac = static_cast<u8>(bp_get(value, 5, 4));
      vf.attrs[GX_VA_TEX2].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 9));
      vf.attrs[GX_VA_TEX2].type = static_cast<GXCompType>(bp_get(value, 3, 10));
      vf.attrs[GX_VA_TEX2].frac = static_cast<u8>(bp_get(value, 5, 13));
      vf.attrs[GX_VA_TEX3].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 18));
      vf.attrs[GX_VA_TEX3].type = static_cast<GXCompType>(bp_get(value, 3, 19));
      vf.attrs[GX_VA_TEX3].frac = static_cast<u8>(bp_get(value, 5, 22));
      vf.attrs[GX_VA_TEX4].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 27));
      vf.attrs[GX_VA_TEX4].type = static_cast<GXCompType>(bp_get(value, 3, 28));
      // TEX4 frac is in VAT C
      g_gxState.stateDirty = true;
    }
    // VAT C registers (0x90-0x97)
    else if (addr >= 0x90 && addr <= 0x97) {
      u32 fmt = addr - 0x90;
      auto& vf = g_gxState.vtxFmts[fmt];
      vf.attrs[GX_VA_TEX4].frac = static_cast<u8>(bp_get(value, 5, 0));
      vf.attrs[GX_VA_TEX5].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 5));
      vf.attrs[GX_VA_TEX5].type = static_cast<GXCompType>(bp_get(value, 3, 6));
      vf.attrs[GX_VA_TEX5].frac = static_cast<u8>(bp_get(value, 5, 9));
      vf.attrs[GX_VA_TEX6].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 14));
      vf.attrs[GX_VA_TEX6].type = static_cast<GXCompType>(bp_get(value, 3, 15));
      vf.attrs[GX_VA_TEX6].frac = static_cast<u8>(bp_get(value, 5, 18));
      vf.attrs[GX_VA_TEX7].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 23));
      vf.attrs[GX_VA_TEX7].type = static_cast<GXCompType>(bp_get(value, 3, 24));
      vf.attrs[GX_VA_TEX7].frac = static_cast<u8>(bp_get(value, 5, 27));
      g_gxState.stateDirty = true;
    }
    // Array base addresses (0xA0-0xAF)
    else if (addr >= 0xA0 && addr <= 0xAF) {
      u32 attrIdx = addr - 0xA0 + GX_VA_POS;
      if (attrIdx < GX_VA_MAX_ATTR) {
        // On TARGET_PC, the array base pointer is set by GXSetArray's side-channel.
        // The FIFO value is a truncated 32-bit pointer (unusable on 64-bit hosts),
        // so we only invalidate the cached range here.
        g_gxState.arrays[attrIdx].cachedRange = {};
      }
    }
    // Array strides (0xB0-0xBF)
    else if (addr >= 0xB0 && addr <= 0xBF) {
      u32 attrIdx = addr - 0xB0 + GX_VA_POS;
      if (attrIdx < GX_VA_MAX_ATTR) {
        g_gxState.arrays[attrIdx].stride = static_cast<u8>(value);
        g_gxState.arrays[attrIdx].cachedRange = {};
      }
    }
    break;
  }
}

// XF register handler - decodes XF (transform unit) register writes and updates g_gxState
static void handle_xf(const u8* data, u32& pos, u32 size, bool bigEndian) {
  CHECK(pos + 4 <= size, "XF header read overrun");
  u32 header = read_u32(data + pos, bigEndian);
  pos += 4;

  u32 count = ((header >> 16) & 0xFFFF) + 1;
  u32 addr = header & 0xFFFF;
  u32 dataBytes = count * 4;
  // Log.warn("  xf: addr {:04x} count {} dataBytes {} pos {} -> {}", addr, count, dataBytes, pos, pos + dataBytes);
  CHECK(pos + dataBytes <= size, "XF data read overrun: need {} bytes at pos {}", dataBytes, pos);

  const u8* xfData = data + pos;

  // Position matrices (0x000-0x077) - 10 matrices, 4 rows each, 4 values per row = 12 floats
  if (addr < 0x078) {
    u32 mtxIdx = addr / 4;
    if (mtxIdx < gx::MaxPnMtx) {
      auto& mtx = g_gxState.pnMtx[mtxIdx].pos;
      u32 startOffset = addr - mtxIdx * 4;
      for (u32 i = 0; i < count && (startOffset + i) < 12; i++) {
        reinterpret_cast<f32*>(&mtx)[startOffset + i] = read_f32(xfData + i * 4, bigEndian);
      }
      g_gxState.stateDirty = true;
    }
  }
  // Texture matrices (0x078-0x0EF) - regular texture matrices
  // Address space: GX_TEXMTX0=30, each mtx takes 12 XF slots (3x4), id*4 = addr
  // So addr 0x078=TEXMTX0*4, up to 10 matrices
  else if (addr >= 0x078 && addr < 0x0F0) {
    u32 texBase = addr - 0x078;
    u32 mtxIdx = texBase / 12;
    u32 startOffset = texBase % 12;
    if (mtxIdx < gx::MaxTexMtx) {
      // Determine if 2x4 or 3x4 from count
      if (count <= 8 && startOffset == 0) {
        // 2x4 matrix
        aurora::Mat2x4<float> mtx{};
        f32* flat = reinterpret_cast<f32*>(&mtx);
        for (u32 i = 0; i < count && i < 8; i++) {
          flat[i] = read_f32(xfData + i * 4, bigEndian);
        }
        g_gxState.texMtxs[mtxIdx] = mtx;
      } else {
        // 3x4 matrix
        aurora::Mat3x4<float> mtx{};
        f32* flat = reinterpret_cast<f32*>(&mtx);
        for (u32 i = 0; i < count && (startOffset + i) < 12; i++) {
          flat[startOffset + i] = read_f32(xfData + i * 4, bigEndian);
        }
        g_gxState.texMtxs[mtxIdx] = mtx;
      }
      g_gxState.stateDirty = true;
    }
  }
  // Normal matrices (0x400-0x459)
  else if (addr >= 0x400 && addr < 0x45A) {
    u32 nrmBase = addr - 0x400;
    u32 mtxIdx = nrmBase / 3;
    if (mtxIdx < gx::MaxPnMtx) {
      auto& mtx = g_gxState.pnMtx[mtxIdx].nrm;
      // Normal matrices are 3x3 stored as 3 rows of 3 XF values,
      // but Mat3x4 has 3 rows of 4 floats. Map XF index to Mat3x4 flat index.
      u32 startOffset = nrmBase - mtxIdx * 3;
      f32* flat = reinterpret_cast<f32*>(&mtx);
      for (u32 i = 0; i < count; i++) {
        u32 xfIdx = startOffset + i;
        u32 row = xfIdx / 3;
        u32 col = xfIdx % 3;
        if (row < 3) {
          flat[row * 4 + col] = read_f32(xfData + i * 4, bigEndian);
        }
      }
      g_gxState.stateDirty = true;
    }
  }
  // Post-transform texture matrices (0x500-0x5EF)
  else if (addr >= 0x500 && addr < 0x5F0) {
    u32 ptBase = addr - 0x500;
    u32 mtxIdx = ptBase / 4; // Each PT matrix takes 4 XF slots (but stores 12 floats = 3x4)
    // Actually PT matrices: (id - GX_PTTEXMTX0) * 4 + 0x500, and they're always 3x4
    // GX_PTTEXMTX0=64, spacing=3, so mtxIdx = ptBase/4 maps to ptTexMtxs index
    if (mtxIdx < gx::MaxPTTexMtx) {
      u32 startOffset = ptBase - mtxIdx * 4;
      // PT matrices store 12 floats (3x4)
      f32* flat = reinterpret_cast<f32*>(&g_gxState.ptTexMtxs[mtxIdx]);
      for (u32 i = 0; i < count && (startOffset + i) < 12; i++) {
        flat[startOffset + i] = read_f32(xfData + i * 4, bigEndian);
      }
      g_gxState.stateDirty = true;
    }
  }
  // Lights (0x600-0x67F) - 8 lights, 16 values each
  else if (addr >= 0x600 && addr < 0x680) {
    u32 lightBase = addr - 0x600;
    u32 lightIdx = lightBase / 0x10;
    u32 offset = lightBase % 0x10;
    if (lightIdx < GX::MaxLights) {
      auto& light = g_gxState.lights[lightIdx];
      for (u32 i = 0; i < count && (offset + i) < 16; i++) {
        u32 field = offset + i;
        f32 val = read_f32(xfData + i * 4, bigEndian);
        u32 ival = read_u32(xfData + i * 4, bigEndian);
        switch (field) {
        case 3: // Color (packed u32)
          light.color = unpack_color(ival);
          break;
        case 4:
          light.cosAtt[0] = val;
          break; // a0
        case 5:
          light.cosAtt[1] = val;
          break; // a1
        case 6:
          light.cosAtt[2] = val;
          break; // a2
        case 7:
          light.distAtt[0] = val;
          break; // k0
        case 8:
          light.distAtt[1] = val;
          break; // k1
        case 9:
          light.distAtt[2] = val;
          break; // k2
        case 10:
          light.pos[0] = val;
          break; // px
        case 11:
          light.pos[1] = val;
          break; // py
        case 12:
          light.pos[2] = val;
          break; // pz
        case 13:
          light.dir[0] = val;
          break; // nx
        case 14:
          light.dir[1] = val;
          break; // ny
        case 15:
          light.dir[2] = val;
          break; // nz
        default:
          break; // padding (0-2)
        }
      }
      g_gxState.stateDirty = true;
    }
  }
  // XF registers (0x1000+)
  else if (addr >= 0x1000) {
    u32 xfAddr = addr - 0x1000;
    for (u32 i = 0; i < count; i++) {
      u32 reg = xfAddr + i;
      u32 val = read_u32(xfData + i * 4, bigEndian);
      f32 fval = read_f32(xfData + i * 4, bigEndian);

      switch (reg) {
      case 0x08:
        // XF vertex specs (numColors, numNormals, numTexCoords) - informational
        break;
      case 0x09:
        // numChans
        g_gxState.numChans = val;
        g_gxState.stateDirty = true;
        break;
      case 0x0A:
        // Ambient color 0
        g_gxState.colorChannelState[GX_COLOR0].ambColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA0].ambColor = unpack_color(val);
        g_gxState.stateDirty = true;
        break;
      case 0x0B:
        // Ambient color 1
        g_gxState.colorChannelState[GX_COLOR1].ambColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA1].ambColor = unpack_color(val);
        g_gxState.stateDirty = true;
        break;
      case 0x0C:
        // Material color 0
        g_gxState.colorChannelState[GX_COLOR0].matColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA0].matColor = unpack_color(val);
        g_gxState.stateDirty = true;
        break;
      case 0x0D:
        // Material color 1
        g_gxState.colorChannelState[GX_COLOR1].matColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA1].matColor = unpack_color(val);
        g_gxState.stateDirty = true;
        break;
      case 0x0E:
      case 0x0F:
      case 0x10:
      case 0x11: {
        // Channel control registers
        u32 chanId = reg - 0x0E;
        if (chanId < gx::MaxColorChannels) {
          auto& chan = g_gxState.colorChannelConfig[chanId];
          chan.matSrc = static_cast<GXColorSrc>(bp_get(val, 1, 0));
          chan.lightingEnabled = bp_get(val, 1, 1) != 0;
          u32 lightsLo = bp_get(val, 4, 2);
          chan.ambSrc = static_cast<GXColorSrc>(bp_get(val, 1, 6));
          chan.diffFn = static_cast<GXDiffuseFn>(bp_get(val, 2, 7));
          // Encoding: bit 9 = (attnFn != GX_AF_SPEC), bit 10 = (attnFn != GX_AF_NONE)
          bool bit9 = bp_get(val, 1, 9) != 0;
          bool bit10 = bp_get(val, 1, 10) != 0;
          u32 lightsHi = bp_get(val, 4, 11);
          if (!bit10) {
            chan.attnFn = GX_AF_NONE;
          } else if (!bit9) {
            chan.attnFn = GX_AF_SPEC;
          } else {
            chan.attnFn = GX_AF_SPOT;
          }
          u32 lightMask = lightsLo | (lightsHi << 4);
          g_gxState.colorChannelState[chanId].lightMask = GX::LightMask{lightMask};
          g_gxState.stateDirty = true;
        }
        break;
      }
      case 0x18:
        // Matrix index A -> current PN matrix
        g_gxState.currentPnMtx = bp_get(val, 6, 0) / 3;
        break;
      case 0x19:
        // Matrix index B
        break;
      case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: {
        // Viewport: sx, sy, sz, ox, oy, oz at XF 0x101A-0x101F
        // Decode back to left/top/width/height/nearZ/farZ
        // These are written as a bulk block starting at 0x1A
        u32 vpOff = reg - 0x1A;
        // Read all 6 viewport params if this is the start of the block
        if (vpOff == 0 && count >= 6) {
          f32 sx = read_f32(xfData + 0, bigEndian);
          f32 sy = read_f32(xfData + 4, bigEndian);
          f32 sz = read_f32(xfData + 8, bigEndian);
          f32 ox = read_f32(xfData + 12, bigEndian);
          f32 oy = read_f32(xfData + 16, bigEndian);
          f32 oz = read_f32(xfData + 20, bigEndian);
          // Reverse the encoding from GXSetViewport:
          // sx = width/2, sy = -height/2, ox = 340+left+width/2, oy = 340+top+height/2
          // sz = zmax-zmin, oz = zmax, zmax = 1.6777215e7*farZ, zmin = 1.6777215e7*nearZ
          f32 width = sx * 2.0f;
          f32 height = -sy * 2.0f;
          f32 left = ox - 340.0f - width / 2.0f;
          f32 top = oy - 340.0f - height / 2.0f;
          f32 farZ = oz / 1.6777215e7f;
          f32 nearZ = (oz - sz) / 1.6777215e7f;
          aurora::gfx::set_viewport(left, top, width, height, nearZ, farZ);
        }
        break;
      }
      case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: {
        // Projection: 6 params + type at XF 0x1020-0x1026
        u32 projOff = reg - 0x20;
        if (projOff == 0 && count >= 7) {
          f32 p0 = read_f32(xfData + 0, bigEndian);
          f32 p1 = read_f32(xfData + 4, bigEndian);
          f32 p2 = read_f32(xfData + 8, bigEndian);
          f32 p3 = read_f32(xfData + 12, bigEndian);
          f32 p4 = read_f32(xfData + 16, bigEndian);
          f32 p5 = read_f32(xfData + 20, bigEndian);
          u32 projType = read_u32(xfData + 24, bigEndian);
          g_gxState.projType = static_cast<GXProjectionType>(projType);
          // Reconstruct 4x4 projection matrix from 6 params
          auto& proj = g_gxState.proj;
          proj = {};
          proj.m0[0] = p0;
          proj.m1[1] = p2;
          proj.m2[2] = p4;
          proj.m2[3] = p5;
          if (projType == GX_ORTHOGRAPHIC) {
            proj.m0[3] = p1;
            proj.m1[3] = p3;
            proj.m3[3] = 1.0f;
          } else {
            proj.m0[2] = p1;
            proj.m1[2] = p3;
            proj.m3[2] = -1.0f;
          }
          g_gxState.stateDirty = true;
        }
        break;
      }
      case 0x3F:
        // numTexGens
        g_gxState.numTexGens = val;
        g_gxState.stateDirty = true;
        break;
      default:
        // TexGen config (0x40-0x4F) and post-transform (0x50-0x5F)
        if (reg >= 0x40 && reg <= 0x4F) {
          u32 tcIdx = reg - 0x40;
          if (tcIdx < gx::MaxTexCoord) {
            auto& tcg = g_gxState.tcgs[tcIdx];
            bool proj = bp_get(val, 1, 1) != 0;
            u32 form = bp_get(val, 1, 2);
            u32 tgType = bp_get(val, 3, 4);
            u32 srcRow = bp_get(val, 5, 7);

            if (tgType == 0) {
              tcg.type = proj ? GX_TG_MTX3x4 : GX_TG_MTX2x4;
            } else if (tgType == 1) {
              // Bump mapping
              tcg.type = static_cast<GXTexGenType>(bp_get(val, 3, 15) + 2);
            } else if (tgType == 2 || tgType == 3) {
              tcg.type = GX_TG_SRTG;
            }

            // Decode source from row
            static const GXTexGenSrc rowToSrc[] = {GX_TG_POS,    GX_TG_NRM,    GX_TG_COLOR0,
                                                    GX_TG_BINRM,  GX_TG_TANGENT, GX_TG_TEX0,
                                                    GX_TG_TEX1,   GX_TG_TEX2,   GX_TG_TEX3,
                                                    GX_TG_TEX4,   GX_TG_TEX5,   GX_TG_TEX6,
                                                    GX_TG_TEX7};
            if (srcRow < 13) {
              tcg.src = rowToSrc[srcRow];
            }
            g_gxState.stateDirty = true;
          }
        } else if (reg >= 0x50 && reg <= 0x5F) {
          u32 tcIdx = reg - 0x50;
          if (tcIdx < gx::MaxTexCoord) {
            g_gxState.tcgs[tcIdx].postMtx = static_cast<GXPTTexMtx>(bp_get(val, 6, 0) + 64);
            g_gxState.tcgs[tcIdx].normalize = bp_get(val, 1, 8) != 0;
            g_gxState.stateDirty = true;
          }
        } else {
          Log.warn("Unhandled XF register 0x{:04X} (value 0x{:08X})", reg, val);
        }
        break;
      }
    }
  }

  pos += dataBytes;
}

// Draw command handler - parses vertices inline and caches results
static void handle_draw(u8 cmd, const u8* data, u32& pos, u32 size, bool bigEndian) {
  using namespace aurora::gfx;
  using namespace aurora::gfx::gx;

  u8 opcode = cmd & CP_OPCODE_MASK;
  GXVtxFmt fmt = static_cast<GXVtxFmt>(cmd & CP_VAT_MASK);
  GXPrimitive prim = static_cast<GXPrimitive>(opcode);

  CHECK(pos + 2 <= size, "draw vtxCount read overrun");
  u16 vtxCount = read_u16(data + pos, bigEndian);
  pos += 2;

  // Temporarily save the vertex data start position
  const u8* vtxDataStart = data + pos;

  u32 vtxSize = prepare_vtx_buffer(nullptr, fmt, nullptr, vtxCount, bigEndian);
  u32 totalVtxBytes = vtxCount * vtxSize;
  // Log.warn("  draw: prim {:02x} fmt {} vtxCount {} vtxSize {} totalBytes {} pos {} -> {}",
  //          static_cast<u32>(prim), static_cast<u32>(fmt), vtxCount, vtxSize, totalVtxBytes, pos, pos + totalVtxBytes);
  if (pos + totalVtxBytes > size) {
    // Hex dump around the draw command for debugging
    u32 cmdPos = pos - 2 - 1; // opcode byte position (before vtxCount and pos++)
    u32 dumpStart = (cmdPos > 16) ? cmdPos - 16 : 0;
    u32 dumpEnd = (cmdPos + 32 < size) ? cmdPos + 32 : size;
    std::string hex;
    for (u32 i = dumpStart; i < dumpEnd; i++) {
      if (i == cmdPos)
        hex += fmt::format("[{:02x}]", data[i]);
      else
        hex += fmt::format(" {:02x}", data[i]);
    }
    Log.error("  hex dump around draw cmd (pos {}-{}):{}",dumpStart, dumpEnd - 1, hex);
    FATAL("draw vertex data overrun: need {} bytes at pos {}, have {}", totalVtxBytes, pos, size);
  }

  // Hash over the raw command data: cmd byte + vtxCount (2 bytes) + vertex data
  const u8* hashStart = data + pos - 3; // cmd byte + vtxCount + vtxData
  u32 hashSize = 3 + totalVtxBytes;
  const auto hash = xxh3_hash_s(hashStart, hashSize, 0);

  Range vertRange, idxRange;
  u32 numIndices = 0;

  auto it = sCachedDisplayLists.find(hash);
  if (it != sCachedDisplayLists.end()) {
    // Cache hit - use cached buffers
    const auto& cache = it->second;
    numIndices = cache.idxBuf.size() / 2;
    vertRange = push_verts(cache.vtxBuf.data(), cache.vtxBuf.size());
    idxRange = push_indices(cache.idxBuf.data(), cache.idxBuf.size());
  } else {
    // Cache miss - build index buffer and cache both
    ByteBuffer vtxBuf;
    ByteBuffer idxBuf;
    prepare_vtx_buffer(&vtxBuf, fmt, vtxDataStart, vtxCount, bigEndian);
    numIndices = prepare_idx_buffer(idxBuf, prim, 0, vtxCount);
    vertRange = push_verts(vtxBuf.data(), vtxBuf.size());
    idxRange = push_indices(idxBuf.data(), idxBuf.size());
    sCachedDisplayLists.try_emplace(hash, std::move(vtxBuf), std::move(idxBuf), fmt);
  }

  // Build pipeline, bind groups, and push draw command
  BindGroupRanges ranges{};
  for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
    if (g_gxState.vtxDesc[i] != GX_INDEX8 && g_gxState.vtxDesc[i] != GX_INDEX16) {
      continue;
    }
    auto& array = g_gxState.arrays[i];
    if (array.cachedRange.size > 0) {
      ranges.vaRanges[i] = array.cachedRange;
    } else {
      const auto range = push_storage(static_cast<const uint8_t*>(array.data), array.size);
      ranges.vaRanges[i] = range;
      array.cachedRange = range;
    }
  }

  model::PipelineConfig config{};
  populate_pipeline_config(config, GX_TRIANGLES, fmt);
  const auto info = build_shader_info(config.shaderConfig);
  const auto bindGroups = build_bind_groups(info, config.shaderConfig, ranges);
  const auto pipeline = pipeline_ref(config);

  push_draw_command(model::DrawData{
      .pipeline = pipeline,
      .vertRange = vertRange,
      .idxRange = idxRange,
      .dataRanges = ranges,
      .uniformRange = build_uniform(info),
      .indexCount = numIndices,
      .bindGroups = bindGroups,
      .dstAlpha = g_gxState.dstAlpha,
  });

  pos += totalVtxBytes;
}

} // namespace aurora::gfx::command_processor

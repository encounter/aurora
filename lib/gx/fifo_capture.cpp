#include "fifo_capture.hpp"

#include "fifo.hpp"
#include "gx.hpp"
#include "../gfx/texture.hpp"
#include "../internal.hpp"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXCommandList.h>
#include <dolphin/gx/GXTexture.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace aurora {
extern AuroraConfig g_config;
}

namespace aurora::gx::fifo_capture {
namespace {
Module Log("aurora::gx::fifo_capture");

constexpr u32 fourcc(char a, char b, char c, char d) {
  return static_cast<u32>(static_cast<u8>(a)) | static_cast<u32>(static_cast<u8>(b)) << 8 |
         static_cast<u32>(static_cast<u8>(c)) << 16 | static_cast<u32>(static_cast<u8>(d)) << 24;
}

constexpr u32 kMagic = fourcc('A', 'C', 'A', 'P');
constexpr u32 kChunkFifo = fourcc('F', 'I', 'F', 'O');
constexpr u32 kChunkResources = fourcc('R', 'T', 'B', 'L');

std::atomic_bool s_captureRequested = false;

struct Writer {
  std::vector<u8> data;

  void u8_(u8 value) { data.push_back(value); }

  void be16(u16 value) {
    data.push_back(static_cast<u8>(value >> 8));
    data.push_back(static_cast<u8>(value));
  }

  void be32(u32 value) {
    data.push_back(static_cast<u8>(value >> 24));
    data.push_back(static_cast<u8>(value >> 16));
    data.push_back(static_cast<u8>(value >> 8));
    data.push_back(static_cast<u8>(value));
  }

  void be64(u64 value) {
    be32(static_cast<u32>(value >> 32));
    be32(static_cast<u32>(value));
  }

  void f32(float value) {
    u32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    be32(bits);
  }

  void bytes(const void* ptr, size_t size) {
    const auto* first = static_cast<const u8*>(ptr);
    data.insert(data.end(), first, first + size);
  }

  void cp(u8 addr, u32 value) {
    u8_(GX_LOAD_CP_REG);
    u8_(addr);
    be32(value);
  }

  void bp(u32 value) {
    u8_(GX_LOAD_BP_REG);
    be32(value);
  }

  void xf(u16 addr, const float* values, u16 count) {
    u8_(GX_LOAD_XF_REG);
    be32(static_cast<u32>(addr) | (static_cast<u32>(count - 1) << 16));
    for (u16 i = 0; i < count; ++i) {
      f32(values[i]);
    }
  }

  void xf_u32(u16 addr, const u32* values, u16 count) {
    u8_(GX_LOAD_XF_REG);
    be32(static_cast<u32>(addr) | (static_cast<u32>(count - 1) << 16));
    for (u16 i = 0; i < count; ++i) {
      be32(values[i]);
    }
  }

  void aurora(u16 cmd) {
    u8_(GX_AURORA);
    be16(cmd);
  }
};

void append_le32(std::vector<u8>& out, u32 value) {
  out.push_back(static_cast<u8>(value));
  out.push_back(static_cast<u8>(value >> 8));
  out.push_back(static_cast<u8>(value >> 16));
  out.push_back(static_cast<u8>(value >> 24));
}

void append_le64(std::vector<u8>& out, u64 value) {
  append_le32(out, static_cast<u32>(value));
  append_le32(out, static_cast<u32>(value >> 32));
}

u16 read_be16(const u8* data) { return static_cast<u16>(data[0]) << 8 | data[1]; }

u32 read_be32(const u8* data) {
  return static_cast<u32>(data[0]) << 24 | static_cast<u32>(data[1]) << 16 | static_cast<u32>(data[2]) << 8 | data[3];
}

u64 read_be64(const u8* data) { return static_cast<u64>(read_be32(data)) << 32 | read_be32(data + 4); }

u32 get_bits(u32 value, u32 size, u32 shift) { return value >> shift & ((1u << size) - 1); }

void set_bits(u32& reg, u32 size, u32 shift, u32 value) {
  const u32 mask = ((1u << size) - 1) << shift;
  reg = reg & ~mask | (value << shift & mask);
}

void encode_vcd(const GXState& state, Writer& out) {
  u32 lo = 0;
  u32 hi = 0;
  const auto& vd = state.vtxDesc;

  set_bits(lo, 1, 0, vd[GX_VA_PNMTXIDX] != GX_NONE);
  set_bits(lo, 1, 1, vd[GX_VA_TEX0MTXIDX] != GX_NONE);
  set_bits(lo, 1, 2, vd[GX_VA_TEX1MTXIDX] != GX_NONE);
  set_bits(lo, 1, 3, vd[GX_VA_TEX2MTXIDX] != GX_NONE);
  set_bits(lo, 1, 4, vd[GX_VA_TEX3MTXIDX] != GX_NONE);
  set_bits(lo, 1, 5, vd[GX_VA_TEX4MTXIDX] != GX_NONE);
  set_bits(lo, 1, 6, vd[GX_VA_TEX5MTXIDX] != GX_NONE);
  set_bits(lo, 1, 7, vd[GX_VA_TEX6MTXIDX] != GX_NONE);
  set_bits(lo, 1, 8, vd[GX_VA_TEX7MTXIDX] != GX_NONE);
  set_bits(lo, 2, 9, vd[GX_VA_POS]);
  set_bits(lo, 2, 11, vd[GX_VA_NRM]);
  set_bits(lo, 2, 13, vd[GX_VA_CLR0]);
  set_bits(lo, 2, 15, vd[GX_VA_CLR1]);

  for (u32 i = 0; i < 8; ++i) {
    set_bits(hi, 2, i * 2, vd[GX_VA_TEX0 + i]);
  }

  out.cp(0x50, lo);
  out.cp(0x60, hi);
}

void encode_vat_attr(u32& va, u32& vb, u32& vc, GXAttr attr, const VtxAttrFmt& fmt) {
  switch (attr) {
  case GX_VA_POS:
    set_bits(va, 1, 0, fmt.cnt);
    set_bits(va, 3, 1, fmt.type);
    set_bits(va, 5, 4, fmt.frac);
    break;
  case GX_VA_NRM:
  case GX_VA_NBT:
    set_bits(va, 3, 10, fmt.type);
    if (fmt.cnt == GX_NRM_NBT3) {
      set_bits(va, 1, 9, 1);
      set_bits(va, 1, 31, 1);
    } else {
      set_bits(va, 1, 9, fmt.cnt);
      set_bits(va, 1, 31, 0);
    }
    break;
  case GX_VA_CLR0:
    set_bits(va, 1, 13, fmt.cnt);
    set_bits(va, 3, 14, fmt.type);
    break;
  case GX_VA_CLR1:
    set_bits(va, 1, 17, fmt.cnt);
    set_bits(va, 3, 18, fmt.type);
    break;
  case GX_VA_TEX0:
    set_bits(va, 1, 21, fmt.cnt);
    set_bits(va, 3, 22, fmt.type);
    set_bits(va, 5, 25, fmt.frac);
    break;
  case GX_VA_TEX1:
    set_bits(vb, 1, 0, fmt.cnt);
    set_bits(vb, 3, 1, fmt.type);
    set_bits(vb, 5, 4, fmt.frac);
    break;
  case GX_VA_TEX2:
    set_bits(vb, 1, 9, fmt.cnt);
    set_bits(vb, 3, 10, fmt.type);
    set_bits(vb, 5, 13, fmt.frac);
    break;
  case GX_VA_TEX3:
    set_bits(vb, 1, 18, fmt.cnt);
    set_bits(vb, 3, 19, fmt.type);
    set_bits(vb, 5, 22, fmt.frac);
    break;
  case GX_VA_TEX4:
    set_bits(vb, 1, 27, fmt.cnt);
    set_bits(vb, 3, 28, fmt.type);
    set_bits(vc, 5, 0, fmt.frac);
    break;
  case GX_VA_TEX5:
    set_bits(vc, 1, 5, fmt.cnt);
    set_bits(vc, 3, 6, fmt.type);
    set_bits(vc, 5, 9, fmt.frac);
    break;
  case GX_VA_TEX6:
    set_bits(vc, 1, 14, fmt.cnt);
    set_bits(vc, 3, 15, fmt.type);
    set_bits(vc, 5, 18, fmt.frac);
    break;
  case GX_VA_TEX7:
    set_bits(vc, 1, 23, fmt.cnt);
    set_bits(vc, 3, 24, fmt.type);
    set_bits(vc, 5, 27, fmt.frac);
    break;
  default:
    break;
  }
}

void encode_vat(const GXState& state, Writer& out) {
  for (u32 fmt = 0; fmt < GX_MAX_VTXFMT; ++fmt) {
    u32 va = 0;
    u32 vb = 0;
    u32 vc = 0;
    const auto& vtxFmt = state.vtxFmts[fmt];
    for (u32 attr = GX_VA_POS; attr <= GX_VA_TEX7; ++attr) {
      encode_vat_attr(va, vb, vc, static_cast<GXAttr>(attr), vtxFmt.attrs[attr]);
    }
    out.cp(static_cast<u8>(0x70 + fmt), va);
    out.cp(static_cast<u8>(0x80 + fmt), vb);
    out.cp(static_cast<u8>(0x90 + fmt), vc);
  }
}

void encode_arrays(const GXState& state, Writer& out) {
  for (u32 attr = GX_VA_POS; attr <= GX_VA_TEX7; ++attr) {
    const auto& array = state.arrays[attr];
    if (array.data != nullptr && array.size != 0) {
      out.aurora(static_cast<u16>(GX_AURORA_LOAD_ARRAYBASE | (attr - GX_VA_POS)));
      out.be64(reinterpret_cast<uintptr_t>(array.data));
      out.be32(array.size);
      out.u8_(array.le ? 1 : 0);
    }
    out.cp(static_cast<u8>(GX_CP_REG_ARRAYSTRIDE | (attr - GX_VA_POS)), array.stride);
  }
}

void encode_textures(const GXState& state, Writer& out) {
  for (u32 i = 0; i < MaxTextures; ++i) {
    const auto& obj = state.loadedTextures[i];
    if (obj.data == nullptr || obj.width() == 0 || obj.height() == 0) {
      continue;
    }
    out.aurora(GX_AURORA_LOAD_TEXOBJ);
    out.u8_(static_cast<u8>(i));
    out.be64(reinterpret_cast<uintptr_t>(obj.data));
    out.be32(obj.width());
    out.be32(obj.height());
    out.be32(obj.format());
    out.be32(static_cast<u32>(obj.tlut));
    out.u8_(obj.has_mips() ? 1 : 0);
    out.be32(obj.texObjId);
    out.be32(obj.texDataVersion);
  }

  for (u32 i = 0; i < MaxTluts; ++i) {
    const auto& obj = state.loadedTluts[i];
    if (obj.data == nullptr || obj.numEntries == 0) {
      continue;
    }
    out.aurora(GX_AURORA_LOAD_TLUT);
    out.u8_(static_cast<u8>(i));
    out.be64(reinterpret_cast<uintptr_t>(obj.data));
    out.be32(static_cast<u32>(obj.format));
    out.be16(obj.numEntries);
    out.be32(obj.tlutObjId);
    out.be32(obj.tlutDataVersion);
  }
}

void encode_view_state(const GXState& state, Writer& out) {
  out.aurora(GX_AURORA_LOAD_VIEWPORT_RENDER);
  out.f32(state.renderViewport.left);
  out.f32(state.renderViewport.top);
  out.f32(state.renderViewport.width);
  out.f32(state.renderViewport.height);
  out.f32(state.renderViewport.znear);
  out.f32(state.renderViewport.zfar);

  out.aurora(GX_AURORA_LOAD_SCISSOR_RENDER);
  out.be32(static_cast<u32>(state.renderScissor.x));
  out.be32(static_cast<u32>(state.renderScissor.y));
  out.be32(static_cast<u32>(state.renderScissor.width));
  out.be32(static_cast<u32>(state.renderScissor.height));

  const float width = state.logicalViewport.width;
  const float height = state.logicalViewport.height;
  const float sx = width * 0.5f;
  const float sy = -height * 0.5f;
  const float nearZ = state.logicalViewport.znear;
  const float farZ = state.logicalViewport.zfar;
  const float sz = 1.6777215e7f * (farZ - nearZ);
  const float oz = 1.6777215e7f * farZ;
  const std::array viewport{
      sx,
      sy,
      sz,
      state.logicalViewport.left + 340.0f + width * 0.5f,
      state.logicalViewport.top + 340.0f + height * 0.5f,
      oz,
  };
  out.xf(0x101A, viewport.data(), static_cast<u16>(viewport.size()));
}

void encode_copy_state(const GXState& state, Writer& out) {
  out.aurora(GX_AURORA_LOAD_COPY_SRC);
  out.be32(static_cast<u32>(state.texCopySrc.x));
  out.be32(static_cast<u32>(state.texCopySrc.y));
  out.be32(static_cast<u32>(state.texCopySrc.width));
  out.be32(static_cast<u32>(state.texCopySrc.height));

  out.aurora(GX_AURORA_LOAD_COPY_DST);
  out.be32(state.texCopyDstWidth);
  out.be32(state.texCopyDstHeight);
  out.be32(static_cast<u32>(state.texCopyFmt));
  out.u8_(state.texCopyDstWide ? 1 : 0);

  if (state.texCopyDest != nullptr) {
    out.aurora(GX_AURORA_LOAD_COPY_DEST);
    out.be64(reinterpret_cast<uintptr_t>(state.texCopyDest));
  }
}

void encode_xf_state(const GXState& state, Writer& out) {
  out.cp(0x30, state.currentPnMtx * 3);

  for (u32 i = 0; i < MaxPnMtx; ++i) {
    const auto* pos = reinterpret_cast<const float*>(&state.pnMtx[i].pos);
    out.xf(static_cast<u16>(i * 12), pos, 12);

    const auto& nrm = state.pnMtx[i].nrm;
    const std::array nrm3x3{
        nrm.m0[0], nrm.m0[1], nrm.m0[2], nrm.m1[0], nrm.m1[1],
        nrm.m1[2], nrm.m2[0], nrm.m2[1], nrm.m2[2],
    };
    out.xf(static_cast<u16>(0x400 + i * 9), nrm3x3.data(), static_cast<u16>(nrm3x3.size()));
  }

  for (u32 i = 0; i < MaxTexMtx; ++i) {
    const auto* mtx = reinterpret_cast<const float*>(&state.texMtxs[i]);
    out.xf(static_cast<u16>(0x078 + i * 12), mtx, 12);
  }

  for (u32 i = 0; i < MaxPTTexMtx; ++i) {
    const auto* mtx = reinterpret_cast<const float*>(&state.ptTexMtxs[i]);
    out.xf(static_cast<u16>(0x500 + i * 12), mtx, 12);
  }

  for (u32 i = 0; i < GX::MaxLights; ++i) {
    const auto& light = state.lights[i];
    std::array<u32, 16> raw{};
    const auto pack_color = [](const Vec4<float>& color) {
      const auto pack = [](float value) {
        return static_cast<u32>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f) & 0xFF;
      };
      return pack(color[0]) << 24 | pack(color[1]) << 16 | pack(color[2]) << 8 | pack(color[3]);
    };
    const auto float_bits = [](float value) {
      u32 bits;
      std::memcpy(&bits, &value, sizeof(bits));
      return bits;
    };
    raw[3] = pack_color(light.color);
    for (u32 c = 0; c < 3; ++c) {
      raw[4 + c] = float_bits(light.cosAtt[c]);
      raw[7 + c] = float_bits(light.distAtt[c]);
      raw[10 + c] = float_bits(light.pos[c]);
      raw[13 + c] = float_bits(light.dir[c]);
    }
    out.xf_u32(static_cast<u16>(0x600 + i * 0x10), raw.data(), static_cast<u16>(raw.size()));
  }

  const auto& proj = state.proj;
  std::array<float, 6> projParams{};
  projParams[0] = proj.m0[0];
  projParams[2] = proj.m1[1];
  projParams[4] = proj.m2[2];
  projParams[5] = proj.m2[3];
  if (state.projType == GX_ORTHOGRAPHIC) {
    projParams[1] = proj.m0[3];
    projParams[3] = proj.m1[3];
  } else {
    projParams[1] = proj.m0[2];
    projParams[3] = proj.m1[2];
  }
  out.u8_(GX_LOAD_XF_REG);
  out.be32(0x00061020);
  for (const float value : projParams) {
    out.f32(value);
  }
  out.be32(static_cast<u32>(state.projType));
}

std::vector<u8> build_state_prefix(const GXState& state) {
  Writer out;
  out.data.reserve(16 * 1024);

  encode_vcd(state, out);
  encode_vat(state, out);
  encode_arrays(state, out);

  for (u32 reg = 0; reg < state.bpRegCache.size(); ++reg) {
    if (reg == 0xFE || reg == 0x52) {
      continue;
    }
    out.bp(reg << 24 | (state.bpRegCache[reg] & 0x00FFFFFF));
  }

  encode_view_state(state, out);
  encode_copy_state(state, out);
  encode_xf_state(state, out);
  encode_textures(state, out);

  out.u8_(GX_NOP);
  out.u8_(GX_NOP);
  out.u8_(GX_NOP);
  return std::move(out.data);
}

struct Resource {
  u64 pointer = 0;
  u32 size = 0;
  std::vector<u8> data;
};

void add_resource(std::vector<Resource>& resources, u64 pointer, u32 size) {
  if (pointer == 0 || size == 0) {
    return;
  }
  for (const auto& resource : resources) {
    if (resource.pointer == pointer && resource.size == size) {
      return;
    }
  }

  Resource resource{
      .pointer = pointer,
      .size = size,
  };
  const auto* bytes = reinterpret_cast<const u8*>(static_cast<uintptr_t>(pointer));
  resource.data.assign(bytes, bytes + size);
  resources.emplace_back(std::move(resource));
}

struct ScanState {
  std::array<GXAttrType, MaxVtxAttr> vtxDesc = g_gxState.vtxDesc;
  std::array<VtxFmt, MaxVtxFmt> vtxFmts = g_gxState.vtxFmts;
  std::array<u32, MaxTextures> texMode1{};
  u64 copyDstKey = reinterpret_cast<uintptr_t>(g_gxState.texCopyDest);
  std::vector<u64> copyTextureKeys;
};

void record_copy_texture_key(ScanState& state, u64 pointer) {
  if (pointer == 0) {
    return;
  }
  if (std::find(state.copyTextureKeys.begin(), state.copyTextureKeys.end(), pointer) !=
      state.copyTextureKeys.end()) {
    return;
  }
  state.copyTextureKeys.emplace_back(pointer);
}

bool is_copy_texture_key(const ScanState& state, u64 pointer) {
  if (pointer == 0) {
    return false;
  }
  if (std::find(state.copyTextureKeys.begin(), state.copyTextureKeys.end(), pointer) !=
      state.copyTextureKeys.end()) {
    return true;
  }
  return g_gxState.copyTextures.contains(reinterpret_cast<const void*>(static_cast<uintptr_t>(pointer)));
}

void scan_vcd(ScanState& state, u8 addr, u32 value) {
  auto& vd = state.vtxDesc;
  if (addr == 0x50) {
    vd[GX_VA_PNMTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 0));
    vd[GX_VA_TEX0MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 1));
    vd[GX_VA_TEX1MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 2));
    vd[GX_VA_TEX2MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 3));
    vd[GX_VA_TEX3MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 4));
    vd[GX_VA_TEX4MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 5));
    vd[GX_VA_TEX5MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 6));
    vd[GX_VA_TEX6MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 7));
    vd[GX_VA_TEX7MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 8));
    vd[GX_VA_POS] = static_cast<GXAttrType>(get_bits(value, 2, 9));
    vd[GX_VA_NRM] = static_cast<GXAttrType>(get_bits(value, 2, 11));
    vd[GX_VA_CLR0] = static_cast<GXAttrType>(get_bits(value, 2, 13));
    vd[GX_VA_CLR1] = static_cast<GXAttrType>(get_bits(value, 2, 15));
  } else if (addr == 0x60) {
    for (u32 i = 0; i < 8; ++i) {
      vd[GX_VA_TEX0 + i] = static_cast<GXAttrType>(get_bits(value, 2, i * 2));
    }
  }
}

void scan_vat(ScanState& state, u8 addr, u32 value) {
  if (addr >= 0x70 && addr <= 0x77) {
    const u32 fmt = addr - 0x70;
    auto& vf = state.vtxFmts[fmt];
    vf.attrs[GX_VA_POS].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 0));
    vf.attrs[GX_VA_POS].type = static_cast<GXCompType>(get_bits(value, 3, 1));
    vf.attrs[GX_VA_POS].frac = static_cast<u8>(get_bits(value, 5, 4));
    vf.attrs[GX_VA_NRM].cnt = get_bits(value, 1, 31) ? GX_NRM_NBT3
                                                     : static_cast<GXCompCnt>(get_bits(value, 1, 9));
    vf.attrs[GX_VA_NRM].type = static_cast<GXCompType>(get_bits(value, 3, 10));
    vf.attrs[GX_VA_CLR0].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 13));
    vf.attrs[GX_VA_CLR0].type = static_cast<GXCompType>(get_bits(value, 3, 14));
    vf.attrs[GX_VA_CLR1].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 17));
    vf.attrs[GX_VA_CLR1].type = static_cast<GXCompType>(get_bits(value, 3, 18));
    vf.attrs[GX_VA_TEX0].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 21));
    vf.attrs[GX_VA_TEX0].type = static_cast<GXCompType>(get_bits(value, 3, 22));
    vf.attrs[GX_VA_TEX0].frac = static_cast<u8>(get_bits(value, 5, 25));
  } else if (addr >= 0x80 && addr <= 0x87) {
    const u32 fmt = addr - 0x80;
    auto& vf = state.vtxFmts[fmt];
    for (u32 attr = GX_VA_TEX1; attr <= GX_VA_TEX4; ++attr) {
      const u32 base = (attr - GX_VA_TEX1) * 9;
      vf.attrs[attr].cnt = static_cast<GXCompCnt>(get_bits(value, 1, base));
      vf.attrs[attr].type = static_cast<GXCompType>(get_bits(value, 3, base + 1));
      if (attr != GX_VA_TEX4) {
        vf.attrs[attr].frac = static_cast<u8>(get_bits(value, 5, base + 4));
      }
    }
  } else if (addr >= 0x90 && addr <= 0x97) {
    const u32 fmt = addr - 0x90;
    auto& vf = state.vtxFmts[fmt];
    vf.attrs[GX_VA_TEX4].frac = static_cast<u8>(get_bits(value, 5, 0));
    for (u32 attr = GX_VA_TEX5; attr <= GX_VA_TEX7; ++attr) {
      const u32 base = 5 + (attr - GX_VA_TEX5) * 9;
      vf.attrs[attr].cnt = static_cast<GXCompCnt>(get_bits(value, 1, base));
      vf.attrs[attr].type = static_cast<GXCompType>(get_bits(value, 3, base + 1));
      vf.attrs[attr].frac = static_cast<u8>(get_bits(value, 5, base + 4));
    }
  }
}

void scan_bp(ScanState& state, u32 value) {
  static constexpr std::array mode1Ids{0x84u, 0x85u, 0x86u, 0x87u, 0xA4u, 0xA5u, 0xA6u, 0xA7u};
  const u32 regId = value >> 24 & 0xFF;
  if (regId == 0x52) {
    const bool displayCopy = get_bits(value, 1, 14) != 0;
    if (!displayCopy) {
      record_copy_texture_key(state, state.copyDstKey);
    }
    return;
  }
  for (u32 i = 0; i < mode1Ids.size(); ++i) {
    if (regId == mode1Ids[i]) {
      state.texMode1[i] = value & 0x00FFFFFF;
      return;
    }
  }
}

u32 scan_vtx_size(const ScanState& state, GXVtxFmt fmt) {
  u32 size = 0;
  const auto& vtxFmt = state.vtxFmts[fmt];
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    switch (state.vtxDesc[i]) {
    case GX_NONE:
      break;
    case GX_DIRECT: {
      if (i >= GX_VA_PNMTXIDX && i <= GX_VA_TEX7MTXIDX) {
        size += 1;
        break;
      }
      const auto attr = static_cast<GXAttr>(i);
      const auto& attrFmt = vtxFmt.attrs[i];
      size += comp_type_size(attr, attrFmt.type) * comp_cnt_count(attr, attrFmt.cnt);
      break;
    }
    case GX_INDEX8:
      size += (i == GX_VA_NRM && vtxFmt.attrs[i].cnt == GX_NRM_NBT3) ? 3 : 1;
      break;
    case GX_INDEX16:
      size += (i == GX_VA_NRM && vtxFmt.attrs[i].cnt == GX_NRM_NBT3) ? 6 : 2;
      break;
    }
  }
  return size;
}

u32 texture_mip_count(bool hasMips, u32 mode1) {
  if (!hasMips) {
    return 1;
  }
  return static_cast<u32>(get_bits(mode1, 8, 8)) / 16 + 1;
}

u32 texture_size(u32 width, u32 height, u32 fmt, bool hasMips, u32 mode1) {
  if (width == 0 || height == 0) {
    return 0;
  }
  const u32 mipCount = texture_mip_count(hasMips, mode1);
  u32 size = 0;
  for (u32 mip = 0; mip < mipCount; ++mip) {
    size += GXGetTexBufferSize(static_cast<u16>(width), static_cast<u16>(height), fmt, GX_FALSE, 0);
    width = std::max(width >> 1, 1u);
    height = std::max(height >> 1, 1u);
  }
  return size;
}

void collect_resources(const std::vector<u8>& fifoBytes, std::vector<Resource>& resources) {
  ScanState state;
  u32 pos = 0;
  const u32 size = static_cast<u32>(fifoBytes.size());
  const u8* data = fifoBytes.data();

  while (pos < size) {
    const u8 cmd = data[pos++];
    const u8 opcode = cmd & GX_OPCODE_MASK;

    switch (opcode) {
    case GX_NOP:
    case GX_CMD_INVL_VC:
      break;
    case GX_LOAD_BP_REG & GX_OPCODE_MASK:
      if (pos + 4 > size) {
        return;
      }
      scan_bp(state, read_be32(data + pos));
      pos += 4;
      break;
    case GX_LOAD_CP_REG: {
      if (pos + 5 > size) {
        return;
      }
      const u8 addr = data[pos++];
      const u32 value = read_be32(data + pos);
      pos += 4;
      scan_vcd(state, addr, value);
      scan_vat(state, addr, value);
      break;
    }
    case GX_LOAD_XF_REG: {
      if (pos + 4 > size) {
        return;
      }
      const u32 header = read_be32(data + pos);
      pos += 4;
      pos += (((header >> 16) & 0xFFFF) + 1) * 4;
      if (pos > size) {
        return;
      }
      break;
    }
    case GX_LOAD_INDX_A:
    case GX_LOAD_INDX_B:
    case GX_LOAD_INDX_C:
    case GX_LOAD_INDX_D:
      pos += 4;
      if (pos > size) {
        return;
      }
      break;
    case GX_CMD_CALL_DL:
      pos += 8;
      if (pos > size) {
        return;
      }
      break;
    case GX_AURORA: {
      if (pos + 2 > size) {
        return;
      }
      const u16 subCmd = read_be16(data + pos);
      pos += 2;
      if (subCmd == GX_AURORA_LOAD_VIEWPORT_RENDER) {
        pos += 24;
      } else if (subCmd == GX_AURORA_LOAD_SCISSOR_RENDER) {
        pos += 16;
      } else if (subCmd >= GX_AURORA_LOAD_ARRAYBASE && subCmd <= (GX_AURORA_LOAD_ARRAYBASE | 0x0f)) {
        if (pos + 13 > size) {
          return;
        }
        const u64 pointer = read_be64(data + pos);
        pos += 8;
        const u32 arraySize = read_be32(data + pos);
        pos += 5;
        add_resource(resources, pointer, arraySize);
      } else if (subCmd == GX_AURORA_LOAD_TEXOBJ) {
        if (pos + 34 > size) {
          return;
        }
        const u8 texMapId = data[pos++];
        const u64 pointer = read_be64(data + pos);
        pos += 8;
        const u32 width = read_be32(data + pos);
        pos += 4;
        const u32 height = read_be32(data + pos);
        pos += 4;
        const u32 fmt = read_be32(data + pos);
        pos += 4;
        pos += 4; // tlut
        const bool hasMips = data[pos++] != 0;
        pos += 8; // object IDs/versions
        if (!is_copy_texture_key(state, pointer)) {
          const u32 mode1 = texMapId < state.texMode1.size() ? state.texMode1[texMapId] : 0;
          add_resource(resources, pointer, texture_size(width, height, fmt, hasMips, mode1));
        }
      } else if (subCmd == GX_AURORA_LOAD_TLUT) {
        if (pos + 23 > size) {
          return;
        }
        pos += 1;
        const u64 pointer = read_be64(data + pos);
        pos += 8;
        pos += 4;
        const u16 entries = read_be16(data + pos);
        pos += 10;
        add_resource(resources, pointer, static_cast<u32>(entries) * sizeof(u16));
      } else if (subCmd == GX2_SET_POLYGON_OFFSET) {
        pos += 20;
      } else if (subCmd == GX_AURORA_DESTROY_TEXOBJ || subCmd == GX_AURORA_DESTROY_TLUT) {
        pos += 4;
      } else if (subCmd == GX_AURORA_DESTROY_COPY_TEX) {
        pos += 8;
      } else if (subCmd == GX_AURORA_LOAD_COPY_SRC) {
        pos += 16;
      } else if (subCmd == GX_AURORA_LOAD_COPY_DST) {
        if (pos + 13 > size) {
          return;
        }
        pos += 13;
      } else if (subCmd == GX_AURORA_LOAD_COPY_DEST) {
        if (pos + 8 > size) {
          return;
        }
        // Copy destinations are GPU texture keys, not CPU-readable resource pointers.
        state.copyDstKey = read_be64(data + pos);
        pos += 8;
      } else if (subCmd == GX_AURORA_REQUEST_DEPTH_SNAPSHOT || subCmd == GX_AURORA_BEGIN_OFFSCREEN ||
                 subCmd == GX_AURORA_END_OFFSCREEN) {
        pos += subCmd == GX_AURORA_BEGIN_OFFSCREEN ? 8 : 0;
      } else if (subCmd == GX_AURORA_DRAW_SIZED) {
        if (pos + 5 > size) {
          return;
        }
        pos += 1;
        const u32 byteLen = read_be32(data + pos);
        pos += 4 + byteLen;
      } else if (subCmd == GX_AURORA_DRAW_INDEXED) {
        if (pos + 7 > size) {
          return;
        }
        const u8 drawCmd = data[pos++];
        const GXVtxFmt fmt = static_cast<GXVtxFmt>(drawCmd & GX_VAT_MASK);
        const u16 vtxCount = read_be16(data + pos);
        pos += 2;
        const u32 indexCount = read_be32(data + pos);
        pos += 4 + indexCount * sizeof(u16) + vtxCount * scan_vtx_size(state, fmt);
      } else if (subCmd == GX_AURORA_DEBUG_GROUP_PUSH || subCmd == GX_AURORA_DEBUG_MARKER_INSERT) {
        if (pos + 2 > size) {
          return;
        }
        const u16 len = read_be16(data + pos);
        pos += 2 + len;
      } else if (subCmd == GX_AURORA_DEBUG_GROUP_POP) {
      } else {
        return;
      }
      if (pos > size) {
        return;
      }
      break;
    }
    case GX_DRAW_QUADS:
    case GX_DRAW_TRIANGLES:
    case GX_DRAW_TRIANGLE_STRIP:
    case GX_DRAW_TRIANGLE_FAN:
    case GX_DRAW_LINES:
    case GX_DRAW_LINE_STRIP:
    case GX_DRAW_POINTS: {
      if (pos + 2 > size) {
        return;
      }
      const GXVtxFmt fmt = static_cast<GXVtxFmt>(cmd & GX_VAT_MASK);
      const u16 vtxCount = read_be16(data + pos);
      pos += 2 + vtxCount * scan_vtx_size(state, fmt);
      if (pos > size) {
        return;
      }
      break;
    }
    default:
      return;
    }
  }
}

std::vector<u8> build_resource_chunk(const std::vector<Resource>& resources) {
  std::vector<u8> out;
  for (const auto& resource : resources) {
    append_le64(out, resource.pointer);
    append_le32(out, resource.size);
    out.insert(out.end(), resource.data.begin(), resource.data.end());
  }
  return out;
}

void append_chunk(std::vector<u8>& out, u32 fourcc, const std::vector<u8>& payload) {
  append_le32(out, fourcc);
  append_le32(out, static_cast<u32>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

std::filesystem::path capture_path() {
  const char* base = aurora::g_config.userPath != nullptr ? aurora::g_config.userPath : ".";
  std::filesystem::path dir = std::filesystem::path{base} / "recordings";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    Log.warn("Failed to create FIFO recording directory {}: {}", dir.string(), ec.message());
  }

  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if _WIN32
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  char name[64];
  std::snprintf(name, sizeof(name), "aurora_fifo_%04d%02d%02d_%02d%02d%02d.bin", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return dir / name;
}

bool write_capture(const std::filesystem::path& path, const std::vector<u8>& fifoBytes,
                   const std::vector<Resource>& resources) {
  std::vector<u8> file;
  append_le32(file, kMagic);
  append_chunk(file, kChunkFifo, fifoBytes);
  append_chunk(file, kChunkResources, build_resource_chunk(resources));

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  return out.good();
}
} // namespace

void request() noexcept {
  s_captureRequested.store(true, std::memory_order_release);
  Log.info("FIFO capture requested");
}

void do_capture() noexcept {
  if (!s_captureRequested.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  try {
    auto fifoBytes = build_state_prefix(g_gxState);
    const auto* frameData = fifo::get_buffer_data();
    const auto frameSize = fifo::get_buffer_size();
    fifoBytes.insert(fifoBytes.end(), frameData, frameData + frameSize);

    std::vector<Resource> resources;
    collect_resources(fifoBytes, resources);

    const auto path = capture_path();
    if (!write_capture(path, fifoBytes, resources)) {
      Log.error("Failed to write FIFO capture {}", path.string());
      return;
    }
    Log.info("Wrote FIFO capture {} ({} FIFO bytes, {} resources)", path.string(), fifoBytes.size(),
             resources.size());
  } catch (const std::exception& ex) {
    Log.error("FIFO capture failed: {}", ex.what());
  } catch (...) {
    Log.error("FIFO capture failed with unknown exception");
  }
}

} // namespace aurora::gx::fifo_capture

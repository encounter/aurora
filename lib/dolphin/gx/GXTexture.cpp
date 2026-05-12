#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/texture.hpp"
#include "../../gfx/texture_replacement.hpp"
#include "dolphin/gx/GXAurora.h"

#include <algorithm>

#include "tracy/Tracy.hpp"

namespace {
constexpr u8 GXTexMode0Ids[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
constexpr u8 GXTexMode1Ids[8] = {0x84, 0x85, 0x86, 0x87, 0xA4, 0xA5, 0xA6, 0xA7};
constexpr u8 GXTexImage0Ids[8] = {0x88, 0x89, 0x8A, 0x8B, 0xA8, 0xA9, 0xAA, 0xAB};
constexpr u8 GXTexImage1Ids[8] = {0x8C, 0x8D, 0x8E, 0x8F, 0xAC, 0xAD, 0xAE, 0xAF};
constexpr u8 GXTexImage2Ids[8] = {0x90, 0x91, 0x92, 0x93, 0xB0, 0xB1, 0xB2, 0xB3};
constexpr u8 GXTexImage3Ids[8] = {0x94, 0x95, 0x96, 0x97, 0xB4, 0xB5, 0xB6, 0xB7};
constexpr u8 GXTexTlutIds[8] = {0x98, 0x99, 0x9A, 0x9B, 0xB8, 0xB9, 0xBA, 0xBB};
constexpr u8 GX2HWFiltConv[6] = {0x00, 0x04, 0x01, 0x05, 0x02, 0x06};

u32 sNextTexObjId = 1;
u32 sNextTlutObjId = 1;

u32 next_tex_obj_id() {
  if (sNextTexObjId == 0) {
    FATAL("texObj ID overflow");
  }
  return sNextTexObjId++;
}

u32 next_tlut_obj_id() {
  if (sNextTlutObjId == 0) {
    FATAL("tlutObj ID overflow");
  }
  return sNextTlutObjId++;
}

int __cntlzw(unsigned int val) {
  if (val == 0)
    return 32; // PowerPC returns 32 if the input is 0
#ifdef _MSC_VER
  unsigned long idx;
  _BitScanReverse(&idx, val);
  return 31 - (int)idx;
#else
  return __builtin_clz(val);
#endif
}

void init_texobj_common(GXTexObj_& obj, const void* data, u16 width, u16 height, u32 format, GXTexWrapMode wrapS,
                        GXTexWrapMode wrapT, GXBool mipmap) {
  obj = {};
  obj.mWidth = width;
  obj.mHeight = height;
  obj.mFormat = format;
  obj.tlut = GX_TLUT0;
  obj.flags = 2;
  obj.texObjId = next_tex_obj_id();
  obj.texDataVersion = 1;

  SET_REG_FIELD(0, obj.mode0, 2, 0, wrapS);
  SET_REG_FIELD(0, obj.mode0, 2, 2, wrapT);
  SET_REG_FIELD(0, obj.mode0, 1, 4, 1);
  if (mipmap) {
    obj.flags |= 1;
    obj.mode0 = (obj.mode0 & 0xFFFFFF1F) | 0xC0;
    const u32 maxDim = std::max(width, height);
    const u8 lmax = static_cast<u8>(16.0f * static_cast<float>(31 - __cntlzw(maxDim)));
    SET_REG_FIELD(0, obj.mode1, 8, 8, lmax);
  } else {
    obj.mode0 = (obj.mode0 & 0xFFFFFF1F) | 0x80;
  }
  SET_REG_FIELD(0, obj.image0, 10, 0, static_cast<u32>(width - 1));
  SET_REG_FIELD(0, obj.image0, 10, 10, static_cast<u32>(height - 1));
  SET_REG_FIELD(0, obj.image0, 4, 20, format & 0xF);

  obj.data = data;
}

void emit_loaded_texobj_metadata(const GXTexObj_& obj, GXTexMapID id) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_TEXOBJ);
  GX_WRITE_U8(static_cast<u8>(id));
  GX_WRITE_U64(reinterpret_cast<u64>(obj.data));
  GX_WRITE_U32(obj.width());
  GX_WRITE_U32(obj.height());
  GX_WRITE_U32(obj.format());
  GX_WRITE_U32(static_cast<u32>(obj.tlut));
  GX_WRITE_U8(static_cast<u8>(obj.has_mips()));
  GX_WRITE_U32(obj.texObjId);
  GX_WRITE_U32(obj.texDataVersion);
}

void emit_loaded_tlut_metadata(const GXTlutObj_& obj, u32 idx) {
  GX_WRITE_AURORA(GX_LOAD_AURORA_TLUT);
  GX_WRITE_U8(static_cast<u8>(idx));
  GX_WRITE_U64(reinterpret_cast<u64>(obj.data));
  GX_WRITE_U32(static_cast<u32>(obj.format));
  GX_WRITE_U16(obj.numEntries);
  GX_WRITE_U32(obj.tlutObjId);
  GX_WRITE_U32(obj.tlutDataVersion);
}
} // namespace

extern "C" {
void GXInitTexObj(GXTexObj* obj_, const void* data, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrapS,
                  GXTexWrapMode wrapT, GXBool mipmap) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  init_texobj_common(*obj, data, width, height, format, wrapS, wrapT, mipmap);
}

void GXInitTexObjCI(GXTexObj* obj_, const void* data, u16 width, u16 height, GXCITexFmt format, GXTexWrapMode wrapS,
                    GXTexWrapMode wrapT, GXBool mipmap, u32 tlut) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  init_texobj_common(*obj, data, width, height, format, wrapS, wrapT, mipmap);
  obj->tlut = static_cast<GXTlut>(tlut);
  obj->flags &= ~2u;
}

void GXInitTexObjLOD(GXTexObj* obj_, GXTexFilter minFilt, GXTexFilter magFilt, float minLod, float maxLod,
                     float lodBias, GXBool biasClamp, GXBool doEdgeLod, GXAnisotropy maxAniso) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  const float clampedBias = std::clamp(lodBias, -4.0f, 3.99f);
  const auto lbias = static_cast<u8>(32.0f * clampedBias);
  SET_REG_FIELD(0, obj->mode0, 8, 9, lbias);
  SET_REG_FIELD(0, obj->mode0, 1, 4, magFilt == GX_LINEAR ? 1 : 0);
  SET_REG_FIELD(0, obj->mode0, 3, 5, GX2HWFiltConv[minFilt]);
  SET_REG_FIELD(0, obj->mode0, 1, 8, doEdgeLod ? 0 : 1);
  obj->mode0 &= 0xFFFDFFFF;
  obj->mode0 &= 0xFFFBFFFF;
  SET_REG_FIELD(0, obj->mode0, 2, 19, maxAniso);
  SET_REG_FIELD(0, obj->mode0, 1, 21, biasClamp);

  const auto clampedMin = std::clamp(minLod, 0.0f, 10.0f);
  const auto clampedMax = std::clamp(maxLod, 0.0f, 10.0f);
  SET_REG_FIELD(0, obj->mode1, 8, 0, static_cast<u8>(16.0f * clampedMin));
  SET_REG_FIELD(0, obj->mode1, 8, 8, static_cast<u8>(16.0f * clampedMax));
}

void GXInitTexObjData(GXTexObj* obj_, const void* data) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->data = data;
  ++obj->texDataVersion;
}

void GXInitTexObjWrapMode(GXTexObj* obj_, GXTexWrapMode wrapS, GXTexWrapMode wrapT) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  SET_REG_FIELD(0, obj->mode0, 2, 0, wrapS);
  SET_REG_FIELD(0, obj->mode0, 2, 2, wrapT);
}

void GXInitTexObjTlut(GXTexObj* obj_, u32 tlut) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->tlut = static_cast<GXTlut>(tlut);
}

void GXInitTexObjFilter(GXTexObj* obj_, GXTexFilter minFilt, GXTexFilter magFilt) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  SET_REG_FIELD(0, obj->mode0, 3, 5, GX2HWFiltConv[minFilt]);
  SET_REG_FIELD(0, obj->mode0, 1, 4, magFilt == GX_LINEAR ? 1 : 0);
}

void GXInitTexObjMaxLOD(GXTexObj* obj_, float maxLod) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  const auto clampedMax = std::clamp(maxLod, 0.0f, 10.0f);
  SET_REG_FIELD(0, obj->mode1, 8, 8, static_cast<u8>(16.0f * clampedMax));
}

void GXInitTexObjMinLOD(GXTexObj* obj_, float minLod) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  const auto clampedMin = std::clamp(minLod, 0.0f, 10.0f);
  SET_REG_FIELD(0, obj->mode1, 8, 0, static_cast<u8>(16.0f * clampedMin));
}

void GXInitTexObjLODBias(GXTexObj* obj_, float lodBias) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  const float clampedBias = std::clamp(lodBias, -4.0f, 3.99f);
  const auto lbias = static_cast<u8>(32.0f * clampedBias);
  SET_REG_FIELD(0, obj->mode0, 8, 9, lbias);
}

void GXInitTexObjBiasClamp(GXTexObj* obj_, GXBool biasClamp) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  SET_REG_FIELD(0, obj->mode0, 1, 21, biasClamp);
}

void GXInitTexObjEdgeLOD(GXTexObj* obj_, GXBool doEdgeLod) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  SET_REG_FIELD(0, obj->mode0, 1, 8, doEdgeLod ? 0 : 1);
}

void GXInitTexObjMaxAniso(GXTexObj* obj_, GXAnisotropy maxAniso) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  SET_REG_FIELD(0, obj->mode0, 2, 19, maxAniso);
}

void GXInitTexObjUserData(GXTexObj* obj_, void* userData) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->userData = userData;
}

void* GXGetTexObjUserData(const GXTexObj* obj_) {
  const auto* obj = reinterpret_cast<const GXTexObj_*>(obj_);
  return const_cast<void*>(obj->userData);
}

void GXLoadTexObj(GXTexObj* obj_, GXTexMapID id) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  SET_REG_FIELD(0, obj->mode0, 8, 24, GXTexMode0Ids[id]);
  SET_REG_FIELD(0, obj->mode1, 8, 24, GXTexMode1Ids[id]);
  SET_REG_FIELD(0, obj->image0, 8, 24, GXTexImage0Ids[id]);

  u32 image1 = 0;
  u32 image2 = 0;
  SET_REG_FIELD(0, image1, 8, 24, GXTexImage1Ids[id]);
  SET_REG_FIELD(0, image2, 8, 24, GXTexImage2Ids[id]);
  SET_REG_FIELD(0, obj->image3, 8, 24, GXTexImage3Ids[id]);

  GX_WRITE_RAS_REG(obj->mode0);
  GX_WRITE_RAS_REG(obj->mode1);
  GX_WRITE_RAS_REG(obj->image0);
  GX_WRITE_RAS_REG(image1);
  GX_WRITE_RAS_REG(image2);
  GX_WRITE_RAS_REG(obj->image3);

  if ((obj->flags & 2) == 0) {
    u32 tlut = 0;
    SET_REG_FIELD(0, tlut, 10, 0, static_cast<u32>(obj->tlut));
    SET_REG_FIELD(0, tlut, 8, 24, GXTexTlutIds[id]);
    GX_WRITE_RAS_REG(tlut);
  }

  __gx->tImage0[id] = obj->image0;
  __gx->tMode0[id] = obj->mode0;
  __gx->dirtyState |= 1;
  __gx->bpSent = 1;

  emit_loaded_texobj_metadata(*obj, id);
}

u32 GXGetTexBufferSize(u16 width, u16 height, u32 fmt, GXBool mips, u8 maxLod) {
  s32 shiftX = 0;
  s32 shiftY = 0;
  switch (fmt) {
  case GX_TF_I4:
  case GX_TF_C4:
  case GX_TF_CMPR:
  case GX_CTF_R4:
  case GX_CTF_Z4:
    shiftX = 3;
    shiftY = 3;
    break;
  case GX_TF_I8:
  case GX_TF_IA4:
  case GX_TF_C8:
  case GX_TF_Z8:
  case GX_CTF_RA4:
  case GX_CTF_A8:
  case GX_CTF_R8:
  case GX_CTF_G8:
  case GX_CTF_B8:
  case GX_CTF_Z8M:
  case GX_CTF_Z8L:
    shiftX = 3;
    shiftY = 2;
    break;
  case GX_TF_IA8:
  case GX_TF_RGB565:
  case GX_TF_RGB5A3:
  case GX_TF_RGBA8:
  case GX_TF_C14X2:
  case GX_TF_Z16:
  case GX_TF_Z24X8:
  case GX_CTF_RA8:
  case GX_CTF_RG8:
  case GX_CTF_GB8:
  case GX_CTF_Z16L:
    shiftX = 2;
    shiftY = 2;
    break;
  default:
    break;
  }
  u32 bitSize = fmt == GX_TF_RGBA8 || fmt == GX_TF_Z24X8 ? 64 : 32;
  u32 bufLen = 0;
  if (mips) {
    while (maxLod != 0) {
      const u32 tileX = ((width + (1 << shiftX) - 1) >> shiftX);
      const u32 tileY = ((height + (1 << shiftY) - 1) >> shiftY);
      bufLen += bitSize * tileX * tileY;

      if (width == 1 && height == 1) {
        return bufLen;
      }

      width = (width < 2) ? 1 : width / 2;
      height = (height < 2) ? 1 : height / 2;
      --maxLod;
    };
  } else {
    const u32 tileX = ((width + (1 << shiftX) - 1) >> shiftX);
    const u32 tileY = ((height + (1 << shiftY) - 1) >> shiftY);
    bufLen = bitSize * tileX * tileY;
  }

  return bufLen;
}

void GXInitTlutObj(GXTlutObj* obj_, const void* data, GXTlutFmt format, u16 entries) {
  memset(obj_, 0, sizeof(GXTlutObj));
  auto* obj = reinterpret_cast<GXTlutObj_*>(obj_);
  obj->data = data;
  obj->format = format;
  obj->numEntries = entries;
  obj->tlutObjId = next_tlut_obj_id();
  obj->tlutDataVersion = 1;

  SET_REG_FIELD(0, obj->tlut, 2, 10, format);
  SET_REG_FIELD(0, obj->loadTlut0, 8, 24, 0x64);
  aurora::gfx::texture_replacement::register_tlut(obj_, data, format, entries);
}

void GXInitTlutObjData(GXTlutObj* obj_, const void* data) {
  auto* obj = reinterpret_cast<GXTlutObj_*>(obj_);
  obj->data = data;
  ++obj->tlutDataVersion;
}

void GXLoadTlut(const GXTlutObj* obj_, u32 idx) {
  auto* obj = reinterpret_cast<const GXTlutObj_*>(obj_);
  __GXFlushTextureState();
  GX_WRITE_RAS_REG(obj->loadTlut0);

  u32 loadTlut1 = 0;
  SET_REG_FIELD(0, loadTlut1, 10, 0, idx);
  SET_REG_FIELD(0, loadTlut1, 10, 10, obj->numEntries - 1);
  SET_REG_FIELD(0, loadTlut1, 8, 24, 0x65);
  GX_WRITE_RAS_REG(loadTlut1);
  __GXFlushTextureState();

  aurora::gfx::texture_replacement::load_tlut(obj_, idx);
  emit_loaded_tlut_metadata(*obj, idx);
}

// TODO GXInitTexCacheRegion
// TODO GXInitTexPreLoadRegion
// TODO GXInitTlutRegion
// TODO GXInvalidateTexRegion

void GXInvalidateTexAll() {
  // no-op?
}

// TODO GXPreLoadEntireTexture
// TODO GXSetTexRegionCallback
// TODO GXSetTlutRegionCallback
// TODO GXLoadTexObjPreLoaded
void GXSetTexCoordScaleManually(GXTexCoordID coord, GXBool enable, u16 ss, u16 ts) {
  __gx->tcsManEnab = (__gx->tcsManEnab & ~(1 << coord)) | (enable << coord);
  if (enable) {
    SET_REG_FIELD(0, __gx->suTs0[coord], 16, 0, static_cast<u16>(ss - 1));
    SET_REG_FIELD(0, __gx->suTs1[coord], 16, 0, static_cast<u16>(ts - 1));
    GX_WRITE_RAS_REG(__gx->suTs0[coord]);
    GX_WRITE_RAS_REG(__gx->suTs1[coord]);
    __gx->bpSent = 1;
  }
}

void GXSetTexCoordCylWrap(GXTexCoordID coord, GXBool s_enable, GXBool t_enable) {
  SET_REG_FIELD(0, __gx->suTs0[coord], 1, 17, s_enable);
  SET_REG_FIELD(0, __gx->suTs1[coord], 1, 17, t_enable);
  if (__gx->tcsManEnab & (1 << coord)) {
    GX_WRITE_RAS_REG(__gx->suTs0[coord]);
    GX_WRITE_RAS_REG(__gx->suTs1[coord]);
    __gx->bpSent = 1;
  }
}

void GXSetTexCoordBias(GXTexCoordID coord, GXBool s_enable, GXBool t_enable) {
  SET_REG_FIELD(0, __gx->suTs0[coord], 1, 16, s_enable);
  SET_REG_FIELD(0, __gx->suTs1[coord], 1, 16, t_enable);
  if (__gx->tcsManEnab & (1 << coord)) {
    GX_WRITE_RAS_REG(__gx->suTs0[coord]);
    GX_WRITE_RAS_REG(__gx->suTs1[coord]);
    __gx->bpSent = 1;
  }
}

void __GXFlushTextureState() {
  GX_WRITE_RAS_REG(__gx->bpMask);
  __gx->bpSent = 1;
}
}

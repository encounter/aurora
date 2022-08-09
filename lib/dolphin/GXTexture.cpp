#include "gx.hpp"

#include "../gfx/texture.hpp"

#include <absl/container/flat_hash_map.h>

void GXInitTexObj(GXTexObj* obj_, const void* data, u16 width, u16 height, u32 format, GXTexWrapMode wrapS,
                  GXTexWrapMode wrapT, GXBool mipmap) {
  memset(obj_, 0, sizeof(GXTexObj));
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->data = data;
  obj->width = width;
  obj->height = height;
  obj->fmt = format;
  obj->wrapS = wrapS;
  obj->wrapT = wrapT;
  obj->hasMips = mipmap;
  // TODO default values?
  obj->minFilter = GX_LINEAR;
  obj->magFilter = GX_LINEAR;
  obj->minLod = 0.f;
  obj->maxLod = 0.f;
  obj->lodBias = 0.f;
  obj->biasClamp = false;
  obj->doEdgeLod = false;
  obj->maxAniso = GX_ANISO_4;
  obj->tlut = GX_TLUT0;
  const auto it = g_gxState.copyTextures.find(data);
  if (it != g_gxState.copyTextures.end()) {
    obj->ref = it->second;
    obj->dataInvalidated = false;
  } else {
    obj->dataInvalidated = true;
  }
}

void GXInitTexObjCI(GXTexObj* obj_, const void* data, u16 width, u16 height, GXCITexFmt format, GXTexWrapMode wrapS,
                    GXTexWrapMode wrapT, GXBool mipmap, u32 tlut) {
  memset(obj_, 0, sizeof(GXTexObj));
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->data = data;
  obj->width = width;
  obj->height = height;
  obj->fmt = static_cast<GXTexFmt>(format);
  obj->wrapS = wrapS;
  obj->wrapT = wrapT;
  obj->hasMips = mipmap;
  obj->tlut = static_cast<GXTlut>(tlut);
  // TODO default values?
  obj->minFilter = GX_LINEAR;
  obj->magFilter = GX_LINEAR;
  obj->minLod = 0.f;
  obj->maxLod = 0.f;
  obj->lodBias = 0.f;
  obj->biasClamp = false;
  obj->doEdgeLod = false;
  obj->maxAniso = GX_ANISO_4;
  const auto it = g_gxState.copyTextures.find(data);
  if (it != g_gxState.copyTextures.end()) {
    obj->ref = it->second;
    obj->dataInvalidated = false;
  } else {
    obj->dataInvalidated = true;
  }
}

void GXInitTexObjLOD(GXTexObj* obj_, GXTexFilter minFilt, GXTexFilter magFilt, float minLod, float maxLod,
                     float lodBias, GXBool biasClamp, GXBool doEdgeLod, GXAnisotropy maxAniso) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->minFilter = minFilt;
  obj->magFilter = magFilt;
  obj->minLod = minLod;
  obj->maxLod = maxLod;
  obj->lodBias = lodBias;
  obj->biasClamp = biasClamp;
  obj->doEdgeLod = doEdgeLod;
  obj->maxAniso = maxAniso;
}

void GXInitTexObjData(GXTexObj* obj_, const void* data) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  const auto it = g_gxState.copyTextures.find(data);
  if (it != g_gxState.copyTextures.end()) {
    obj->ref = it->second;
    obj->dataInvalidated = false;
  } else {
    obj->data = data;
    obj->dataInvalidated = true;
  }
}

void GXInitTexObjWrapMode(GXTexObj* obj_, GXTexWrapMode wrapS, GXTexWrapMode wrapT) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->wrapS = wrapS;
  obj->wrapT = wrapT;
}

void GXInitTexObjTlut(GXTexObj* obj_, u32 tlut) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->tlut = static_cast<GXTlut>(tlut);
}

// TODO GXInitTexObjFilter
// TODO GXInitTexObjMaxLOD
// TODO GXInitTexObjMinLOD
// TODO GXInitTexObjLODBias
// TODO GXInitTexObjBiasClamp
// TODO GXInitTexObjEdgeLOD
// TODO GXInitTexObjMaxAniso
// TODO GXInitTexObjUserData
// TODO GXGetTexObjUserData

void GXLoadTexObj(GXTexObj* obj_, GXTexMapID id) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  if (!obj->ref) {
    obj->ref = aurora::gfx::new_dynamic_texture_2d(obj->width, obj->height, u32(obj->maxLod) + 1, obj->fmt,
                                                   fmt::format(FMT_STRING("GXLoadTexObj_{}"), obj->fmt).c_str());
  }
  if (obj->dataInvalidated) {
    aurora::gfx::write_texture(*obj->ref, {static_cast<const u8*>(obj->data), UINT32_MAX /* TODO */});
    obj->dataInvalidated = false;
  }
  g_gxState.textures[id] = {*obj};
  g_gxState.stateDirty = true; // TODO only if changed?
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
  GXTexFmt texFmt;
  switch (format) {
    DEFAULT_FATAL("invalid tlut format {}", static_cast<int>(format));
  case GX_TL_IA8:
    texFmt = GX_TF_IA8;
    break;
  case GX_TL_RGB565:
    texFmt = GX_TF_RGB565;
    break;
  case GX_TL_RGB5A3:
    texFmt = GX_TF_RGB5A3;
    break;
  }
  auto* obj = reinterpret_cast<GXTlutObj_*>(obj_);
  obj->ref = aurora::gfx::new_static_texture_2d(
      entries, 1, 1, texFmt, aurora::ArrayRef{static_cast<const u8*>(data), static_cast<size_t>(entries) * 2},
      "GXInitTlutObj");
}

void GXLoadTlut(const GXTlutObj* obj_, GXTlut idx) {
  g_gxState.tluts[idx] = *reinterpret_cast<const GXTlutObj_*>(obj_);
  // TODO stateDirty?
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
// TODO GXSetTexCoordScaleManually
// TODO GXSetTexCoordCylWrap
// TODO GXSetTexCoordBias

#ifndef DOLPHIN_GXGET_H
#define DOLPHIN_GXGET_H

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>

#ifdef __cplusplus
extern "C" {
#endif

GXBool GXGetTexObjMipMap(GXTexObj* tex_obj);
GXTexFmt GXGetTexObjFmt(GXTexObj* tex_obj);
u16 GXGetTexObjHeight(GXTexObj* tex_obj);
u16 GXGetTexObjWidth(GXTexObj* tex_obj);
GXTexWrapMode GXGetTexObjWrapS(GXTexObj* tex_obj);
GXTexWrapMode GXGetTexObjWrapT(GXTexObj* tex_obj);
void* GXGetTexObjData(GXTexObj* tex_obj);
void GXGetProjectionv(f32* p);
void GXGetLightPos(GXLightObj* lt_obj, f32* x, f32* y, f32* z);
void GXGetLightColor(GXLightObj* lt_obj, GXColor* color);
void GXGetVtxAttrFmt(GXVtxFmt idx, GXAttr attr, GXCompCnt* compCnt, GXCompType* compType, u8* shift);

#ifdef __cplusplus
}
#endif

#endif

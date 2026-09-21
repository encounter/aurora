#ifndef DOLPHIN_GXGET_H
#define DOLPHIN_GXGET_H

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>

#ifdef __cplusplus
extern "C" {
#endif

void GXGetVtxDesc(GXAttr attr, GXAttrType* type);
void GXGetVtxDescv(GXVtxDescList* vcd);
void GXGetVtxAttrFmtv(GXVtxFmt fmt, GXVtxAttrFmtList* vat);
void GXGetLineWidth(u8* width, GXTexOffset* texOffsets);
void GXGetPointSize(u8* pointSize, GXTexOffset* texOffsets);
void GXGetCullMode(GXCullMode* mode);
GXBool GXGetTexObjMipMap(const GXTexObj* tex_obj);
GXTexFmt GXGetTexObjFmt(const GXTexObj* tex_obj);
u16 GXGetTexObjHeight(const GXTexObj* tex_obj);
u16 GXGetTexObjWidth(const GXTexObj* tex_obj);
GXTexWrapMode GXGetTexObjWrapS(const GXTexObj* tex_obj);
GXTexWrapMode GXGetTexObjWrapT(const GXTexObj* tex_obj);
void* GXGetTexObjData(const GXTexObj* tex_obj);
void GXGetProjectionv(f32* p);
void GXGetLightPos(const GXLightObj* lt_obj, f32* x, f32* y, f32* z);
void GXGetLightColor(const GXLightObj* lt_obj, GXColor* color);
void GXGetVtxAttrFmt(GXVtxFmt idx, GXAttr attr, GXCompCnt* compCnt, GXCompType* compType, u8* shift);
u32 GXGetTexObjTlut(const GXTexObj* tex_obj);

void GXGetViewportv(f32* vp);
void GXGetScissor(u32* left, u32* top, u32* wd, u32* ht);

#ifdef __cplusplus
}
#endif

#endif

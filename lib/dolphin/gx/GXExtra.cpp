#include "gx.hpp"
#include "__gx.h"
#include "dolphin/gx/GXAurora.h"

extern "C" {
void GXDestroyTexObj(GXTexObj* obj_) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  if (obj->texObjId != 0) {
    GX_WRITE_AURORA(GX_LOAD_AURORA_DESTROY_TEXOBJ);
    GX_WRITE_U32(obj->texObjId);
  }
  obj->texObjId = 0;
}

void GXDestroyTlutObj(GXTlutObj* obj_) {
  auto* obj = reinterpret_cast<GXTlutObj_*>(obj_);
  if (obj->tlutObjId != 0) {
    GX_WRITE_AURORA(GX_LOAD_AURORA_DESTROY_TLUT);
    GX_WRITE_U32(obj->tlutObjId);
  }
  obj->tlutObjId = 0;
}
}

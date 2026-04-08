#include "gx.hpp"

extern "C" {

void GXDestroyTexObj(GXTexObj* obj_) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  if (obj->texObjId == 0) {
    obj->texObjId = 1;
  } else {
    ++obj->texObjId;
    if (obj->texObjId == 0) {
      obj->texObjId = 1;
    }
  }
  if (obj->texDataVersion == 0) {
    obj->texDataVersion = 1;
  } else {
    ++obj->texDataVersion;
    if (obj->texDataVersion == 0) {
      obj->texDataVersion = 1;
    }
  }
}

void GXDestroyTlutObj(GXTlutObj* obj_) {
  auto* obj = reinterpret_cast<GXTlutObj_*>(obj_);
  if (obj->tlutObjId == 0) {
    obj->tlutObjId = 1;
  } else {
    ++obj->tlutObjId;
    if (obj->tlutObjId == 0) {
      obj->tlutObjId = 1;
    }
  }
  if (obj->tlutDataVersion == 0) {
    obj->tlutDataVersion = 1;
  } else {
    ++obj->tlutDataVersion;
    if (obj->tlutDataVersion == 0) {
      obj->tlutDataVersion = 1;
    }
  }
}
}

#include "gx.hpp"

void GXDestroyTexObj(GXTexObj* obj_) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  obj->ref.reset();
}

void GXDestroyTlutObj(GXTlutObj* obj_) {
  auto* obj = reinterpret_cast<GXTlutObj_*>(obj_);
  obj->ref.reset();
}

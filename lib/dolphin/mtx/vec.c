#include <assert.h>
#include <math.h>
#include <dolphin/mtx.h>

void C_VECAdd(const Vec* a, const Vec* b, Vec* ab) {
  assert(a && "VECAdd():  NULL VecPtr 'a' ");
  assert(b && "VECAdd():  NULL VecPtr 'b' ");
  assert(ab && "VECAdd():  NULL VecPtr 'ab' ");
  ab->x = a->x + b->x;
  ab->y = a->y + b->y;
  ab->z = a->z + b->z;
}

void C_VECSubtract(const Vec* a, const Vec* b, Vec* a_b) {
  assert(a && "VECSubtract():  NULL VecPtr 'a' ");
  assert(b && "VECSubtract():  NULL VecPtr 'b' ");
  assert(a_b && "VECSubtract():  NULL VecPtr 'a_b' ");
  a_b->x = a->x - b->x;
  a_b->y = a->y - b->y;
  a_b->z = a->z - b->z;
}

void C_VECScale(const Vec* src, Vec* dst, f32 scale) {
  assert(src && "VECScale():  NULL VecPtr 'src' ");
  assert(dst && "VECScale():  NULL VecPtr 'dst' ");
  dst->x = (src->x * scale);
  dst->y = (src->y * scale);
  dst->z = (src->z * scale);
}

void C_VECNormalize(const Vec* src, Vec* unit) {
  f32 mag;

  assert(src && "VECNormalize():  NULL VecPtr 'src' ");
  assert(unit && "VECNormalize():  NULL VecPtr 'unit' ");

  mag = (src->z * src->z) + ((src->x * src->x) + (src->y * src->y));
  assert(0.0f != mag && "VECNormalize():  zero magnitude vector ");

  mag = 1.0f/ sqrtf(mag);
  unit->x = src->x * mag;
  unit->y = src->y * mag;
  unit->z = src->z * mag;
}

f32 C_VECSquareMag(const Vec* v) {
  f32 sqmag;

  assert(v && "VECMag():  NULL VecPtr 'v' ");

  sqmag = v->z * v->z + ((v->x * v->x) + (v->y * v->y));
  return sqmag;
}

f32 C_VECMag(const Vec* v) {
  return sqrtf(C_VECSquareMag(v));
}

f32 C_VECDotProduct(const Vec* a, const Vec* b) {
  f32 dot;

  assert(a && "VECDotProduct():  NULL VecPtr 'a' ");
  assert(b && "VECDotProduct():  NULL VecPtr 'b' ");
  dot = (a->z * b->z) + ((a->x * b->x) + (a->y * b->y));
  return dot;
}

void C_VECCrossProduct(const Vec* a, const Vec* b, Vec* axb) {
  Vec vTmp;

  assert(a && "VECCrossProduct():  NULL VecPtr 'a' ");
  assert(b && "VECCrossProduct():  NULL VecPtr 'b' ");
  assert(axb && "VECCrossProduct():  NULL VecPtr 'axb' ");

  vTmp.x = (a->y * b->z) - (a->z * b->y);
  vTmp.y = (a->z * b->x) - (a->x * b->z);
  vTmp.z = (a->x * b->y) - (a->y * b->x);
  axb->x = vTmp.x;
  axb->y = vTmp.y;
  axb->z = vTmp.z;
}

void C_VECHalfAngle(const Vec* a, const Vec* b, Vec* half) {
  Vec aTmp;
  Vec bTmp;
  Vec hTmp;

  assert(a && "VECHalfAngle():  NULL VecPtr 'a' ");
  assert(b && "VECHalfAngle():  NULL VecPtr 'b' ");
  assert(half && "VECHalfAngle():  NULL VecPtr 'half' ");

  aTmp.x = -a->x;
  aTmp.y = -a->y;
  aTmp.z = -a->z;
  bTmp.x = -b->x;
  bTmp.y = -b->y;
  bTmp.z = -b->z;

  VECNormalize(&aTmp, &aTmp);
  VECNormalize(&bTmp, &bTmp);
  VECAdd(&aTmp, &bTmp, &hTmp);

  if (VECDotProduct(&hTmp, &hTmp) > 0.0f) {
    VECNormalize(&hTmp, half);
    return;
  }
  *half = hTmp;
}

void C_VECReflect(const Vec* src, const Vec* normal, Vec* dst) {
  f32 cosA;
  Vec uI;
  Vec uN;

  assert(src && "VECReflect():  NULL VecPtr 'src' ");
  assert(normal && "VECReflect():  NULL VecPtr 'normal' ");
  assert(dst && "VECReflect():  NULL VecPtr 'dst' ");

  uI.x = -src->x;
  uI.y = -src->y;
  uI.z = -src->z;

  VECNormalize(&uI, &uI);
  VECNormalize(normal, &uN);

  cosA = VECDotProduct(&uI, &uN);
  dst->x = (2.0f * uN.x * cosA) - uI.x;
  dst->y = (2.0f * uN.y * cosA) - uI.y;
  dst->z = (2.0f * uN.z * cosA) - uI.z;
  VECNormalize(dst, dst);
}

f32 C_VECSquareDistance(const Vec* a, const Vec* b) {
  Vec diff;

  diff.x = a->x - b->x;
  diff.y = a->y - b->y;
  diff.z = a->z - b->z;
  return (diff.z * diff.z) + ((diff.x * diff.x) + (diff.y * diff.y));
}

f32 C_VECDistance(const Vec* a, const Vec* b) {
  return sqrtf(C_VECSquareDistance(a, b));
}

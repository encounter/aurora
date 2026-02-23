#include <math.h>
#include <dolphin/mtx.h>

#define ASSERTLINE(line, cond) (void)0
#define ASSERTMSGLINE(line, cond, msg) (void)0
#define ASSERTMSG1LINE(line, cond, msg, arg1) (void)0
#define ASSERTMSG2LINE(line, cond, msg, arg1, arg2) (void)0
#define ASSERTMSGLINEV(line, cond, ...) (void)0

void C_VECAdd(const Vec* a, const Vec* b, Vec* ab) {
  ASSERTMSGLINE(108, a, "VECAdd():  NULL VecPtr 'a' ");
  ASSERTMSGLINE(109, b, "VECAdd():  NULL VecPtr 'b' ");
  ASSERTMSGLINE(110, ab, "VECAdd():  NULL VecPtr 'ab' ");
  ab->x = a->x + b->x;
  ab->y = a->y + b->y;
  ab->z = a->z + b->z;
}

void C_VECSubtract(const Vec* a, const Vec* b, Vec* a_b) {
  ASSERTMSGLINE(177, a, "VECSubtract():  NULL VecPtr 'a' ");
  ASSERTMSGLINE(178, b, "VECSubtract():  NULL VecPtr 'b' ");
  ASSERTMSGLINE(179, a_b, "VECSubtract():  NULL VecPtr 'a_b' ");
  a_b->x = a->x - b->x;
  a_b->y = a->y - b->y;
  a_b->z = a->z - b->z;
}

void C_VECScale(const Vec* src, Vec* dst, f32 scale) {
  ASSERTMSGLINE(247, src, "VECScale():  NULL VecPtr 'src' ");
  ASSERTMSGLINE(248, dst, "VECScale():  NULL VecPtr 'dst' ");
  dst->x = (src->x * scale);
  dst->y = (src->y * scale);
  dst->z = (src->z * scale);
}

void C_VECNormalize(const Vec* src, Vec* unit) {
  f32 mag;

  ASSERTMSGLINE(315, src, "VECNormalize():  NULL VecPtr 'src' ");
  ASSERTMSGLINE(316, unit, "VECNormalize():  NULL VecPtr 'unit' ");

  mag = (src->z * src->z) + ((src->x * src->x) + (src->y * src->y));
  ASSERTMSGLINE(321, 0.0f != mag, "VECNormalize():  zero magnitude vector ");

  mag = 1.0f/ sqrtf(mag);
  unit->x = src->x * mag;
  unit->y = src->y * mag;
  unit->z = src->z * mag;
}

f32 C_VECSquareMag(const Vec* v) {
  f32 sqmag;

  ASSERTMSGLINE(405, v, "VECMag():  NULL VecPtr 'v' ");

  sqmag = v->z * v->z + ((v->x * v->x) + (v->y * v->y));
  return sqmag;
}

f32 C_VECMag(const Vec* v) {
  return sqrtf(C_VECSquareMag(v));
}

f32 C_VECDotProduct(const Vec* a, const Vec* b) {
  f32 dot;

  ASSERTMSGLINE(540, a, "VECDotProduct():  NULL VecPtr 'a' ");
  ASSERTMSGLINE(541, b, "VECDotProduct():  NULL VecPtr 'b' ");
  dot = (a->z * b->z) + ((a->x * b->x) + (a->y * b->y));
  return dot;
}

void C_VECCrossProduct(const Vec* a, const Vec* b, Vec* axb) {
  Vec vTmp;

  ASSERTMSGLINE(602, a, "VECCrossProduct():  NULL VecPtr 'a' ");
  ASSERTMSGLINE(603, b, "VECCrossProduct():  NULL VecPtr 'b' ");
  ASSERTMSGLINE(604, axb, "VECCrossProduct():  NULL VecPtr 'axb' ");

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

  ASSERTMSGLINE(707, a, "VECHalfAngle():  NULL VecPtr 'a' ");
  ASSERTMSGLINE(708, b, "VECHalfAngle():  NULL VecPtr 'b' ");
  ASSERTMSGLINE(709, half, "VECHalfAngle():  NULL VecPtr 'half' ");

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

  ASSERTMSGLINE(763, src, "VECReflect():  NULL VecPtr 'src' ");
  ASSERTMSGLINE(764, normal, "VECReflect():  NULL VecPtr 'normal' ");
  ASSERTMSGLINE(765, dst, "VECReflect():  NULL VecPtr 'dst' ");

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

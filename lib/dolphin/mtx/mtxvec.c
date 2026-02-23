#include <dolphin/mtx.h>

#define ASSERTLINE(line, cond) (void)0
#define ASSERTMSGLINE(line, cond, msg) (void)0
#define ASSERTMSG1LINE(line, cond, msg, arg1) (void)0
#define ASSERTMSG2LINE(line, cond, msg, arg1, arg2) (void)0
#define ASSERTMSGLINEV(line, cond, ...) (void)0

void C_MTXMultVec(const Mtx m, const Vec* src, Vec* dst) {
  Vec vTmp;

  ASSERTMSGLINE(66, m, "MTXMultVec():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(67, src, "MTXMultVec():  NULL VecPtr 'src' ");
  ASSERTMSGLINE(68, dst, "MTXMultVec():  NULL VecPtr 'dst' ");

  vTmp.x = m[0][3] + ((m[0][2] * src->z) + ((m[0][0] * src->x) + (m[0][1] * src->y)));
  vTmp.y = m[1][3] + ((m[1][2] * src->z) + ((m[1][0] * src->x) + (m[1][1] * src->y)));
  vTmp.z = m[2][3] + ((m[2][2] * src->z) + ((m[2][0] * src->x) + (m[2][1] * src->y)));
  dst->x = vTmp.x;
  dst->y = vTmp.y;
  dst->z = vTmp.z;
}

void C_MTXMultVecArray(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
  u32 i;
  Vec vTmp;

  ASSERTMSGLINE(168, m, "MTXMultVecArray():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(169, srcBase, "MTXMultVecArray():  NULL VecPtr 'srcBase' ");
  ASSERTMSGLINE(170, dstBase, "MTXMultVecArray():  NULL VecPtr 'dstBase' ");
  ASSERTMSGLINE(171, count > 1, "MTXMultVecArray():  count must be greater than 1.");

  for(i = 0; i < count; i++) {
    vTmp.x = m[0][3] + ((m[0][2] * srcBase->z) + ((m[0][0] * srcBase->x) + (m[0][1] * srcBase->y)));
    vTmp.y = m[1][3] + ((m[1][2] * srcBase->z) + ((m[1][0] * srcBase->x) + (m[1][1] * srcBase->y)));
    vTmp.z = m[2][3] + ((m[2][2] * srcBase->z) + ((m[2][0] * srcBase->x) + (m[2][1] * srcBase->y)));
    dstBase->x = vTmp.x;
    dstBase->y = vTmp.y;
    dstBase->z = vTmp.z;
    srcBase++;
    dstBase++;
  }
}

void C_MTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) {
  Vec vTmp;

  ASSERTMSGLINE(313, m, "MTXMultVecSR():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(314, src, "MTXMultVecSR():  NULL VecPtr 'src' ");
  ASSERTMSGLINE(315, dst, "MTXMultVecSR():  NULL VecPtr 'dst' ");

  vTmp.x = (m[0][2] * src->z) + ((m[0][0] * src->x) + (m[0][1] * src->y));
  vTmp.y = (m[1][2] * src->z) + ((m[1][0] * src->x) + (m[1][1] * src->y));
  vTmp.z = (m[2][2] * src->z) + ((m[2][0] * src->x) + (m[2][1] * src->y));
  dst->x = vTmp.x;
  dst->y = vTmp.y;
  dst->z = vTmp.z;
}

void C_MTXMultVecArraySR(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
  u32 i;
  Vec vTmp;

  ASSERTMSGLINE(410, m, "MTXMultVecArraySR():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(411, srcBase, "MTXMultVecArraySR():  NULL VecPtr 'srcBase' ");
  ASSERTMSGLINE(412, dstBase, "MTXMultVecArraySR():  NULL VecPtr 'dstBase' ");
  ASSERTMSGLINE(413, count > 1, "MTXMultVecArraySR():  count must be greater than 1.");

  for(i = 0; i < count; i++) {
    vTmp.x = (m[0][2] * srcBase->z) + ((m[0][0] * srcBase->x) + (m[0][1] * srcBase->y));
    vTmp.y = (m[1][2] * srcBase->z) + ((m[1][0] * srcBase->x) + (m[1][1] * srcBase->y));
    vTmp.z = (m[2][2] * srcBase->z) + ((m[2][0] * srcBase->x) + (m[2][1] * srcBase->y));
    dstBase->x = vTmp.x;
    dstBase->y = vTmp.y;
    dstBase->z = vTmp.z;
    srcBase++;
    dstBase++;
  }
}


#include <assert.h>
#include <dolphin/mtx.h>

void C_MTXMultVec(const Mtx m, const Vec* src, Vec* dst) {
  Vec vTmp;

  assert(m && "MTXMultVec():  NULL MtxPtr 'm' ");
  assert(src && "MTXMultVec():  NULL VecPtr 'src' ");
  assert(dst && "MTXMultVec():  NULL VecPtr 'dst' ");

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

  assert(m && "MTXMultVecArray():  NULL MtxPtr 'm' ");
  assert(srcBase && "MTXMultVecArray():  NULL VecPtr 'srcBase' ");
  assert(dstBase && "MTXMultVecArray():  NULL VecPtr 'dstBase' ");
  assert(count > 1 && "MTXMultVecArray():  count must be greater than 1.");

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

  assert(m && "MTXMultVecSR():  NULL MtxPtr 'm' ");
  assert(src && "MTXMultVecSR():  NULL VecPtr 'src' ");
  assert(dst && "MTXMultVecSR():  NULL VecPtr 'dst' ");

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

  assert(m && "MTXMultVecArraySR():  NULL MtxPtr 'm' ");
  assert(srcBase && "MTXMultVecArraySR():  NULL VecPtr 'srcBase' ");
  assert(dstBase && "MTXMultVecArraySR():  NULL VecPtr 'dstBase' ");
  assert(count > 1 && "MTXMultVecArraySR():  count must be greater than 1.");

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


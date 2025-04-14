#include <dolphin/mtx.h>

#define ASSERTLINE(line, cond) (void)0
#define ASSERTMSGLINE(line, cond, msg) (void)0
#define ASSERTMSG1LINE(line, cond, msg, arg1) (void)0
#define ASSERTMSG2LINE(line, cond, msg, arg1, arg2) (void)0
#define ASSERTMSGLINEV(line, cond, ...) (void)0

void C_MTXIdentity(Mtx m) {
  ASSERTMSGLINE(189, m, "MtxIdentity():  NULL Mtx 'm' ");
  m[0][0] = 1;
  m[0][1] = 0;
  m[0][2] = 0;
  m[0][3] = 0;
  m[1][0] = 0;
  m[1][1] = 1;
  m[1][2] = 0;
  m[1][3] = 0;
  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = 1;
  m[2][3] = 0;
}

void C_MTXCopy(const Mtx src, Mtx dst) {
  ASSERTMSGLINE(250, src, "MTXCopy():  NULL MtxPtr 'src' ");
  ASSERTMSGLINE(251, dst, "MTXCopy():  NULL MtxPtr 'dst' ");
  if (src != dst) {
    dst[0][0] = src[0][0];
    dst[0][1] = src[0][1];
    dst[0][2] = src[0][2];
    dst[0][3] = src[0][3];
    dst[1][0] = src[1][0];
    dst[1][1] = src[1][1];
    dst[1][2] = src[1][2];
    dst[1][3] = src[1][3];
    dst[2][0] = src[2][0];
    dst[2][1] = src[2][1];
    dst[2][2] = src[2][2];
    dst[2][3] = src[2][3];
  }
}

void C_MTXConcat(const Mtx a, const Mtx b, Mtx ab) {
  Mtx mTmp;
  MtxPtr m;

  ASSERTMSGLINE(324, a, "MTXConcat():  NULL MtxPtr 'a'  ");
  ASSERTMSGLINE(325, b, "MTXConcat():  NULL MtxPtr 'b'  ");
  ASSERTMSGLINE(326, ab, "MTXConcat():  NULL MtxPtr 'ab' ");

  if (ab == a || ab == b) {
    m = mTmp;
  } else {
    m = ab;
  }

  m[0][0] =       0 +  a[0][2] * b[2][0] + ((a[0][0] * b[0][0]) + (a[0][1] * b[1][0]));
  m[0][1] =       0 +  a[0][2] * b[2][1] + ((a[0][0] * b[0][1]) + (a[0][1] * b[1][1]));
  m[0][2] =       0 +  a[0][2] * b[2][2] + ((a[0][0] * b[0][2]) + (a[0][1] * b[1][2]));
  m[0][3] = a[0][3] + (a[0][2] * b[2][3] +  (a[0][0] * b[0][3]  + (a[0][1] * b[1][3])));

  m[1][0] =       0 +  a[1][2] * b[2][0] + ((a[1][0] * b[0][0]) + (a[1][1] * b[1][0]));
  m[1][1] =       0 +  a[1][2] * b[2][1] + ((a[1][0] * b[0][1]) + (a[1][1] * b[1][1]));
  m[1][2] =       0 +  a[1][2] * b[2][2] + ((a[1][0] * b[0][2]) + (a[1][1] * b[1][2]));
  m[1][3] = a[1][3] + (a[1][2] * b[2][3] +  (a[1][0] * b[0][3]  + (a[1][1] * b[1][3])));

  m[2][0] =       0 +  a[2][2] * b[2][0] + ((a[2][0] * b[0][0]) + (a[2][1] * b[1][0]));
  m[2][1] =       0 +  a[2][2] * b[2][1] + ((a[2][0] * b[0][1]) + (a[2][1] * b[1][1]));
  m[2][2] =       0 +  a[2][2] * b[2][2] + ((a[2][0] * b[0][2]) + (a[2][1] * b[1][2]));
  m[2][3] = a[2][3] + (a[2][2] * b[2][3] +  (a[2][0] * b[0][3]  + (a[2][1] * b[1][3])));

  if (m == mTmp) {
    C_MTXCopy(mTmp, ab);
  }
}

u32 C_MTXInvXpose(const Mtx src, Mtx invX) {
  Mtx mTmp;
  MtxPtr m;
  f32 det;

  ASSERTMSGLINE(1185, src, "MTXInvXpose(): NULL MtxPtr 'src' ");
  ASSERTMSGLINE(1186, invX, "MTXInvXpose(): NULL MtxPtr 'invX' ");

  if (src == invX) {
    m = mTmp;
  } else {
    m = invX;
  }
  det = ((((src[2][1] * (src[0][2] * src[1][0]))
        + ((src[2][2] * (src[0][0] * src[1][1]))
        +  (src[2][0] * (src[0][1] * src[1][2]))))
        -  (src[0][2] * (src[2][0] * src[1][1])))
        -  (src[2][2] * (src[1][0] * src[0][1])))
        -  (src[1][2] * (src[0][0] * src[2][1]));
  if (0 == det) {
    return 0;
  }
  det = 1 / det;
  m[0][0] = (det * +((src[1][1] * src[2][2]) - (src[2][1] * src[1][2])));
  m[0][1] = (det * -((src[1][0] * src[2][2]) - (src[2][0] * src[1][2])));
  m[0][2] = (det * +((src[1][0] * src[2][1]) - (src[2][0] * src[1][1])));

  m[1][0] = (det * -((src[0][1] * src[2][2]) - (src[2][1] * src[0][2])));
  m[1][1] = (det * +((src[0][0] * src[2][2]) - (src[2][0] * src[0][2])));
  m[1][2] = (det * -((src[0][0] * src[2][1]) - (src[2][0] * src[0][1])));

  m[2][0] = (det * +((src[0][1] * src[1][2]) - (src[1][1] * src[0][2])));
  m[2][1] = (det * -((src[0][0] * src[1][2]) - (src[1][0] * src[0][2])));
  m[2][2] = (det * +((src[0][0] * src[1][1]) - (src[1][0] * src[0][1])));

  m[0][3] = 0;
  m[1][3] = 0;
  m[2][3] = 0;

  if (m == mTmp) {
    C_MTXCopy(mTmp, invX);
  }
  return 1;
}

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

void C_MTXTrans(Mtx m, f32 xT, f32 yT, f32 zT) {
  ASSERTMSGLINE(1866, m, "MTXTrans():  NULL MtxPtr 'm' ");
  m[0][0] = 1;
  m[0][1] = 0;
  m[0][2] = 0;
  m[0][3] = xT;
  m[1][0] = 0;
  m[1][1] = 1;
  m[1][2] = 0;
  m[1][3] = yT;
  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = 1;
  m[2][3] = zT;
}

void C_MTXFrustum(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
  f32 tmp;

  ASSERTMSGLINE(105, m, "MTXFrustum():  NULL Mtx44Ptr 'm' ");
  ASSERTMSGLINE(106, t != b, "MTXFrustum():  't' and 'b' clipping planes are equal ");
  ASSERTMSGLINE(107, l != r, "MTXFrustum():  'l' and 'r' clipping planes are equal ");
  ASSERTMSGLINE(108, n != f, "MTXFrustum():  'n' and 'f' clipping planes are equal ");
  tmp = 1 / (r - l);
  m[0][0] = (2 * n * tmp);
  m[0][1] = 0;
  m[0][2] = (tmp * (r + l));
  m[0][3] = 0;
  tmp = 1 / (t - b);
  m[1][0] = 0;
  m[1][1] = (2 * n * tmp);
  m[1][2] = (tmp * (t + b));
  m[1][3] = 0;
  m[2][0] = 0;
  m[2][1] = 0;
  tmp = 1 / (f - n);
  m[2][2] = (-n * tmp);
  m[2][3] = (tmp * -(f * n));
  m[3][0] = 0;
  m[3][1] = 0;
  m[3][2] = -1;
  m[3][3] = 0;
}

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
  f32 tmp;

  ASSERTMSGLINE(254, m, "MTXOrtho():  NULL Mtx44Ptr 'm' ");
  ASSERTMSGLINE(255, t != b, "MTXOrtho():  't' and 'b' clipping planes are equal ");
  ASSERTMSGLINE(256, l != r, "MTXOrtho():  'l' and 'r' clipping planes are equal ");
  ASSERTMSGLINE(257, n != f, "MTXOrtho():  'n' and 'f' clipping planes are equal ");
  tmp = 1 / (r - l);
  m[0][0] = 2 * tmp;
  m[0][1] = 0;
  m[0][2] = 0;
  m[0][3] = (tmp * -(r + l));
  tmp = 1 / (t - b);
  m[1][0] = 0;
  m[1][1] = 2 * tmp;
  m[1][2] = 0;
  m[1][3] = (tmp * -(t + b));
  m[2][0] = 0;
  m[2][1] = 0;
  tmp = 1 / (f - n);
  m[2][2] = (-1 * tmp);
  m[2][3] = (-f * tmp);
  m[3][0] = 0;
  m[3][1] = 0;
  m[3][2] = 0;
  m[3][3] = 1;
}
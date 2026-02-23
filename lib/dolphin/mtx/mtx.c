#include <dolphin/mtx.h>

#include <math.h>

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

void C_MTXConcatArray(const Mtx a, const Mtx* srcBase, Mtx* dstBase, u32 count) {
  u32 i;

  ASSERTMSGLINE(580, a != 0, "MTXConcatArray(): NULL MtxPtr 'a' ");
  ASSERTMSGLINE(581, srcBase != 0, "MTXConcatArray(): NULL MtxPtr 'srcBase' ");
  ASSERTMSGLINE(582, dstBase != 0, "MTXConcatArray(): NULL MtxPtr 'dstBase' ");
  ASSERTMSGLINE(583, count > 1, "MTXConcatArray(): count must be greater than 1.");

  for (i = 0; i < count; i++) {
    C_MTXConcat(a, *srcBase, *dstBase);
    srcBase++;
    dstBase++;
  }
}

void C_MTXTranspose(const Mtx src, Mtx xPose) {
  Mtx mTmp;
  MtxPtr m;

  ASSERTMSGLINE(851, src, "MTXTranspose():  NULL MtxPtr 'src' ");
  ASSERTMSGLINE(852, xPose, "MTXTranspose():  NULL MtxPtr 'xPose' ");

  if (src == xPose) {
    m = mTmp;
  } else {
    m = xPose;
  }

  m[0][0] = src[0][0];
  m[0][1] = src[1][0];
  m[0][2] = src[2][0];
  m[0][3] = 0;
  m[1][0] = src[0][1];
  m[1][1] = src[1][1];
  m[1][2] = src[2][1];
  m[1][3] = 0;
  m[2][0] = src[0][2];
  m[2][1] = src[1][2];
  m[2][2] = src[2][2];
  m[2][3] = 0;
  if (m == mTmp) {
    C_MTXCopy(mTmp, xPose);
  }
}

u32 C_MTXInverse(const Mtx src, Mtx inv) {
  Mtx mTmp;
  MtxPtr m;
  f32 det;

  ASSERTMSGLINE(950, src, "MTXInverse():  NULL MtxPtr 'src' ");
  ASSERTMSGLINE(951, inv, "MTXInverse():  NULL MtxPtr 'inv' ");

  if (src == inv) {
    m = mTmp;
  } else {
    m = inv;
  }
  det = ((((src[2][1] * (src[0][2] * src[1][0]))
        + ((src[2][2] * (src[0][0] * src[1][1]))
         + (src[2][0] * (src[0][1] * src[1][2]))))
         - (src[0][2] * (src[2][0] * src[1][1])))
         - (src[2][2] * (src[1][0] * src[0][1])))
         - (src[1][2] * (src[0][0] * src[2][1]));
  if (0 == det) {
    return 0;
  }
  det = 1 / det;
  m[0][0] = (det * +((src[1][1] * src[2][2]) - (src[2][1] * src[1][2])));
  m[0][1] = (det * -((src[0][1] * src[2][2]) - (src[2][1] * src[0][2])));
  m[0][2] = (det * +((src[0][1] * src[1][2]) - (src[1][1] * src[0][2])));

  m[1][0] = (det * -((src[1][0] * src[2][2]) - (src[2][0] * src[1][2])));
  m[1][1] = (det * +((src[0][0] * src[2][2]) - (src[2][0] * src[0][2])));
  m[1][2] = (det * -((src[0][0] * src[1][2]) - (src[1][0] * src[0][2])));

  m[2][0] = (det * +((src[1][0] * src[2][1]) - (src[2][0] * src[1][1])));
  m[2][1] = (det * -((src[0][0] * src[2][1]) - (src[2][0] * src[0][1])));
  m[2][2] = (det * +((src[0][0] * src[1][1]) - (src[1][0] * src[0][1])));

  m[0][3] = ((-m[0][0] * src[0][3]) - (m[0][1] * src[1][3])) - (m[0][2] * src[2][3]);
  m[1][3] = ((-m[1][0] * src[0][3]) - (m[1][1] * src[1][3])) - (m[1][2] * src[2][3]);
  m[2][3] = ((-m[2][0] * src[0][3]) - (m[2][1] * src[1][3])) - (m[2][2] * src[2][3]);

  if (m == mTmp) {
    C_MTXCopy(mTmp, inv);
  }
  return 1;
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

void C_MTXQuat(Mtx m, const Quaternion* q) {
  f32 s;
  f32 xs;
  f32 ys;
  f32 zs;
  f32 wx;
  f32 wy;
  f32 wz;
  f32 xx;
  f32 xy;
  f32 xz;
  f32 yy;
  f32 yz;
  f32 zz;

  ASSERTMSGLINE(2145, m, "MTXQuat():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(2146, q, "MTXQuat():  NULL QuaternionPtr 'q' ");
  ASSERTMSGLINE(2147, q->x || q->y || q->z || q->w, "MTXQuat():  zero-value quaternion ");
  s = 2 / ((q->w * q->w) + ((q->z * q->z) + ((q->x * q->x) + (q->y * q->y))));
  xs = q->x * s;
  ys = q->y * s;
  zs = q->z * s;
  wx = q->w * xs;
  wy = q->w * ys;
  wz = q->w * zs;
  xx = q->x * xs;
  xy = q->x * ys;
  xz = q->x * zs;
  yy = q->y * ys;
  yz = q->y * zs;
  zz = q->z * zs;
  m[0][0] = (1 - (yy + zz));
  m[0][1] = (xy - wz);
  m[0][2] = (xz + wy);
  m[0][3] = 0;
  m[1][0] = (xy + wz);
  m[1][1] = (1 - (xx + zz));
  m[1][2] = (yz - wx);
  m[1][3] = 0;
  m[2][0] = (xz - wy);
  m[2][1] = (yz + wx);
  m[2][2] = (1 - (xx + yy));
  m[2][3] = 0;
}

void C_MTXReflect(Mtx m, const Vec* p, const Vec* n) {
  f32 vxy;
  f32 vxz;
  f32 vyz;
  f32 pdotn;

  vxy = -2 * n->x * n->y;
  vxz = -2 * n->x * n->z;
  vyz = -2 * n->y * n->z;
  pdotn = 2 * C_VECDotProduct(p, n);
  m[0][0] = (1 - (2 * n->x * n->x));
  m[0][1] = vxy;
  m[0][2] = vxz;
  m[0][3] = (pdotn * n->x);
  m[1][0] = vxy;
  m[1][1] = (1 - (2 * n->y * n->y));
  m[1][2] = vyz;
  m[1][3] = (pdotn * n->y);
  m[2][0] = vxz;
  m[2][1] = vyz;
  m[2][2] = (1 - (2 * n->z * n->z));
  m[2][3] = (pdotn * n->z);
}

void C_MTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT) {
  ASSERTMSGLINE(1933, src, "MTXTransApply(): NULL MtxPtr 'src' ");
  ASSERTMSGLINE(1934, dst, "MTXTransApply(): NULL MtxPtr 'src' "); //! wrong assert string

  if (src != dst) {
    dst[0][0] = src[0][0];
    dst[0][1] = src[0][1];
    dst[0][2] = src[0][2];
    dst[1][0] = src[1][0];
    dst[1][1] = src[1][1];
    dst[1][2] = src[1][2];
    dst[2][0] = src[2][0];
    dst[2][1] = src[2][1];
    dst[2][2] = src[2][2];
  }

  dst[0][3] = (src[0][3] + xT);
  dst[1][3] = (src[1][3] + yT);
  dst[2][3] = (src[2][3] + zT);
}

void C_MTXScale(Mtx m, f32 xS, f32 yS, f32 zS) {
  ASSERTMSGLINE(2008, m, "MTXScale():  NULL MtxPtr 'm' ");
  m[0][0] = xS;
  m[0][1] = 0;
  m[0][2] = 0;
  m[0][3] = 0;
  m[1][0] = 0;
  m[1][1] = yS;
  m[1][2] = 0;
  m[1][3] = 0;
  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = zS;
  m[2][3] = 0;
}

void C_MTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS) {
  ASSERTMSGLINE(2070, src, "MTXScaleApply(): NULL MtxPtr 'src' ");
  ASSERTMSGLINE(2071, dst, "MTXScaleApply(): NULL MtxPtr 'dst' ");
  dst[0][0] = (src[0][0] * xS);
  dst[0][1] = (src[0][1] * xS);
  dst[0][2] = (src[0][2] * xS);
  dst[0][3] = (src[0][3] * xS);
  dst[1][0] = (src[1][0] * yS);
  dst[1][1] = (src[1][1] * yS);
  dst[1][2] = (src[1][2] * yS);
  dst[1][3] = (src[1][3] * yS);
  dst[2][0] = (src[2][0] * zS);
  dst[2][1] = (src[2][1] * zS);
  dst[2][2] = (src[2][2] * zS);
  dst[2][3] = (src[2][3] * zS);
}

void C_MTXRotRad(Mtx m, char axis, f32 rad) {
  f32 sinA;
  f32 cosA;

  ASSERTMSGLINE(1447, m, "MTXRotRad():  NULL MtxPtr 'm' ");
  sinA = sinf(rad);
  cosA = cosf(rad);
  C_MTXRotTrig(m, axis, sinA, cosA);
}

void C_MTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA) {
  ASSERTMSGLINE(1502, m, "MTXRotTrig():  NULL MtxPtr 'm' ");
  switch(axis) {
  case 'x':
  case 'X':
    m[0][0] = 1;
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = cosA;
    m[1][2] = -sinA;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = sinA;
    m[2][2] = cosA;
    m[2][3] = 0;
    break;
  case 'y':
  case 'Y':
    m[0][0] = cosA;
    m[0][1] = 0;
    m[0][2] = sinA;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = 1;
    m[1][2] = 0;
    m[1][3] = 0;
    m[2][0] = -sinA;
    m[2][1] = 0;
    m[2][2] = cosA;
    m[2][3] = 0;
    break;
  case 'z':
  case 'Z':
    m[0][0] = cosA;
    m[0][1] = -sinA;
    m[0][2] = 0;
    m[0][3] = 0;
    m[1][0] = sinA;
    m[1][1] = cosA;
    m[1][2] = 0;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = 1;
    m[2][3] = 0;
    break;
  default:
    ASSERTMSGLINE(1529, FALSE, "MTXRotTrig():  invalid 'axis' value ");
    break;
  }
}

void C_MTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) {
  Vec vN;
  f32 s;
  f32 c;
  f32 t;
  f32 x;
  f32 y;
  f32 z;
  f32 xSq;
  f32 ySq;
  f32 zSq;

  ASSERTMSGLINE(1677, m, "MTXRotAxisRad():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(1678, axis, "MTXRotAxisRad():  NULL VecPtr 'axis' ");

  s = sinf(rad);
  c = cosf(rad);
  t = 1 - c;
  C_VECNormalize(axis, &vN);
  x = vN.x;
  y = vN.y;
  z = vN.z;
  xSq = (x * x);
  ySq = (y * y);
  zSq = (z * z);
  m[0][0] = (c + (t * xSq));
  m[0][1] = (y * (t * x)) - (s * z);
  m[0][2] = (z * (t * x)) + (s * y);
  m[0][3] = 0;
  m[1][0] = ((y * (t * x)) + (s * z));
  m[1][1] = (c + (t * ySq));
  m[1][2] = ((z * (t * y)) - (s * x));
  m[1][3] = 0;
  m[2][0] = ((z * (t * x)) - (s * y));
  m[2][1] = ((z * (t * y)) + (s * x));
  m[2][2] = (c + (t * zSq));
  m[2][3] = 0;
}

void C_MTXLookAt(Mtx m, const Point3d* camPos, const Vec* camUp, const Point3d* target) {
  Vec vLook;
  Vec vRight;
  Vec vUp;

  ASSERTMSGLINE(2438, m, "MTXLookAt():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(2439, camPos, "MTXLookAt():  NULL VecPtr 'camPos' ");
  ASSERTMSGLINE(2440, camUp, "MTXLookAt():  NULL VecPtr 'camUp' ");
  ASSERTMSGLINE(2441, target, "MTXLookAt():  NULL Point3dPtr 'target' ");

  vLook.x = camPos->x - target->x;
  vLook.y = camPos->y - target->y;
  vLook.z = camPos->z - target->z;
  VECNormalize(&vLook, &vLook);
  VECCrossProduct(camUp, &vLook, &vRight);
  VECNormalize(&vRight, &vRight);
  VECCrossProduct(&vLook, &vRight, &vUp);
  m[0][0] = vRight.x;
  m[0][1] = vRight.y;
  m[0][2] = vRight.z;
  m[0][3] = -((camPos->z * vRight.z) + ((camPos->x * vRight.x) + (camPos->y * vRight.y)));
  m[1][0] = vUp.x;
  m[1][1] = vUp.y;
  m[1][2] = vUp.z;
  m[1][3] = -((camPos->z * vUp.z) + ((camPos->x * vUp.x) + (camPos->y * vUp.y)));
  m[2][0] = vLook.x;
  m[2][1] = vLook.y;
  m[2][2] = vLook.z;
  m[2][3] = -((camPos->z * vLook.z) + ((camPos->x * vLook.x) + (camPos->y * vLook.y)));
}

void C_MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
  f32 tmp;

  ASSERTMSGLINE(2541, m, "MTXLightFrustum():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(2542, (t != b), "MTXLightFrustum():  't' and 'b' clipping planes are equal ");
  ASSERTMSGLINE(2543, (l != r), "MTXLightFrustum():  'l' and 'r' clipping planes are equal ");

  tmp = 1 / (r - l);
  m[0][0] = (scaleS * (2 * n * tmp));
  m[0][1] = 0;
  m[0][2] = (scaleS * (tmp * (r + l))) - transS;
  m[0][3] = 0;
  tmp = 1 / (t - b);
  m[1][0] = 0;
  m[1][1] = (scaleT * (2 * n * tmp));
  m[1][2] = (scaleT * (tmp * (t + b))) - transT;
  m[1][3] = 0;
  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = -1;
  m[2][3] = 0;
}

void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
  f32 angle;
  f32 cot;

  ASSERTMSGLINE(2605, m, "MTXLightPerspective():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(2606, (fovY > 0.0) && (fovY < 180.0), "MTXLightPerspective():  'fovY' out of range ");
  ASSERTMSGLINE(2607, 0 != aspect, "MTXLightPerspective():  'aspect' is 0 ");

  angle = (0.5f * fovY);
  angle = MTXDegToRad(angle);
  cot = 1 / tanf(angle);
  m[0][0] = (scaleS * (cot / aspect));
  m[0][1] = 0;
  m[0][2] = -transS;
  m[0][3] = 0;
  m[1][0] = 0;
  m[1][1] = (cot * scaleT);
  m[1][2] = -transT;
  m[1][3] = 0;
  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = -1;
  m[2][3] = 0;
}

void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
  f32 tmp;

  ASSERTMSGLINE(2673, m, "MTXLightOrtho():  NULL MtxPtr 'm' ");
  ASSERTMSGLINE(2674, (t != b), "MTXLightOrtho():  't' and 'b' clipping planes are equal ");
  ASSERTMSGLINE(2675, (l != r), "MTXLightOrtho():  'l' and 'r' clipping planes are equal ");
  tmp = 1 / (r - l);
  m[0][0] = (2 * tmp * scaleS);
  m[0][1] = 0;
  m[0][2] = 0;
  m[0][3] = (transS + (scaleS * (tmp * -(r + l))));
  tmp = 1/ (t - b);
  m[1][0] = 0;
  m[1][1] = (2 * tmp * scaleT);
  m[1][2] = 0;
  m[1][3] = (transT + (scaleT * (tmp * -(t + b))));
  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = 0;
  m[2][3] = 1;
}

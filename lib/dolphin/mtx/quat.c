#include <assert.h>
#include <math.h>
#include <dolphin/mtx.h>

void C_QUATAdd(const Quaternion* p, const Quaternion* q, Quaternion* r) {
  assert(p && "QUATAdd():  NULL QuaternionPtr 'p' ");
  assert(q && "QUATAdd():  NULL QuaternionPtr 'q' ");
  assert(r && "QUATAdd():  NULL QuaternionPtr 'r' ");

  r->x = p->x + q->x;
  r->y = p->y + q->y;
  r->z = p->z + q->z;
  r->w = p->w + q->w;
}

void C_QUATSubtract(const Quaternion* p, const Quaternion* q, Quaternion* r) {
  assert(p && "QUATSubtract():  NULL QuaternionPtr 'p' ");
  assert(q && "QUATSubtract():  NULL QuaternionPtr 'q' ");
  assert(r && "QUATSubtract():  NULL QuaternionPtr 'r' ");

  r->x = p->x - q->x;
  r->y = p->y - q->y;
  r->z = p->z - q->z;
  r->w = p->w - q->w;
}

void C_QUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) {
  Quaternion* r;
  Quaternion pqTmp;

  assert(p && "QUATMultiply():  NULL QuaternionPtr 'p' ");
  assert(q && "QUATMultiply():  NULL QuaternionPtr 'q' ");
  assert(pq && "QUATMultiply():  NULL QuaternionPtr 'pq' ");

  if (p == pq || q == pq){
    r = &pqTmp;
  } else {
    r = pq;
  }

  r->w = (p->w * q->w) - (p->x * q->x) - (p->y * q->y) - (p->z * q->z);
  r->x = (p->w * q->x) + (p->x * q->w) + (p->y * q->z) - (p->z * q->y);
  r->y = (p->w * q->y) + (p->y * q->w) + (p->z * q->x) - (p->x * q->z);
  r->z = (p->w * q->z) + (p->z * q->w) + (p->x * q->y) - (p->y * q->x);

  if (r == &pqTmp) {
    *pq = pqTmp;
  }
}

void C_QUATDivide(const Quaternion* p, const Quaternion* q, Quaternion* r) {
  Quaternion qtmp;
  assert(p && "QUATDivide():  NULL QuaternionPtr 'p' ");
  assert(q && "QUATDivide():  NULL QuaternionPtr 'q' ");
  assert(r && "QUATDivide():  NULL QuaternionPtr 'r' ");

  C_QUATInverse(q, &qtmp);
  C_QUATMultiply(&qtmp, p, r);
}

void C_QUATScale(const Quaternion* q, Quaternion* r, f32 scale) {
  assert(q && "QUATScale():  NULL QuaternionPtr 'q' ");
  assert(r && "QUATScale():  NULL QuaternionPtr 'r' ");

  r->x = q->x * scale;
  r->y = q->y * scale;
  r->z = q->z * scale;
  r->w = q->w * scale;
}

f32 C_QUATDotProduct(const Quaternion* p, const Quaternion* q) {
  assert(p && "QUATDotProduct():  NULL QuaternionPtr 'p' ");
  assert(q && "QUATDotProduct():  NULL QuaternionPtr 'q' ");

  return (q->x * p->x) + (q->y * p->y) + (q->z * p->z) + (q->w * p->w);
}

void C_QUATNormalize(const Quaternion* src, Quaternion* unit) {
  f32 mag;
  assert(src && "QUATNormalize():  NULL QuaternionPtr 'src' ");
  assert(unit && "QUATNormalize():  NULL QuaternionPtr 'unit' ");

  mag = (src->x * src->x) + (src->y * src->y) + (src->z * src->z) + (src->w * src->w);
  if (mag >= 0.00001f) {
    mag = 1.0f / sqrtf(mag);

    unit->x = src->x * mag;
    unit->y = src->y * mag;
    unit->z = src->z * mag;
    unit->w = src->w * mag;
  } else {
    unit->x = unit->y = unit->z = unit->w = 0.0f;
  }
}

void C_QUATInverse(const Quaternion* src, Quaternion* inv) {
  f32 mag, norminv;
  assert(src && "QUATInverse():  NULL QuaternionPtr 'src' ");
  assert(inv && "QUATInverse():  NULL QuaternionPtr 'inv' ");

  mag = (src->x * src->x) + (src->y * src->y) + (src->z * src->z) + (src->w * src->w);
  if (mag == 0.0f) {
    mag = 1.0f;
  }

  norminv = 1.0f / mag;
  inv->x = -src->x * norminv;
  inv->y = -src->y * norminv;
  inv->z = -src->z * norminv;
  inv->w =  src->w * norminv;
}

void C_QUATExp(const Quaternion* q, Quaternion* r)  {
  f32 theta, scale;
  assert(q && "QUATExp():  NULL QuaternionPtr 'q' ");
  assert(r && "QUATExp():  NULL QuaternionPtr 'r' ");
  assert(q->w == 0.0f && "QUATExp():  'q' is not a pure quaternion. ");

  theta = sqrtf((q->x * q->x) + (q->y * q->y) + (q->z * q->z));
  scale = 1.0f;

  if (theta > 0.00001f) {
    scale = sinf(theta) / theta;
  }

  r->x = scale * q->x;
  r->y = scale * q->y;
  r->z = scale * q->z;
  r->w = cosf(theta);
}

void C_QUATLogN(const Quaternion* q, Quaternion* r) {
  f32 theta, scale;
  assert(q && "QUATLogN():  NULL QuaternionPtr 'q' ");
  assert(r && "QUATLogN():  NULL QuaternionPtr 'r' ");

  scale = (q->x * q->x) + (q->y * q->y) + (q->z * q->z);

  scale = sqrtf(scale);
  theta = atan2f(scale, q->w);

  if (scale > 0.0f) {
    scale = theta / scale;
  }

  r->x = scale * q->x;
  r->y = scale * q->y;
  r->z = scale * q->z;
  r->w = 0.0f;
}


void C_QUATMakeClosest(const Quaternion* q, const Quaternion* qto, Quaternion* r) {
  f32 dot;
  assert(q && "QUATMakeClosest():  NULL QuaternionPtr 'q' ");
  assert(qto && "QUATMakeClosest():  NULL QuaternionPtr 'qto' ");
  assert(r && "QUATMakeClosest():  NULL QuaternionPtr 'r' ");

  dot = (q->x * qto->x) + (q->y * qto->y) + (q->z * qto->z) + (q->w * qto->w);
  if (dot < 0.0f) {
    r->x = -q->x;
    r->y = -q->y;
    r->z = -q->z;
    r->w = -q->w;
  } else {
    *r = *q;
  }
}

void C_QUATRotAxisRad(Quaternion* r, const Vec* axis, f32 rad) {
  f32 half, sh, ch;
  Vec nAxis;

  assert(r && "QUATRotAxisRad():  NULL QuaternionPtr 'r' ");
  assert(axis && "QUATRotAxisRad():  NULL VecPtr 'axis' ");

  VECNormalize(axis, &nAxis);

  half = rad * 0.5f;
  sh = sinf(half);
  ch = cosf(half);

  r->x = sh * nAxis.x;
  r->y = sh * nAxis.y;
  r->z = sh * nAxis.z;
  r->w = ch;
}

void C_QUATMtx(Quaternion* r, const Mtx m) {
  f32 tr,s;
  s32 i, j, k;
  s32 nxt[3] = {1, 2, 0};
  f32 q[3];

  assert(r && "QUATMtx():  NULL QuaternionPtr 'r' ");
  assert(m && "QUATMtx():  NULL MtxPtr 'm' ");

  tr = m[0][0] + m[1][1] + m[2][2];
  if (tr > 0.0f) {
    s = sqrtf(tr + 1.0f);
    r->w = s * 0.5f;
    s = 0.5f / s;

    r->x = (m[2][1] - m[1][2]) * s;
    r->y = (m[0][2] - m[2][0]) * s;
    r->z = (m[1][0] - m[0][1]) * s;
  } else  {
    i = 0;
    if (m[1][1] > m[0][0]) {
      i = 1;
    }

    if (m[2][2] > m[i][i]) {
      i = 2;
    }

    j = nxt[i];
    k = nxt[j];

    s = sqrtf((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
    q[i] = s * 0.5f;

    if (s != 0.0f) {
      s = 0.5f / s;
    }

    r->w = (m[k][j] - m[j][k]) * s;
    q[j] = (m[i][j] + m[j][i]) * s;
    q[k] = (m[i][k] + m[k][i]) * s;

    r->x = q[0];
    r->y = q[1];
    r->z = q[2];
  }
}

void C_QUATLerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
  assert(p && "QUATLerp():  NULL QuaternionPtr 'p' ");
  assert(q && "QUATLerp():  NULL QuaternionPtr 'q' ");
  assert(r && "QUATLerp():  NULL QuaternionPtr 'r' ");

  r->x = t * (q->x - p->x) + p->x;
  r->y = t * (q->y - p->y) + p->y;
  r->z = t * (q->z - p->z) + p->z;
  r->w = t * (q->w - p->w) + p->w;
}

void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
    f32 theta, sin_th, cos_th;
    f32 tp, tq;

    assert(p && "QUATSlerp():  NULL QuaternionPtr 'p' ");
    assert(q && "QUATSlerp():  NULL QuaternionPtr 'q' ");
    assert(r && "QUATSlerp():  NULL QuaternionPtr 'r' ");

    cos_th = p->x * q->x + p->y * q->y + p->z * q->z + p->w * q->w;
    tq = 1.0f;

    if (cos_th < 0.0f) {
        cos_th = -cos_th;
        tq = -tq;
    }

    if (cos_th <= 0.99999f) {
        theta = acosf(cos_th);
        sin_th = sinf(theta);

        tp = sinf((1.0f - t) * theta) / sin_th;
        tq *= sinf(t * theta) / sin_th;
    } else {
        tp = 1.0f - t;
        tq *= t;
    }

    r->x = (tp * p->x) + (tq * q->x);
    r->y = (tp * p->y) + (tq * q->y);
    r->z = (tp * p->z) + (tq * q->z);
    r->w = (tp * p->w) + (tq * q->w);
}

void C_QUATSquad(const Quaternion* p, const Quaternion* a, const Quaternion* b, const Quaternion* q, Quaternion* r, f32 t) {
    Quaternion pq, ab;
    f32 t2;

    assert(p && "QUATSquad():  NULL QuaternionPtr 'p' ");
    assert(a && "QUATSquad():  NULL QuaternionPtr 'a' ");
    assert(b && "QUATSquad():  NULL QuaternionPtr 'b' ");
    assert(q && "QUATSquad():  NULL QuaternionPtr 'q' ");
    assert(r && "QUATSquad():  NULL QuaternionPtr 'r' ");

    t2 = 2.0f * t * (1.0f - t);
    C_QUATSlerp(p, q, &pq, t);
    C_QUATSlerp(a, b, &ab, t);
    C_QUATSlerp(&pq, &ab, r, t2);
}

void C_QUATCompA(const Quaternion* qprev, const Quaternion* q, const Quaternion* qnext, Quaternion* a) {
    Quaternion qm, qp, lqm, lqp, qpqm, exq;

    assert(qprev && "QUATCompA():  NULL QuaternionPtr 'qprev' ");
    assert(q && "QUATCompA():  NULL QuaternionPtr 'q' ");
    assert(qnext && "QUATCompA():  NULL QuaternionPtr 'qnext' ");
    assert(a && "QUATCompA():  NULL QuaternionPtr 'a' ");

    C_QUATDivide(qprev, q, &qm);
    C_QUATLogN(&qm, &lqm);
    C_QUATDivide(qnext, q, &qp);
    C_QUATLogN(&qp, &lqp);
    C_QUATAdd(&lqp, &lqm, &qpqm);
    C_QUATScale(&qpqm, &qpqm, -0.25f);
    C_QUATExp(&qpqm, &exq);
    C_QUATMultiply(q, &exq, a);
}

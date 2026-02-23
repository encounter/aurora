#include <assert.h>
#include <math.h>
#include <dolphin/mtx/mtx44ext.h>

void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
  f32 angle;
  f32 cot;
  f32 tmp;

  assert(m && "MTXPerspective():  NULL Mtx44Ptr 'm' ");
  assert((fovY > 0.0) && (fovY < 180.0) && "MTXPerspective():  'fovY' out of range ");
  assert(0.0f != aspect && "MTXPerspective():  'aspect' is 0 ");

  angle = (0.5f * fovY);
  angle = MTXDegToRad(angle);
  cot = 1 / tanf(angle);
  m[0][0] = (cot / aspect);
  m[0][1] = 0;
  m[0][2] = 0;
  m[0][3] = 0;
  m[1][0] = 0;
  m[1][1] = (cot);
  m[1][2] = 0;
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
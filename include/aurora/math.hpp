#pragma once

#include <cstddef>
#include <cstdint>

#ifndef AURORA_VEC2_EXTRA
#define AURORA_VEC2_EXTRA
#endif
#ifndef AURORA_VEC3_EXTRA
#define AURORA_VEC3_EXTRA
#endif
#ifndef AURORA_VEC4_EXTRA
#define AURORA_VEC4_EXTRA
#endif
#ifndef AURORA_MAT4X4_EXTRA
#define AURORA_MAT4X4_EXTRA
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
#if __has_attribute(vector_size)
//#define USE_GCC_VECTOR_EXTENSIONS
#endif

namespace aurora {
template <typename T>
struct Vec2 {
  T x{};
  T y{};

  constexpr Vec2() = default;
  constexpr Vec2(T x, T y) : x(x), y(y) {}
  AURORA_VEC2_EXTRA
#ifdef METAFORCE
  constexpr Vec2(const zeus::CVector2f& vec) : x(vec.x()), y(vec.y()) {}
#endif

  bool operator==(const Vec2& rhs) const { return x == rhs.x && y == rhs.y; }
  bool operator!=(const Vec2& rhs) const { return !(*this == rhs); }
};
template <typename T>
struct Vec3 {
  T x{};
  T y{};
  T z{};

  constexpr Vec3() = default;
  constexpr Vec3(T x, T y, T z) : x(x), y(y), z(z) {}
  AURORA_VEC3_EXTRA
#ifdef METAFORCE
  constexpr Vec3(const zeus::CVector3f& vec) : x(vec.x()), y(vec.y()), z(vec.z()) {}
  operator zeus::CVector3f() const { return {x, y, z}; }
#endif

  bool operator==(const Vec3& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
  bool operator!=(const Vec3& rhs) const { return !(*this == rhs); }
};
template <typename T>
struct Vec4 {
#ifdef USE_GCC_VECTOR_EXTENSIONS
  typedef T Vt __attribute__((vector_size(sizeof(T) * 4)));
  Vt m;
#else
  using Vt = T[4];
  Vt m;
#endif

  constexpr Vec4() = default;
  constexpr Vec4(Vt m) : m(m) {}
  constexpr Vec4(T x, T y, T z, T w) : m{x, y, z, w} {}
  // For Vec3 with padding
  constexpr Vec4(T x, T y, T z) : m{x, y, z, {}} {}
  // For Vec3 -> Vec4
  constexpr Vec4(Vec3<T> v, T w) : m{v.x, v.y, v.z, w} {}
  AURORA_VEC4_EXTRA
#ifdef METAFORCE
  constexpr Vec4(const zeus::CVector4f& vec) : x(vec.x()), y(vec.y()), z(vec.z()), w(vec.w()) {}
  constexpr Vec4(const zeus::CColor& color) : x(color.r()), y(color.g()), z(color.b()), w(color.a()) {}
#endif

  inline Vec4& operator=(const Vec4& other) {
    memcpy(&m, &other.m, sizeof(Vt));
    return *this;
  }

  [[nodiscard]] inline T& x() { return m[0]; }
  [[nodiscard]] inline T x() const { return m[0]; }
  [[nodiscard]] inline T& y() { return m[1]; }
  [[nodiscard]] inline T y() const { return m[1]; }
  [[nodiscard]] inline T& z() { return m[2]; }
  [[nodiscard]] inline T z() const { return m[2]; }
  [[nodiscard]] inline T& w() { return m[3]; }
  [[nodiscard]] inline T w() const { return m[3]; }
  [[nodiscard]] inline T& operator[](size_t i) { return m[i]; }
  [[nodiscard]] inline T operator[](size_t i) const { return m[i]; }

  template <size_t x, size_t y, size_t z, size_t w>
  [[nodiscard]] constexpr Vec4 shuffle() const {
    static_assert(x < 4 && y < 4 && z < 4 && w < 4);
#if defined(USE_GCC_VECTOR_EXTENSIONS) && __has_builtin(__builtin_shuffle)
    typedef int Vi __attribute__((vector_size(16)));
    return __builtin_shuffle(m, Vi{x, y, z, w});
#else
    return {m[x], m[y], m[z], m[w]};
#endif
  }

  bool operator==(const Vec4& rhs) const {
#if defined(USE_GCC_VECTOR_EXTENSIONS) && __has_builtin(__builtin_reduce_and)
    return __builtin_reduce_and(m == rhs.m) != 0;
#else
    return m[0] == rhs.m[0] && m[1] == rhs.m[1] && m[2] == rhs.m[2] && m[3] == rhs.m[3];
#endif
  }
  bool operator!=(const Vec4& rhs) const { return !(*this == rhs); }
};
template <typename T>
[[nodiscard]] inline Vec4<T> operator+(const Vec4<T>& a, const Vec4<T>& b) {
#ifdef USE_GCC_VECTOR_EXTENSIONS
  return a.m + b.m;
#else
  return {a.m[0] + b.m[0], a.m[1] + b.m[1], a.m[2] + b.m[2], a.m[3] + b.m[3]};
#endif
}
template <typename T>
[[nodiscard]] inline Vec4<T> operator*(const Vec4<T>& a, const Vec4<T>& b) {
#ifdef USE_GCC_VECTOR_EXTENSIONS
  return a.m * b.m;
#else
  return {a.m[0] * b.m[0], a.m[1] * b.m[1], a.m[2] * b.m[2], a.m[3] * b.m[3]};
#endif
}
template <typename T>
struct Mat3x2 {
  Vec2<T> m0{};
  Vec2<T> m1{};
  Vec2<T> m2{};

  constexpr Mat3x2() = default;
  constexpr Mat3x2(const Vec2<T>& m0, const Vec2<T>& m1, const Vec2<T>& m2) : m0(m0), m1(m1), m2(m2) {}

  bool operator==(const Mat3x2& rhs) const { return m0 == rhs.m0 && m1 == rhs.m1 && m2 == rhs.m2; }
  bool operator!=(const Mat3x2& rhs) const { return !(*this == rhs); }
};
template <typename T>
struct Mat4x2 {
  Vec2<T> m0{};
  Vec2<T> m1{};
  Vec2<T> m2{};
  Vec2<T> m3{};

  constexpr Mat4x2() = default;
  constexpr Mat4x2(const Vec2<T>& m0, const Vec2<T>& m1, const Vec2<T>& m2, const Vec2<T>& m3)
  : m0(m0), m1(m1), m2(m2), m3(m3) {}

  inline Mat4x2 transpose() const {
    return {
        {m0.x, m2.x},
        {m0.y, m2.y},
        {m1.x, m3.x},
        {m1.y, m3.y},
    };
  }

  bool operator==(const Mat4x2& rhs) const { return m0 == rhs.m0 && m1 == rhs.m1 && m2 == rhs.m2 && m3 == rhs.m3; }
  bool operator!=(const Mat4x2& rhs) const { return !(*this == rhs); }
};
template <typename T>
struct Mat4x4;
template <typename T>
struct Mat3x4 {
  Vec4<T> m0{};
  Vec4<T> m1{};
  Vec4<T> m2{};

  constexpr Mat3x4() = default;
  constexpr Mat3x4(const Vec4<T>& m0, const Vec4<T>& m1, const Vec4<T>& m2) : m0(m0), m1(m1), m2(m2) {}

  inline Mat4x4<T> to4x4() const;
  inline Mat4x4<T> toTransposed4x4() const;
};
static_assert(sizeof(Mat3x4<float>) == sizeof(float[3][4]));
template <typename T>
struct Mat4x4 {
  Vec4<T> m0{};
  Vec4<T> m1{};
  Vec4<T> m2{};
  Vec4<T> m3{};

  constexpr Mat4x4() = default;
  constexpr Mat4x4(const Vec4<T>& m0, const Vec4<T>& m1, const Vec4<T>& m2, const Vec4<T>& m3)
  : m0(m0), m1(m1), m2(m2), m3(m3) {}
  AURORA_MAT4X4_EXTRA
#ifdef METAFORCE
  constexpr Mat4x4(const zeus::CMatrix4f& m) : m0(m[0]), m1(m[1]), m2(m[2]), m3(m[3]) {}
  constexpr Mat4x4(const zeus::CTransform& m) : Mat4x4(m.toMatrix4f()) {}
#endif

  [[nodiscard]] Mat4x4 transpose() const {
    return {
        {m0[0], m1[0], m2[0], m3[0]},
        {m0[1], m1[1], m2[1], m3[1]},
        {m0[2], m1[2], m2[2], m3[2]},
        {m0[3], m1[3], m2[3], m3[3]},
    };
  }
  inline Mat4x4& operator=(const Mat4x4& other) {
    m0 = other.m0;
    m1 = other.m1;
    m2 = other.m2;
    m3 = other.m3;
    return *this;
  }

  inline Vec4<T>& operator[](size_t i) { return *(&m0 + i); }
  inline const Vec4<T>& operator[](size_t i) const { return *(&m0 + i); }

  bool operator==(const Mat4x4& rhs) const { return m0 == rhs.m0 && m1 == rhs.m1 && m2 == rhs.m2 && m3 == rhs.m3; }
  bool operator!=(const Mat4x4& rhs) const { return !(*this == rhs); }
};
static_assert(sizeof(Mat4x4<float>) == sizeof(float[4][4]));
template <typename T>
[[nodiscard]] inline Mat4x4<T> operator*(const Mat4x4<T>& a, const Mat4x4<T>& b) {
  Mat4x4<T> out;
  for (size_t i = 0; i < 4; ++i) {
    *(&out.m0 + i) = a.m0 * b[i].template shuffle<0, 0, 0, 0>() + a.m1 * b[i].template shuffle<1, 1, 1, 1>() +
                     a.m2 * b[i].template shuffle<2, 2, 2, 2>() + a.m3 * b[i].template shuffle<3, 3, 3, 3>();
  }
  return out;
}
template <typename T>
[[nodiscard]] inline Mat4x4<T> Mat3x4<T>::to4x4() const {
  return {
      {m0.m[0], m0.m[1], m0.m[2], 0.f},
      {m1.m[0], m1.m[1], m1.m[2], 0.f},
      {m2.m[0], m2.m[1], m2.m[2], 0.f},
      {m0.m[3], m1.m[3], m2.m[3], 1.f},
  };
}
template <typename T>
[[nodiscard]] inline Mat4x4<T> Mat3x4<T>::toTransposed4x4() const {
  return Mat4x4<T>{
      m0,
      m1,
      m2,
      {0.f, 0.f, 0.f, 1.f},
  }
      .transpose();
}
constexpr Mat4x4<float> Mat4x4_Identity{
    Vec4<float>{1.f, 0.f, 0.f, 0.f},
    Vec4<float>{0.f, 1.f, 0.f, 0.f},
    Vec4<float>{0.f, 0.f, 1.f, 0.f},
    Vec4<float>{0.f, 0.f, 0.f, 1.f},
};
} // namespace aurora

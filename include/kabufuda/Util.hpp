#pragma once

#ifndef _WIN32
#include <cerrno>
#include <cstdlib>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#undef bswap16
#undef bswap32
#undef bswap64

#ifndef ENABLE_BITWISE_ENUM
#define ENABLE_BITWISE_ENUM(type)                                                                                      \
  constexpr type operator|(type a, type b) {                                                                           \
    using T = std::underlying_type_t<type>;                                                                            \
    return type(static_cast<T>(a) | static_cast<T>(b));                                                                \
  }                                                                                                                    \
  constexpr type operator&(type a, type b) {                                                                           \
    using T = std::underlying_type_t<type>;                                                                            \
    return type(static_cast<T>(a) & static_cast<T>(b));                                                                \
  }                                                                                                                    \
  constexpr type& operator|=(type& a, type b) {                                                                        \
    using T = std::underlying_type_t<type>;                                                                            \
    a = type(static_cast<T>(a) | static_cast<T>(b));                                                                   \
    return a;                                                                                                          \
  }                                                                                                                    \
  constexpr type& operator&=(type& a, type b) {                                                                        \
    using T = std::underlying_type_t<type>;                                                                            \
    a = type(static_cast<T>(a) & static_cast<T>(b));                                                                   \
    return a;                                                                                                          \
  }                                                                                                                    \
  constexpr type operator~(type key) {                                                                                 \
    using T = std::underlying_type_t<type>;                                                                            \
    return type(~static_cast<T>(key));                                                                                 \
  }                                                                                                                    \
  constexpr bool True(type key) {                                                                                      \
    using T = std::underlying_type_t<type>;                                                                            \
    return static_cast<T>(key) != 0;                                                                                   \
  }                                                                                                                    \
  constexpr bool False(type key) {                                                                                     \
    using T = std::underlying_type_t<type>;                                                                            \
    return static_cast<T>(key) == 0;                                                                                   \
  }
#endif

namespace kabufuda {

/* Type-sensitive byte swappers */
template <typename T>
constexpr T bswap16(T val) {
#if __GNUC__
  return __builtin_bswap16(val);
#elif _WIN32
  return _byteswap_ushort(val);
#else
  return (val = (val << 8) | ((val >> 8) & 0xFF));
#endif
}

template <typename T>
constexpr T bswap32(T val) {
#if __GNUC__
  return __builtin_bswap32(val);
#elif _WIN32
  return _byteswap_ulong(val);
#else
  val = (val & 0x0000FFFF) << 16 | (val & 0xFFFF0000) >> 16;
  val = (val & 0x00FF00FF) << 8 | (val & 0xFF00FF00) >> 8;
  return val;
#endif
}

template <typename T>
constexpr T bswap64(T val) {
#if __GNUC__
  return __builtin_bswap64(val);
#elif _WIN32
  return _byteswap_uint64(val);
#else
  return ((val & 0xFF00000000000000ULL) >> 56) | ((val & 0x00FF000000000000ULL) >> 40) |
         ((val & 0x0000FF0000000000ULL) >> 24) | ((val & 0x000000FF00000000ULL) >> 8) |
         ((val & 0x00000000FF000000ULL) << 8) | ((val & 0x0000000000FF0000ULL) << 24) |
         ((val & 0x000000000000FF00ULL) << 40) | ((val & 0x00000000000000FFULL) << 56);
#endif
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr int16_t SBig(int16_t val) { return bswap16(val); }
constexpr uint16_t SBig(uint16_t val) { return bswap16(val); }
constexpr int32_t SBig(int32_t val) { return bswap32(val); }
constexpr uint32_t SBig(uint32_t val) { return bswap32(val); }
constexpr int64_t SBig(int64_t val) { return bswap64(val); }
constexpr uint64_t SBig(uint64_t val) { return bswap64(val); }
constexpr float SBig(float val) {
  union { float f; int32_t i; } uval1 = {val};
  union { int32_t i; float f; } uval2 = {bswap32(uval1.i)};
  return uval2.f;
}
constexpr double SBig(double val) {
  union { double f; int64_t i; } uval1 = {val};
  union { int64_t i; double f; } uval2 = {bswap64(uval1.i)};
  return uval2.f;
}
#ifndef SBIG
#define SBIG(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

constexpr int16_t SLittle(int16_t val) { return val; }
constexpr uint16_t SLittle(uint16_t val) { return val; }
constexpr int32_t SLittle(int32_t val) { return val; }
constexpr uint32_t SLittle(uint32_t val) { return val; }
constexpr int64_t SLittle(int64_t val) { return val; }
constexpr uint64_t SLittle(uint64_t val) { return val; }
constexpr float SLittle(float val) { return val; }
constexpr double SLittle(double val) { return val; }
#ifndef SLITTLE
#define SLITTLE(q) (q)
#endif
#else
constexpr int16_t SLittle(int16_t val) { return bswap16(val); }
constexpr uint16_t SLittle(uint16_t val) { return bswap16(val); }
constexpr int32_t SLittle(int32_t val) { return bswap32(val); }
constexpr uint32_t SLittle(uint32_t val) { return bswap32(val); }
constexpr int64_t SLittle(int64_t val) { return bswap64(val); }
constexpr uint64_t SLittle(uint64_t val) { return bswap64(val); }
constexpr float SLittle(float val) {
  int32_t ival = bswap32(*((int32_t*)(&val)));
  return *((float*)(&ival));
}
constexpr double SLittle(double val) {
  int64_t ival = bswap64(*((int64_t*)(&val)));
  return *((double*)(&ival));
}
#ifndef SLITTLE
#define SLITTLE(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

constexpr int16_t SBig(int16_t val) { return val; }
constexpr uint16_t SBig(uint16_t val) { return val; }
constexpr int32_t SBig(int32_t val) { return val; }
constexpr uint32_t SBig(uint32_t val) { return val; }
constexpr int64_t SBig(int64_t val) { return val; }
constexpr uint64_t SBig(uint64_t val) { return val; }
constexpr float SBig(float val) { return val; }
constexpr double SBig(double val) { return val; }
#ifndef SBIG
#define SBIG(q) (q)
#endif
#endif

#if _WIN32
using Sstat = struct ::_stat64;
#else
using Sstat = struct stat;
#endif

uint64_t getGCTime();

#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m)&S_IFMT) == S_IFREG)
#endif

#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#define S_ISDIR(m) (((m)&S_IFMT) == S_IFDIR)
#endif

#if _WIN32
int Stat(const char* path, Sstat* statOut);
#else
inline int Stat(const char* path, Sstat* statOut) { return stat(path, statOut); }
#endif

/**
 * @brief calculateChecksum
 * @param data
 * @param len
 * @param checksum
 * @param checksumInv
 */
void calculateChecksumBE(const uint16_t* data, size_t len, uint16_t* checksum, uint16_t* checksumInv);

#undef NOFILE

enum class ECardResult {
  CRC_MISMATCH = -1003, /* Extension enum for Retro's CRC check */
  FATAL_ERROR = -128,
  ENCODING = -13,
  NAMETOOLONG = -12,
  INSSPACE = -9,
  NOENT = -8,
  EXIST = -7,
  BROKEN = -6,
  IOERROR = -5,
  NOFILE = -4,
  NOCARD = -3,
  WRONGDEVICE = -2,
  BUSY = -1,
  READY = 0
};
} // namespace kabufuda

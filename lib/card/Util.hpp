#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

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

#define ROUND_UP_8192(val) (((val) + 8191) & ~8191)

namespace aurora::card {

uint64_t getGCTime();

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
} // namespace aurora::card

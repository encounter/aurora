#include "readback.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <tmmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

namespace aurora::gfx::readback {

void copy_bgra_to_rgba(uint8_t* __restrict dst, const uint8_t* __restrict src, size_t pixels) noexcept {
  constexpr uint32_t low = std::endian::native == std::endian::little ? 0x000000ffu : 0x0000ff00u;
  for (size_t x = 0; x < pixels; ++x) {
    uint32_t pixel;
    std::memcpy(&pixel, src + x * 4, 4);
    const uint32_t swap = ((pixel >> 16) ^ pixel) & low;
    pixel ^= swap ^ (swap << 16);
    std::memcpy(dst + x * 4, &pixel, 4);
  }
}

namespace {

using CopyRow = void (*)(uint8_t*, const uint8_t*, size_t) noexcept;

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("ssse3"), noinline))
#else
__declspec(noinline)
#endif
void copy_bgra_ssse3(uint8_t* dst, const uint8_t* src, size_t pixels) noexcept {
  const auto mask = _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
  size_t x = 0;
  for (; pixels - x >= 16; x += 16) {
    const auto a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x * 4));
    const auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x * 4 + 16));
    const auto c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x * 4 + 32));
    const auto d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x * 4 + 48));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4), _mm_shuffle_epi8(a, mask));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4 + 16), _mm_shuffle_epi8(b, mask));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4 + 32), _mm_shuffle_epi8(c, mask));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4 + 48), _mm_shuffle_epi8(d, mask));
  }

  for (; pixels - x >= 4; x += 4) {
    const auto p = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x * 4));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4), _mm_shuffle_epi8(p, mask));
  }
  copy_bgra_to_rgba(dst + x * 4, src + x * 4, pixels - x);
}

CopyRow x86_copy_fn() noexcept {
#if defined(_MSC_VER)
  int features[4];
  __cpuid(features, 1);
  const bool ssse3 = (features[2] & (1 << 9)) != 0;
#else
  const bool ssse3 = __builtin_cpu_supports("ssse3");
#endif
  return ssse3 ? copy_bgra_ssse3 : copy_bgra_to_rgba;
}
#elif defined(__aarch64__) || defined(_M_ARM64)
void copy_bgra_neon(uint8_t* dst, const uint8_t* src, size_t pixels) noexcept {
  static constexpr uint8_t indices[16]{2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15};
  const auto mask = vld1q_u8(indices);
  size_t x = 0;
  for (; pixels - x >= 16; x += 16) {
    const auto a = vld1q_u8(src + x * 4);
    const auto b = vld1q_u8(src + x * 4 + 16);
    const auto c = vld1q_u8(src + x * 4 + 32);
    const auto d = vld1q_u8(src + x * 4 + 48);
    vst1q_u8(dst + x * 4, vqtbl1q_u8(a, mask));
    vst1q_u8(dst + x * 4 + 16, vqtbl1q_u8(b, mask));
    vst1q_u8(dst + x * 4 + 32, vqtbl1q_u8(c, mask));
    vst1q_u8(dst + x * 4 + 48, vqtbl1q_u8(d, mask));
  }

  for (; pixels - x >= 4; x += 4) {
    vst1q_u8(dst + x * 4, vqtbl1q_u8(vld1q_u8(src + x * 4), mask));
  }
  copy_bgra_to_rgba(dst + x * 4, src + x * 4, pixels - x);
}
#endif

} // namespace

void copy_color_rows(uint8_t* dst, const uint8_t* src, size_t width, size_t height, size_t rowPitch,
                     bool bgra) noexcept {
  if (width == 0 || height == 0) {
    return;
  }

  const size_t rowBytes = width * 4;
  if (!bgra) {
    if (rowPitch == rowBytes) {
      std::memcpy(dst, src, rowBytes * height);
    } else {
      for (size_t y = 0; y < height; ++y) {
        std::memcpy(dst + y * rowBytes, src + y * rowPitch, rowBytes);
      }
    }
    return;
  }

#if defined(__x86_64__) || defined(_M_X64)
  static const CopyRow copy_row = x86_copy_fn();
#else
  constexpr CopyRow copy_row = copy_bgra_to_rgba;
#endif
  if (rowPitch == rowBytes) {
#if defined(__aarch64__) || defined(_M_ARM64)
    // NEON is faster for packed images only
    copy_bgra_neon(dst, src, width * height);
#else
    copy_row(dst, src, width * height);
#endif
  } else {
    for (size_t y = 0; y < height; ++y) {
      copy_row(dst + y * rowBytes, src + y * rowPitch, width);
    }
  }
}

} // namespace aurora::gfx::readback

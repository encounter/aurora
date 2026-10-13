#pragma once

#include <dolphin/gx/GXEnum.h>

#include <cstdint>
#include <optional>

namespace aurora::gfx::readback {

enum class Family : uint32_t {
  Bpp4 = 0,  // 8x8 texel tiles, two texels per byte (first texel in the high nibble)
  Bpp8 = 1,  // 8x4 texel tiles, one byte per texel
  Bpp16 = 2, // 4x4 texel tiles, two big-endian bytes per texel
  Bpp32 = 3, // 4x4 texel tiles split into an AR block followed by a GB block
};

enum class Encoding : uint32_t {
  Red = 0,          // byte = r
  Nibble = 1,       // 4-bit value from r
  NibblePairAR = 2, // byte = (a4 << 4) | r4
  PairAR = 3,       // byte0 = a, byte1 = r (IA8/RA8/RG8/GB8/Z16/Z16L slot layout)
  RGB565 = 4,       //
  RGB5A3 = 5,       //
  RGBA8 = 6,        // AR, then GB
};

struct Format {
  Family family;
  Encoding encoding;
  uint32_t tileWidth;
  uint32_t tileHeight;
  uint32_t tileBytes;
  bool rawColor;
};

constexpr std::optional<Format> format_info(uint32_t fmt) noexcept {
  switch (fmt) {
  case GX_TF_I4:
  case GX_CTF_R4:
  case GX_CTF_Z4:
    return Format{Family::Bpp4, Encoding::Nibble, 8, 8, 32, false};
  case GX_TF_I8:
  case GX_TF_Z8:
  case GX_CTF_Z8M:
  case GX_CTF_Z8L:
    return Format{Family::Bpp8, Encoding::Red, 8, 4, 32, false};
  case GX_CTF_A8:
  case GX_CTF_R8:
  case GX_CTF_G8:
  case GX_CTF_B8:
    return Format{Family::Bpp8, Encoding::Red, 8, 4, 32, true};
  case GX_TF_IA4:
  case GX_CTF_RA4:
    return Format{Family::Bpp8, Encoding::NibblePairAR, 8, 4, 32, false};
  case GX_TF_IA8:
  case GX_TF_Z16:
  case GX_CTF_Z16L:
    return Format{Family::Bpp16, Encoding::PairAR, 4, 4, 32, false};
  case GX_CTF_RA8:
  case GX_CTF_RG8:
  case GX_CTF_GB8:
    return Format{Family::Bpp16, Encoding::PairAR, 4, 4, 32, true};
  case GX_TF_RGB565:
    return Format{Family::Bpp16, Encoding::RGB565, 4, 4, 32, false};
  case GX_TF_RGB5A3:
    return Format{Family::Bpp16, Encoding::RGB5A3, 4, 4, 32, false};
  case GX_TF_RGBA8:
    return Format{Family::Bpp32, Encoding::RGBA8, 4, 4, 64, true};
  case GX_TF_Z24X8:
    return Format{Family::Bpp32, Encoding::RGBA8, 4, 4, 64, false};
  default:
    return std::nullopt;
  }
}

constexpr bool is_supported(uint32_t fmt) noexcept { return format_info(fmt).has_value(); }

constexpr uint32_t tiles_x(const Format& f, uint32_t width) noexcept { return (width + f.tileWidth - 1) / f.tileWidth; }
constexpr uint32_t tiles_y(const Format& f, uint32_t height) noexcept {
  return (height + f.tileHeight - 1) / f.tileHeight;
}

constexpr uint32_t byte_size(uint32_t fmt, uint32_t width, uint32_t height) noexcept {
  const auto f = format_info(fmt);
  if (!f || width == 0 || height == 0) {
    return 0;
  }
  const uint64_t size = static_cast<uint64_t>(tiles_x(*f, width)) * tiles_y(*f, height) * f->tileBytes;
  return size <= UINT32_MAX ? static_cast<uint32_t>(size) : 0;
}

} // namespace aurora::gfx::readback

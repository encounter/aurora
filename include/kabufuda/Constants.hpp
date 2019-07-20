#pragma once

#include <stdint.h>
#include "Util.hpp"

namespace kabufuda {
uint32_t constexpr BlockSize = 0x2000;
uint32_t constexpr MaxFiles = 127;
uint32_t constexpr FSTBlocks = 5;
uint32_t constexpr MbitToBlocks = 0x10;
uint32_t constexpr BATSize = 0xFFB;

/**
 * @brief The EPermissions enum
 */
enum class EPermissions : uint8_t {
  Public = (1 << 2),
  NoCopy = (1 << 3),
  NoMove = (1 << 4),
  Global = (1 << 5),
  Company = (1 << 6)
};
ENABLE_BITWISE_ENUM(EPermissions)

enum class EImageFormat : uint8_t {
  None,
  C8,
  RGB5A3,
};

enum class EAnimationType {
  Loop = 0,
  Bounce = 2,
};

enum class EAnimationSpeed {
  End,
  Fast,
  Middle,
  Slow,
};

enum class SeekOrigin { Begin, Current, End };

/**
 * @brief The ECardSlot enum
 */
enum class ECardSlot : uint16_t { SlotA, SlotB };

/**
 * @brief The ECardSize enum
 */
enum class ECardSize : uint16_t {
  Card59Mb = 0x04,
  Card123Mb = 0x08,
  Card251Mb = 0x10,
  Card507Mb = 0x20,
  Card1019Mb = 0x40,
  Card2043Mb = 0x80
};

constexpr uint32_t BannerWidth = 96;
constexpr uint32_t BannerHeight = 64;
constexpr uint32_t IconWidth = 32;
constexpr uint32_t IconHeight = 32;

/**
 * @brief The EEncoding enum
 */
enum class EEncoding : uint16_t {
  ASCII, /**< Standard ASCII Encoding */
  SJIS   /**< SJIS Encoding for japanese */
};
} // namespace kabufuda

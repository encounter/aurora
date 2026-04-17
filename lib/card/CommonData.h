#pragma once

#include <array>

#include "Util.hpp"
#include "Constants.hpp"

namespace aurora::card {

class FileHandle {
  friend class CardRawFile;
  uint32_t idx = UINT32_MAX;
  int32_t offset = 0;
  explicit FileHandle(uint32_t idx) : idx(idx) {}

public:
  FileHandle() = default;
  explicit FileHandle(uint32_t idx, int32_t offset) : idx(idx), offset(offset) {}

  [[nodiscard]] uint32_t getFileNo() const { return idx; }
  [[nodiscard]] uint32_t getOffset() const { return offset; }
  explicit operator bool() const { return getFileNo() != UINT32_MAX; }
};

struct ProbeResults {
  ECardResult x0_error;
  uint32_t x4_cardSize;   /* in megabits */
  uint32_t x8_sectorSize; /* in bytes */
};

struct CardStat {
  /* read-only (Set by Card::getStatus) */
  char x0_fileName[CARD_FILENAME_MAX];
  uint32_t x20_length;
  uint32_t x24_time; /* seconds since 01/01/2000 midnight */
  std::array<uint8_t, 4> x28_gameName;
  std::array<uint8_t, 2> x2c_company;

  /* read/write (Set by Card::getStatus/Card::setStatus) */
  uint8_t x2e_bannerFormat;
  uint8_t x2f___padding;
  uint32_t x30_iconAddr; /* offset to the banner, bannerTlut, icon, iconTlut data set. */
  uint16_t x34_iconFormat;
  uint16_t x36_iconSpeed;
  uint32_t x38_commentAddr; /* offset to the pair of 32 byte character strings. */

  /* read-only (Set by Card::getStatus) */
  uint32_t x3c_offsetBanner;
  uint32_t x40_offsetBannerTlut;
  std::array<uint32_t, CARD_ICON_MAX> x44_offsetIcon;
  uint32_t x64_offsetIconTlut;
  uint32_t x68_offsetData;

  uint32_t GetFileLength() const { return x20_length; }
  uint32_t GetTime() const { return x24_time; }
  EImageFormat GetBannerFormat() const { return EImageFormat(x2e_bannerFormat & 0x3); }
  void SetBannerFormat(EImageFormat fmt) { x2e_bannerFormat = (x2e_bannerFormat & ~0x3) | uint8_t(fmt); }
  EImageFormat GetIconFormat(int idx) const { return EImageFormat((x34_iconFormat >> (idx * 2)) & 0x3); }
  void SetIconFormat(EImageFormat fmt, int idx) {
    x34_iconFormat &= ~(0x3 << (idx * 2));
    x34_iconFormat |= uint16_t(fmt) << (idx * 2);
  }
  void SetIconSpeed(EAnimationSpeed sp, int idx) {
    x36_iconSpeed &= ~(0x3 << (idx * 2));
    x36_iconSpeed |= uint16_t(sp) << (idx * 2);
  }
  uint32_t GetIconAddr() const { return x30_iconAddr; }
  void SetIconAddr(uint32_t addr) { x30_iconAddr = addr; }
  uint32_t GetCommentAddr() const { return x38_commentAddr; }
  void SetCommentAddr(uint32_t addr) { x38_commentAddr = addr; }
};

}
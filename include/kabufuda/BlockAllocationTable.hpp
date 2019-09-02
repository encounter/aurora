#pragma once

#include <array>
#include <cstdint>
#include "kabufuda/Constants.hpp"

namespace kabufuda {
class BlockAllocationTable {
  friend class Card;
#pragma pack(push, 4)
  union {
    struct {
      uint16_t m_checksum;
      uint16_t m_checksumInv;
      uint16_t m_updateCounter;
      uint16_t m_freeBlocks;
      uint16_t m_lastAllocated;
      std::array<uint16_t, 0xFFB> m_map;
    };
    std::array<uint8_t, BlockSize> raw{};
  };
#pragma pack(pop)

  void swapEndian();
  void updateChecksum();
  bool valid() const;

public:
  explicit BlockAllocationTable(uint32_t blockCount = (uint32_t(ECardSize::Card2043Mb) * MbitToBlocks));
  ~BlockAllocationTable() = default;

  uint16_t getNextBlock(uint16_t block) const;
  uint16_t nextFreeBlock(uint16_t maxBlock, uint16_t startingBlock) const;
  bool clear(uint16_t first, uint16_t count);
  uint16_t allocateBlocks(uint16_t count, uint16_t maxBlocks);
  uint16_t numFreeBlocks() const { return m_freeBlocks; }
};
} // namespace kabufuda

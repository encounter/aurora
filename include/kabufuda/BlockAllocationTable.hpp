#pragma once

#include "Constants.hpp"

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
      uint16_t m_map[0xFFB];
    };
    uint8_t __raw[BlockSize];
  };
#pragma pack(pop)

  void swapEndian();
  void updateChecksum();
  bool valid() const;

public:
  explicit BlockAllocationTable(uint32_t blockCount = (uint32_t(ECardSize::Card2043Mb) * MbitToBlocks));
  BlockAllocationTable(uint8_t data[BlockSize]);
  ~BlockAllocationTable() = default;

  uint16_t getNextBlock(uint16_t block) const;
  uint16_t nextFreeBlock(uint16_t maxBlock, uint16_t startingBlock) const;
  bool clear(uint16_t first, uint16_t count);
  uint16_t allocateBlocks(uint16_t count, uint16_t maxBlocks);
  uint16_t numFreeBlocks() const { return m_freeBlocks; }
};
} // namespace kabufuda

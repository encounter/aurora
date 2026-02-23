#pragma once

#include <array>
#include <cstdint>
#include "kabufuda/File.hpp"

namespace kabufuda {
class Directory {
  friend class Card;

#pragma pack(push, 4)
  using RawData = std::array<uint8_t, BlockSize>;
  struct Data {
    File m_files[MaxFiles];
    uint8_t padding[0x3a];
    uint16_t m_updateCounter;
    uint16_t m_checksum;
    uint16_t m_checksumInv;
  };
#pragma pack(pop)


  union {
    Data data;
    RawData raw;
  };

  void swapEndian();
  void updateChecksum();
  bool valid() const;

public:
  Directory();
  explicit Directory(const RawData& rawData);
  ~Directory() = default;

  bool hasFreeFile() const;
  int32_t numFreeFiles() const;
  File* getFirstFreeFile(const char* game, const char* maker, const char* filename);
  File* getFirstNonFreeFile(uint32_t start, const char* game, const char* maker);
  File* getFile(const char* game, const char* maker, const char* filename);
  File* getFile(uint32_t idx);
  int32_t indexForFile(const File* f) const;
};
} // namespace kabufuda

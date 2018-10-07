#pragma once

#include "File.hpp"

namespace kabufuda
{
class Directory
{
    friend class Card;
#pragma pack(push, 4)
    union {
        struct
        {
            File m_files[MaxFiles];
            uint8_t __padding[0x3a];
            uint16_t m_updateCounter;
            uint16_t m_checksum;
            uint16_t m_checksumInv;
        };
        uint8_t __raw[BlockSize];
    };
#pragma pack(pop)

    void swapEndian();
    void updateChecksum();
    bool valid() const;

public:
    Directory();
    Directory(uint8_t data[BlockSize]);
    ~Directory() = default;

    bool hasFreeFile() const;
    int32_t numFreeFiles() const;
    File* getFirstFreeFile(const char* game, const char* maker, const char* filename);
    File* getFirstNonFreeFile(uint32_t start, const char* game, const char* maker);
    File* getFile(const char* game, const char* maker, const char* filename);
    File* getFile(uint32_t idx);
    int32_t indexForFile(File* f);
};
}


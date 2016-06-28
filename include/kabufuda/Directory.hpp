#ifndef __KABU_DIRECTORY_HPP__
#define __KABU_DIRECTORY_HPP__

#include "File.hpp"

namespace kabufuda
{
class Directory
{
    friend class Card;
#pragma pack(push, 4)
    union
    {
        struct
        {
            File     m_files[MaxFiles];
            uint8_t  __padding[0x3a];
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
    Directory(const Directory& other);
    void operator=(const Directory& other);
    ~Directory() {}

    File* getFirstFreeFile(const char* game, const char* maker, const char* filename);
    File* getFile(const char* game, const char* maker, const char* filename);
};
}

#endif // __KABU_DIRECTORY_HPP__

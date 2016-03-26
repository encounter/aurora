#ifndef __CARD_HPP__
#define __CARD_HPP__

#include <string>
#include <vector>
#include <stdint.h>
#include <memory.h>

namespace card
{
uint32_t constexpr BlockSize = 0x2000;
uint32_t constexpr MaxFiles = 127;

enum class EPermissions : uint8_t
{
};
enum class EBannerFlags : uint8_t
{
};

class File
{
    union
    {
        struct
        {
            uint8_t  m_id[4];
            uint8_t  m_maker[2];
            uint8_t  m_reserved;
            uint8_t  m_bannerFlags;
            char     m_filename[0x20];
            uint32_t m_modifiedTime;
            uint8_t  m_permissions;
            int8_t   m_copyCounter;
            uint16_t m_firstBlock;
            uint16_t m_reserved2;
            uint32_t m_commentAddr;
        };
        uint8_t __raw[0x40] = {0xFF};
    };
public:
    File() {}
    File(char data[0x40])
    {
        memcpy(__raw, data, 0x40);
    }
    File(const char* filename)
    {
        memset(m_filename, 0, 0x20);
        memcpy(m_filename, filename, 0x20);
    }
    ~File() {}
};

class BlockAllocationTable
{
    union
    {
        struct
        {
            uint16_t m_checksum;
            uint16_t m_checksumInv;
            uint16_t m_freeBlocks;
            uint16_t m_lastAllocated;
            uint16_t m_map[0xFFB];
        };
        uint8_t __raw[BlockSize] = {0xFF};
    };
public:
    BlockAllocationTable() {}
    BlockAllocationTable(uint8_t data[BlockSize]);
    ~BlockAllocationTable() {}
};

class Directory
{
    union
    {
        struct
        {
            File m_files[MaxFiles];
            uint8_t __padding[0x3a];
            uint16_t m_updateCounter;
            uint16_t m_checksum;
            uint16_t m_checksumInv;
        };
        uint8_t __raw[BlockSize] = {0xFF};
    };
public:
    Directory() {}
    Directory(uint8_t data[BlockSize])
    {
        memcpy(__raw, data, BlockSize);
    }
    ~Directory() {}
};

class Card
{
    union
    {
        struct
        {
            std::string m_filepath;
            uint8_t  m_serial[12];
            uint64_t m_formatTime;
            int32_t  m_sramBias;
            uint32_t m_sramLanguage;
            uint32_t m_unknown;
            uint16_t m_deviceId; /* 0 for Slot A, 1 for Slot B */
            uint16_t m_sizeMb;
            uint16_t m_encoding;
            uint8_t  __padding[468];
            uint16_t m_updateCounter;
            uint16_t m_checksum;
            uint16_t m_checksumInv;
        };
        uint8_t __raw[BlockSize] = {0xFF};
    };
    Directory  m_dir;
    Directory  m_dirBackup;
    Directory* m_dirInUse = nullptr;
    BlockAllocationTable  m_bat;
    BlockAllocationTable  m_batBackup;
    BlockAllocationTable* m_batInUse = nullptr;

    char m_game[4];  /*!< The current selected game,  if null requests return the first matching filename */
    char m_maker[2]; /*!< The current selected maker, if null requests return the first matching filename */
public:
    Card();
    Card(const std::string& filepath, const char* game = nullptr, const char* maker=nullptr);
    ~Card() {}

    void setGame(const char* id);
    const uint8_t* getGame() const;
    void setMaker(const char* maker);
    const uint8_t* getMaker() const;
};
}

#endif // __CARD_HPP__


#ifndef __CARD_HPP__
#define __CARD_HPP__

#include <string>
#include <vector>
#include <memory>

#include "Util.hpp"

namespace kabufuda
{
uint32_t constexpr BlockSize    = 0x2000;
uint32_t constexpr MaxFiles     = 127;
uint32_t constexpr FSTBlocks    = 5;
uint32_t constexpr MbitToBlocks = 0x10;
uint32_t constexpr BATSize      = 0xFFB;

/**
 * @brief The EPermissions enum
 */
enum class EPermissions : uint8_t
{
};

/**
 * @brief The EBannerFlags enum
 */
enum class EBannerFlags : uint8_t
{
};

/**
 * @brief The EDeviceId enum
 */
enum class EDeviceId : uint16_t
{
    SlotA,
    SlotB
};

/**
 * @brief The ECardSize enum
 */
enum class ECardSize : uint16_t
{
    Card59Mb   = 0x04,
    Card123Mb  = 0x08,
    Card251Mb  = 0x10,
    Card507Mb  = 0x20,
    Card1019Mb = 0x40,
    Card2043Mb = 0x80
};

/**
 * @brief The EEncoding enum
 */
enum class EEncoding : uint16_t
{
    ASCII,   /**< Standard ASCII Encoding */
    SJIS     /**< SJIS Encoding for japanese */
};

class File
{
    friend class FileHandle;
    friend class Directory;
    friend class Card;
#pragma pack(push, 4)
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
            uint32_t m_imageOffset;
            uint16_t m_iconFmt;
            uint16_t m_animSpeed;
            uint8_t  m_permissions;
            int8_t   m_copyCounter;
            uint16_t m_firstBlock;
            uint16_t m_blockCount;
            uint16_t m_reserved2;
            uint32_t m_commentAddr;
        };
        uint8_t __raw[0x40];
    };

#pragma pop()
    void swapEndian();

public:
    File() {}
    File(char data[0x40]);
    File(const char* filename);
    ~File() {}
};

struct FileHandle
{
    File* file = nullptr;
    uint16_t curBlock = 0;
    uint16_t blockOffset = 0;
    uint32_t offset =0;
    operator bool() const { return file != nullptr; }
    FileHandle(File* file = nullptr)
        : file(file),
          curBlock(file ? file->m_firstBlock : 0)
    {
    }
};

class BlockAllocationTable
{
    friend class Card;
#pragma pack(push, 4)
    union
    {
        struct
        {
            uint16_t m_checksum;
            uint16_t m_checksumInv;
            uint16_t m_updateCounter;
            uint16_t m_freeBlocks;
            uint16_t m_lastAllocated;
            uint16_t m_map[0xFFB];
        };
        uint8_t __raw[BlockSize];
    };
#pragma pop()

    void swapEndian();

public:
    explicit BlockAllocationTable(uint32_t blockCount = (uint32_t(ECardSize::Card2043Mb) * MbitToBlocks));
    BlockAllocationTable(uint8_t data[BlockSize]);
    ~BlockAllocationTable() {}

    uint16_t getNextBlock(uint16_t block) const;
    uint16_t nextFreeBlock(uint16_t maxBlock, uint16_t startingBlock) const;
    bool clear(uint16_t first, uint16_t count);
    uint16_t allocateBlocks(uint16_t count, uint16_t maxBlocks);
};

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
#pragma pop()

    void swapEndian();

public:
    Directory();
    Directory(uint8_t data[BlockSize]);
    Directory(const Directory& other);
    void operator=(const Directory& other);
    ~Directory() {}

    File* getFirstFreeFile(char* game, char* maker, const char* filename);
    File* getFile(char* game, char* maker, const char* filename);

};

class Card
{
#pragma pack(push, 4)
    union
    {
        struct
        {
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
        uint8_t __raw[BlockSize];
    };

    void swapEndian();
#pragma pop()

    SystemString m_filename;
    Directory  m_dir;
    Directory  m_dirBackup;
    Directory* m_currentDir;
    Directory* m_previousDir;
    BlockAllocationTable  m_bat;
    BlockAllocationTable  m_batBackup;
    BlockAllocationTable* m_currentBat;
    BlockAllocationTable* m_previousBat;

    uint16_t m_maxBlock;
    char m_game[5] = {'\0'};
    char m_maker[3] = {'\0'};
public:
    Card();
    Card(const SystemString& filepath, const char* game = nullptr, const char* maker=nullptr);
    ~Card();

    /**
     * @brief openFile
     * @param filename
     */
    FileHandle openFile(const char* filename);
    /**
     * @brief createFile
     * @param filename
     * @return
     */
    FileHandle createFile(const char* filename, size_t size);
    void write(FileHandle* f, const void* buf, size_t size);
    /**
     * @brief Sets the current game, if not null any openFile requests will only return files that match this game
     * @param game The target game id, e.g "GM8E"
     * @sa openFile
     */
    void setGame(const char* game);
    /**
     * @brief Returns the currently selected game
     * @return The selected game, or nullptr
     */
    const uint8_t* getGame() const;
    /**
     * @brief Sets the current maker, if not null any openFile requests will only return files that match this maker
     * @param maker The target maker id, e.g "01"
     * @sa openFile
     */
    void setMaker(const char* maker);
    /**
     * @brief Returns the currently selected maker
     * @return The selected maker, or nullptr
     */
    const uint8_t* getMaker() const;
    /**
     * @brief Retrieves the format assigned serial in two 32bit parts
     * @param s0
     * @param s1
     */
    void getSerial(uint32_t* s0, uint32_t* s1);
    /**
     * @brief Retrieves
     * @param checksum
     * @param inverse
     */
    void getChecksum(uint16_t* checksum, uint16_t* inverse);
    /**
     * @brief Formats the memory card and assigns a new serial
     * @param size The desired size of the file @sa ECardSize
     * @param encoding The desired encoding @sa EEncoding
     */
    void format(EDeviceId deviceId, ECardSize size = ECardSize::Card2043Mb, EEncoding encoding = EEncoding::ASCII);

    /**
     * @brief getSizeMbit
     * @return
     */
    static uint32_t getSizeMbitFromFile(const SystemString& filename);

    void commit();
};

/**
 * @brief calculateChecksum
 * @param data
 * @param len
 * @param checksum
 * @param checksumInv
 */
void calculateChecksum(uint16_t* data, size_t len, uint16_t* checksum, uint16_t* checksumInv);
}

#endif // __CARD_HPP__


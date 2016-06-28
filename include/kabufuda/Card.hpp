#ifndef __KABU_CARD_HPP__
#define __KABU_CARD_HPP__

#include "BlockAllocationTable.hpp"
#include "Directory.hpp"
#include "File.hpp"
#include "Util.hpp"

#include <string>
#include <vector>
#include <memory>

namespace kabufuda
{

class IFileHandle
{
public:
    virtual ~IFileHandle();
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

#pragma pack(pop)

    SystemString m_filename;
    FILE*      m_fileHandle = nullptr;
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

    void swapEndian();
    void updateDirAndBat();
    void updateChecksum();
public:
    Card();
    Card(const Card& other);
    Card(const SystemString& filepath, const char* game = nullptr, const char* maker=nullptr);
    ~Card();

    /**
     * @brief openFile
     * @param filename
     */
    std::unique_ptr<IFileHandle>  openFile(const char* filename);
    /**
     * @brief createFile
     * @param filename
     * @return
     */
    std::unique_ptr<IFileHandle>  createFile(const char* filename, size_t size);
    void deleteFile(const std::unique_ptr<IFileHandle>& fh);
    void write(const std::unique_ptr<IFileHandle>& fh, const void* buf, size_t size);
    void read(const std::unique_ptr<IFileHandle>& fh, void* dst, size_t size);
    void seek(const std::unique_ptr<IFileHandle>& fh, int32_t pos, SeekOrigin whence);
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

    operator bool() const;
};
}

#endif // __CARD_HPP__


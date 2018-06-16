#ifndef __KABU_CARD_HPP__
#define __KABU_CARD_HPP__

#include "BlockAllocationTable.hpp"
#include "Directory.hpp"
#include "File.hpp"
#include "Util.hpp"
#include "AsyncIO.hpp"

#include <string>
#include <vector>
#include <memory>

#define CARD_FILENAME_MAX 32
#define CARD_ICON_MAX 8

namespace kabufuda
{

class FileHandle
{
    friend class Card;
    uint32_t idx = -1;
    int32_t offset = 0;
    FileHandle(uint32_t idx) : idx(idx) {}
public:
    FileHandle() = default;
    uint32_t getFileNo() const { return idx; }
    operator bool() const { return getFileNo() != -1; }
};

struct ProbeResults
{
    ECardResult x0_error;
    uint32_t x4_cardSize; /* in megabits */
    uint32_t x8_sectorSize; /* in bytes */
};

struct CardStat
{
    /* read-only (Set by Card::getStatus) */
    char x0_fileName[CARD_FILENAME_MAX];
    uint32_t x20_length;
    uint32_t x24_time;          /* seconds since 01/01/2000 midnight */
    uint8_t x28_gameName[4];
    uint8_t x2c_company[2];

    /* read/write (Set by Card::getStatus/Card::setStatus) */
    uint8_t x2e_bannerFormat;
    uint8_t x2f___padding;
    uint32_t x30_iconAddr;      /* offset to the banner, bannerTlut, icon, iconTlut data set. */
    uint16_t x34_iconFormat;
    uint16_t x36_iconSpeed;
    uint32_t x38_commentAddr;   /* offset to the pair of 32 byte character strings. */

    /* read-only (Set by Card::getStatus) */
    uint32_t x3c_offsetBanner;
    uint32_t x40_offsetBannerTlut;
    uint32_t x44_offsetIcon[CARD_ICON_MAX];
    uint32_t x64_offsetIconTlut;
    uint32_t x68_offsetData;

    uint32_t GetFileLength() const { return x20_length; }
    uint32_t GetTime() const { return x24_time; }
    EImageFormat GetBannerFormat() const { return EImageFormat(x2e_bannerFormat & 0x3); }
    void SetBannerFormat(EImageFormat fmt) { x2e_bannerFormat = (x2e_bannerFormat & ~0x3) | uint8_t(fmt); }
    EImageFormat GetIconFormat(int idx) const { return EImageFormat((x34_iconFormat >> (idx * 2)) & 0x3); }
    void SetIconFormat(EImageFormat fmt, int idx)
    {
        x34_iconFormat &= ~(0x3 << (idx * 2));
        x34_iconFormat |= uint16_t(fmt) << (idx * 2);
    }
    void SetIconSpeed(EAnimationSpeed sp, int idx)
    {
        x36_iconSpeed &= ~(0x3 << (idx * 2));
        x36_iconSpeed |= uint16_t(sp) << (idx * 2);
    }
    uint32_t GetIconAddr() const { return x30_iconAddr; }
    void SetIconAddr(uint32_t addr) { x30_iconAddr = addr; }
    uint32_t GetCommentAddr() const { return x38_commentAddr; }
    void SetCommentAddr(uint32_t addr) { x38_commentAddr = addr; }
};

class Card
{
#pragma pack(push, 4)
    struct CardHeader
    {
        uint8_t m_serial[12];
        uint64_t m_formatTime;
        int32_t m_sramBias;
        uint32_t m_sramLanguage;
        uint32_t m_unknown;
        uint16_t m_deviceId; /* 0 for Slot A, 1 for Slot B */
        uint16_t m_sizeMb;
        uint16_t m_encoding;
        uint8_t __padding[468];
        uint16_t m_updateCounter;
        uint16_t m_checksum;
        uint16_t m_checksumInv;
        void _swapEndian();
    };
    union {
        CardHeader m_ch;
        uint8_t __raw[BlockSize];
    };
    CardHeader m_tmpCh;
#pragma pack(pop)

    SystemString m_filename;
    AsyncIO m_fileHandle;
    Directory m_dirs[2];
    BlockAllocationTable m_bats[2];
    Directory m_tmpDirs[2];
    BlockAllocationTable m_tmpBats[2];
    uint8_t m_currentDir;
    uint8_t m_currentBat;

    uint16_t m_maxBlock;
    char m_game[5] = {'\0'};
    char m_maker[3] = {'\0'};

    void _updateDirAndBat(const Directory& dir, const BlockAllocationTable& bat);
    void _updateChecksum();
    File* _fileFromHandle(const FileHandle& fh) const;
    void _deleteFile(File& f, BlockAllocationTable& bat);

    bool m_dirty = false;
    bool m_opened = false;
    ECardResult _pumpOpen();

public:
    Card();
    /**
     * @brief Card
     * @param other
     */
    Card(const Card& other) = delete;
    Card& operator=(const Card& other) = delete;
    Card(Card&& other);
    Card& operator=(Card&& other);

    /**
     * @brief Card
     * @param filepath
     * @param game
     * @param maker
     */
    Card(const char* game = nullptr, const char* maker = nullptr);
    ~Card();

    /**
     * @brief openFile
     * @param filename
     */
    ECardResult openFile(const char* filename, FileHandle& handleOut);

    /**
     * @brief openFile
     * @param fileno
     */
    ECardResult openFile(uint32_t fileno, FileHandle& handleOut);

    /**
     * @brief createFile
     * @param filename
     * @return
     */
    ECardResult createFile(const char* filename, size_t size, FileHandle& handleOut);

    /**
     * @brief closeFile
     * @param fh FileHandle to close
     * @return
     */
    ECardResult closeFile(FileHandle& fh);

    /**
     * @brief firstFile
     * @return
     */
    FileHandle firstFile();

    /**
     * @brief nextFile
     * @param cur
     * @return
     */
    FileHandle nextFile(const FileHandle& cur);

    /**
     * @brief getFilename
     * @param fh
     * @return
     */
    const char* getFilename(const FileHandle& fh);

    /**
     * @brief deleteFile
     * @param fh
     */
    void deleteFile(const FileHandle& fh);

    /**
     * @brief deleteFile
     * @param filename
     */
    ECardResult deleteFile(const char* filename);

    /**
     * @brief deleteFile
     * @param fileno
     */
    ECardResult deleteFile(uint32_t fileno);

    /**
     * @brief renameFile
     * @param oldName
     * @param newName
     */
    ECardResult renameFile(const char* oldName, const char* newName);

    /**
     * @brief write
     * @param fh
     * @param buf
     * @param size
     */
    ECardResult asyncWrite(FileHandle& fh, const void* buf, size_t size);

    /**
     * @brief read
     * @param fh
     * @param dst
     * @param size
     */
    ECardResult asyncRead(FileHandle& fh, void* dst, size_t size);

    /**
     * @brief seek
     * @param fh
     * @param pos
     * @param whence
     */
    void seek(FileHandle& fh, int32_t pos, SeekOrigin whence);

    /**
     * @brief Returns the current offset of the specified file
     * @param fh The file to retrieve the offset from
     * @return The offset or -1 if an invalid handle is passed
     */
    int32_t tell(const FileHandle& fh);

    /**
     * @brief setPublic
     * @param fh
     * @param pub
     */
    void setPublic(const FileHandle& fh, bool pub);

    /**
     * @brief isPublic
     * @param fh
     * @return
     */
    bool isPublic(const FileHandle& fh) const;

    /**
     * @brief setCanCopy
     * @param fh
     * @param copy
     */
    void setCanCopy(const FileHandle& fh, bool copy) const;

    /**
     * @brief canCopy
     * @param fh
     * @return
     */
    bool canCopy(const FileHandle& fh) const;

    /**
     * @brief setCanMove
     * @param fh
     * @param move
     */
    void setCanMove(const FileHandle& fh, bool move);

    /**
     * @brief canMove
     * @param fh
     * @return
     */
    bool canMove(const FileHandle& fh) const;

    /**
     * @brief getStatus
     * @param fh Handle of requested file
     * @param statOut Structure to fill with file stat
     * @return NOFILE or READY
     */
    ECardResult getStatus(const FileHandle& fh, CardStat& statOut) const;

    /**
     * @brief getStatus
     * @param fileNo Number of requested file
     * @param statOut Structure to fill with file stat
     * @return NOFILE or READY
     */
    ECardResult getStatus(uint32_t fileNo, CardStat& statOut) const;

    /**
     * @brief setStatus
     * @param fh Handle of requested file
     * @param statOut Structure to access for file stat
     * @return NOFILE or READY
     */
    ECardResult setStatus(const FileHandle& fh, const CardStat& stat);

    /**
     * @brief setStatus
     * @param fileNo Number of requested file
     * @param statOut Structure to access for file stat
     * @return NOFILE or READY
     */
    ECardResult setStatus(uint32_t fileNo, const CardStat& stat);

#if 0 // TODO: Async-friendly implementations
    /**
     * @brief Copies a file from the current Card instance to a specified Card instance
     * @param fh The file to copy
     * @param dest The destination Card instance
     * @return True if successful, false otherwise
     */
    bool copyFileTo(FileHandle& fh, Card& dest);

    /**
     * @brief moveFileTo
     * @param fh
     * @param dest
     * @return
     */
    bool moveFileTo(FileHandle& fh, Card& dest);
#endif

    /**
     * @brief Sets the current game, if not null any openFile requests will only return files that match this game
     * @param game The target game id, e.g "GM8E"
     * @sa openFile
     */
    void setCurrentGame(const char* game);

    /**
     * @brief Returns the currently selected game
     * @return The selected game, or nullptr
     */
    const uint8_t* getCurrentGame() const;

    /**
     * @brief Sets the current maker, if not null any openFile requests will only return files that match this maker
     * @param maker The target maker id, e.g "01"
     * @sa openFile
     */
    void setCurrentMaker(const char* maker);

    /**
     * @brief Returns the currently selected maker
     * @return The selected maker, or nullptr
     */
    const uint8_t* getCurrentMaker() const;

    /**
     * @brief Retrieves the format assigned serial
     * @param serial
     */
    void getSerial(uint64_t& serial);

    /**
     * @brief Retrieves the checksum values of the Card system header
     * @param checksum The checksum of the system header
     * @param inverse  The inverser checksum of the system header
     */
    void getChecksum(uint16_t& checksum, uint16_t& inverse);

    /**
     * @brief Retrieves the available storage and directory space
     * @param bytesNotUsed Number of free bytes out
     * @param filesNotUsed Number of free files out
     */
    void getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed);

    /**
     * @brief Formats the memory card and assigns a new serial
     * @param size The desired size of the file @sa ECardSize
     * @param encoding The desired encoding @sa EEncoding
     */
    void format(ECardSlot deviceId, ECardSize size = ECardSize::Card2043Mb, EEncoding encoding = EEncoding::ASCII);

    /**
     * @brief Returns basic stats about a card image without opening a handle
     * @return ProbeResults structure
     */
    static ProbeResults probeCardFile(SystemStringView filename);

    /**
     * @brief Writes any changes to the Card instance immediately to disk. <br />
     * <b>Note:</b> <i>Under normal circumstances there is no need to call this function.</i>
     */
    void commit();

    /**
     * @brief Opens card image (does nothing if currently open path matches)
     */
    bool open(SystemStringView filepath);

    /**
     * @brief Commits changes to disk and closes host file
     */
    void close();

    /**
     * @brief Access host filename of card
     */
    SystemStringView cardFilename() const { return m_filename; }

    /**
     * @brief Gets card-scope error state
     * @return READY, BROKEN, or NOCARD
     */
    ECardResult getError() const;

    /**
     * @brief Block caller until any asynchronous I/O operations have completed
     */
    void waitForCompletion() const;

    operator bool() const { return getError() == ECardResult::READY; }
};
}

#endif // __CARD_HPP__

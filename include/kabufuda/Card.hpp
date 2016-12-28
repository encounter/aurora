#ifndef __KABU_CARD_HPP__
#define __KABU_CARD_HPP__

#include "BlockAllocationTable.hpp"
#include "Directory.hpp"
#include "File.hpp"
#include "Util.hpp"

#include <string>
#include <vector>
#include <memory>

#define CARD_FILENAME_MAX 32
#define CARD_ICON_MAX 8
#undef NOFILE

namespace kabufuda
{

class IFileHandle
{
protected:
    uint32_t idx;
    IFileHandle() = default;
    IFileHandle(uint32_t idx) : idx(idx) {}
public:
    uint32_t getFileNo() const { return idx; }
    virtual ~IFileHandle();
};

enum class ECardResult
{
    CRC_MISMATCH = -1003, /* Extension enum for Retro's CRC check */
    FATAL_ERROR = -128,
    ENCODING = -13,
    NAMETOOLONG = -12,
    INSSPACE = -9,
    NOENT = -8,
    EXIST = -7,
    BROKEN = -6,
    IOERROR = -5,
    NOFILE = -4,
    NOCARD = -3,
    WRONGDEVICE = -2,
    BUSY = -1,
    READY = 0
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
    union {
        struct
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
        };
        uint8_t __raw[BlockSize];
    };

#pragma pack(pop)

    SystemString m_filename;
    FILE* m_fileHandle = nullptr;
    Directory m_dir;
    Directory m_dirBackup;
    Directory* m_currentDir;
    Directory* m_previousDir;
    BlockAllocationTable m_bat;
    BlockAllocationTable m_batBackup;
    BlockAllocationTable* m_currentBat;
    BlockAllocationTable* m_previousBat;

    uint16_t m_maxBlock;
    char m_game[5] = {'\0'};
    char m_maker[3] = {'\0'};

    void _swapEndian();
    void _updateDirAndBat();
    void _updateChecksum();
    File* _fileFromHandle(const std::unique_ptr<IFileHandle>& fh) const;
    void _deleteFile(File& f);

public:
    Card();
    /**
     * @brief Card
     * @param other
     */
    Card(const Card& other);
    /**
     * @brief Card
     * @param filepath
     * @param game
     * @param maker
     */
    Card(const SystemString& filepath, const char* game = nullptr, const char* maker = nullptr);
    ~Card();

    /**
     * @brief openFile
     * @param filename
     */
    ECardResult openFile(const char* filename, std::unique_ptr<IFileHandle>& handleOut);

    /**
     * @brief openFile
     * @param fileno
     */
    ECardResult openFile(uint32_t fileno, std::unique_ptr<IFileHandle>& handleOut);

    /**
     * @brief createFile
     * @param filename
     * @return
     */
    ECardResult createFile(const char* filename, size_t size, std::unique_ptr<IFileHandle>& handleOut);

    /**
     * @brief firstFile
     * @return
     */
    std::unique_ptr<IFileHandle> firstFile();

    /**
     * @brief nextFile
     * @param cur
     * @return
     */
    std::unique_ptr<IFileHandle> nextFile(const std::unique_ptr<IFileHandle>& cur);

    /**
     * @brief getFilename
     * @param fh
     * @return
     */
    const char* getFilename(const std::unique_ptr<IFileHandle>& fh);

    /**
     * @brief deleteFile
     * @param fh
     */
    void deleteFile(const std::unique_ptr<IFileHandle>& fh);

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
    void write(const std::unique_ptr<IFileHandle>& fh, const void* buf, size_t size);

    /**
     * @brief read
     * @param fh
     * @param dst
     * @param size
     */
    void read(const std::unique_ptr<IFileHandle>& fh, void* dst, size_t size);

    /**
     * @brief seek
     * @param fh
     * @param pos
     * @param whence
     */
    void seek(const std::unique_ptr<IFileHandle>& fh, int32_t pos, SeekOrigin whence);

    /**
     * @brief Returns the current offset of the specified file
     * @param fh The file to retrieve the offset from
     * @return The offset or -1 if an invalid handle is passed
     */
    int32_t tell(const std::unique_ptr<IFileHandle>& fh);

    /**
     * @brief setPublic
     * @param fh
     * @param pub
     */
    void setPublic(const std::unique_ptr<IFileHandle>& fh, bool pub);

    /**
     * @brief isPublic
     * @param fh
     * @return
     */
    bool isPublic(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief setCanCopy
     * @param fh
     * @param copy
     */
    void setCanCopy(const std::unique_ptr<IFileHandle>& fh, bool copy) const;

    /**
     * @brief canCopy
     * @param fh
     * @return
     */
    bool canCopy(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief setCanMove
     * @param fh
     * @param move
     */
    void setCanMove(const std::unique_ptr<IFileHandle>& fh, bool move);

    /**
     * @brief canMove
     * @param fh
     * @return
     */
    bool canMove(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief getStatus
     * @param fh Handle of requested file
     * @param statOut Structure to fill with file stat
     * @return NOFILE or READY
     */
    ECardResult getStatus(const std::unique_ptr<IFileHandle>& fh, CardStat& statOut) const;

    /**
     * @brief setStatus
     * @param fh Handle of requested file
     * @param statOut Structure to access for file stat
     * @return NOFILE or READY
     */
    ECardResult setStatus(const std::unique_ptr<IFileHandle>& fh, const CardStat& stat);

    /**
     * @brief gameId
     * @param fh
     * @return
     */
    const char* gameId(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief maker
     * @param fh
     * @return
     */
    const char* maker(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief setBannerFormat
     * @param fh
     * @param fmt
     */
    void setBannerFormat(const std::unique_ptr<IFileHandle>& fh, EImageFormat fmt);

    /**
     * @brief bannerFormat
     * @param fh
     * @return
     */
    EImageFormat bannerFormat(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief setIconAnimationType
     * @param fh
     * @param type
     */
    void setIconAnimationType(const std::unique_ptr<IFileHandle>& fh, EAnimationType type);

    /**
     * @brief iconAnimationType
     * @param fh
     * @return
     */
    EAnimationType iconAnimationType(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief setIconFormat
     * @param fh
     * @param idx
     * @param fmt
     */
    void setIconFormat(const std::unique_ptr<IFileHandle>& fh, uint32_t idx, EImageFormat fmt);

    /**
     * @brief iconFormat
     * @param fh
     * @param idx
     * @return
     */
    EImageFormat iconFormat(const std::unique_ptr<IFileHandle>& fh, uint32_t idx) const;

    /**
     * @brief setIconSpeed
     * @param fh
     * @param idx
     * @param speed
     */
    void setIconSpeed(const std::unique_ptr<IFileHandle>& fh, uint32_t idx, EAnimationSpeed speed);

    /**
     * @brief iconSpeed
     * @param fh
     * @param idx
     * @return
     */
    EAnimationSpeed iconSpeed(const std::unique_ptr<IFileHandle>& fh, uint32_t idx) const;

    /**
     * @brief setImageAddress
     * @param fh
     * @param addr
     */
    void setImageAddress(const std::unique_ptr<IFileHandle>& fh, uint32_t addr);

    /**
     * @brief imageAddress
     * @param fh
     * @return
     */
    int32_t imageAddress(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief setCommentAddress
     * @param fh
     * @param addr
     */
    void setCommentAddress(const std::unique_ptr<IFileHandle>& fh, uint32_t addr);

    /**
     * @brief commentAddress
     * @param fh
     * @return
     */
    int32_t commentAddress(const std::unique_ptr<IFileHandle>& fh) const;

    /**
     * @brief Copies a file from the current Card instance to a specified Card instance
     * @param fh The file to copy
     * @param dest The destination Card instance
     * @return True if successful, false otherwise
     */
    bool copyFileTo(const std::unique_ptr<IFileHandle>& fh, Card& dest);

    /**
     * @brief moveFileTo
     * @param fh
     * @param dest
     * @return
     */
    bool moveFileTo(const std::unique_ptr<IFileHandle>& fh, Card& dest);

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
    static ProbeResults probeCardFile(const SystemString& filename);

    /**
     * @brief Writes any changes to the Card instance immediately to disk. <br />
     * <b>Note:</b> <i>Under normal circumstances there is no need to call this function.</i>
     */
    void commit();

    /**
     * @brief Gets card-scope error state
     * @return READY, BROKEN, or NOCARD
     */
    ECardResult getError() const;

    operator bool() const { return getError() == ECardResult::READY; }
};
}

#endif // __CARD_HPP__

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
    std::unique_ptr<IFileHandle> openFile(const char* filename);

    /**
     * @brief createFile
     * @param filename
     * @return
     */
    std::unique_ptr<IFileHandle> createFile(const char* filename, size_t size);

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
     * @brief Retrieves the format assigned serial in two 32bit parts
     * @param s0
     * @param s1
     */
    void getSerial(uint32_t* s0, uint32_t* s1);

    /**
     * @brief Retrieves the checksum values of the Card system header
     * @param checksum The checksum of the system header
     * @param inverse  The inverser checksum of the system header
     */
    void getChecksum(uint16_t* checksum, uint16_t* inverse);

    /**
     * @brief Formats the memory card and assigns a new serial
     * @param size The desired size of the file @sa ECardSize
     * @param encoding The desired encoding @sa EEncoding
     */
    void format(EDeviceId deviceId, ECardSize size = ECardSize::Card2043Mb, EEncoding encoding = EEncoding::ASCII);

    /**
     * @brief Returns the size of the file in Megabits from a file on disk, useful for determining filesize ahead of
     * time.
     * @return Size of file in Megabits
     */
    static uint32_t getSizeMbitFromFile(const SystemString& filename);

    /**
     * @brief Writes any changes to the Card instance immediately to disk. <br />
     * <b>Note:</b> <i>Under normal circumstances there is no need to call this function.</i>
     */
    void commit();

    operator bool() const;
};
}

#endif // __CARD_HPP__

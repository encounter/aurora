#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "FileIO.hpp"
#include "BlockAllocationTable.hpp"
#include "Directory.hpp"
#include "File.hpp"
#include "Util.hpp"

#include "CommonData.h"
#include "ICard.hpp"

namespace aurora::card {

class CardRawFile : public ICard {
#pragma pack(push, 4)
  struct CardHeader {
    union {
      struct {
        std::array<uint8_t, 12> m_serial;
        uint64_t m_formatTime;
        int32_t m_sramBias;
        uint32_t m_sramLanguage;
        uint32_t m_unknown;
        uint16_t m_deviceId; /* 0 for Slot A, 1 for Slot B */
        uint16_t m_sizeMb;
        uint16_t m_encoding;
        std::array<uint8_t, 468> padding;
        uint16_t m_updateCounter;
        uint16_t m_checksum;
        uint16_t m_checksumInv;
      };
      std::array<uint8_t, BlockSize> raw;
    };
    void _swapEndian();
  };
#pragma pack(pop)

  CardHeader m_ch;
  CardHeader m_tmpCh;

  std::filesystem::path m_filename;
  FileIO m_fileHandle;
  std::array<Directory, 2> m_dirs;
  std::array<BlockAllocationTable, 2> m_bats;
  std::array<Directory, 2> m_tmpDirs;
  std::array<BlockAllocationTable, 2> m_tmpBats;
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
  CardRawFile();
  CardRawFile(const CardRawFile& other) = delete;
  CardRawFile& operator=(const CardRawFile& other) = delete;
  CardRawFile(CardRawFile&& other);
  CardRawFile& operator=(CardRawFile&& other);

  /**
   * @brief Card
   *
   * @param game  The game code.
   * @param maker The maker code.
   */
  void InitCard(const char* game = nullptr, const char* maker = nullptr) override;

  ~CardRawFile() override;

  /**
   * @brief openFile
   *
   * @param[in]  filename  The name of the file to open.
   * @param[out] handleOut Out reference that will contain the opened file handle.
   *
   * @return A result indicating the error status of the operation.
   */
  ECardResult openFile(const char* filename, FileHandle& handleOut) override;

  /**
   * @brief openFile
   *
   * @param[in]  fileno    The file index.
   * @param[out] handleOut Out reference that will contain the opened file handle.
   *
   * @return A result indicating the error status of the operation.
   */
  ECardResult openFile(uint32_t fileno, FileHandle& handleOut) override;

  /**
   * @brief createFile
   *
   * @param[in]  filename  The name of the file to create.
   * @param[out] handleOut Out reference that will contain the handle of the created file.
   *
   * @return A result indicating the error status of the operation.
   */
  ECardResult createFile(const char* filename, size_t size, FileHandle& handleOut) override;

  /**
   * @brief closeFile
   *
   * @param fh FileHandle to close
   *
   * @return READY
   */
  ECardResult closeFile(FileHandle& fh) override;

  /**
   * @brief firstFile
   *
   * @return The first file within this card instance. If
   *         no file is able to be found, then an invalid
   *         handle will be returned.
   */
  FileHandle firstFile();

  /**
   * @brief nextFile
   *
   * @param cur The file handle indicating the current file handle.
   *
   * @return The next file within this card. If no file can be found,
   *         an invalid handle will be returned.
   */
  FileHandle nextFile(const FileHandle& cur);

  /**
   * @brief getFilename
   *
   * @param fh A valid file handle to retrieve the name of.
   *
   * @return Gets the name of the given file.
   */
  const char* getFilename(const FileHandle& fh) const;

  /**
   * @brief deleteFile
   *
   * @param fh File handle to delete.
   */
  void deleteFile(const FileHandle& fh) override;

  /**
   * @brief deleteFile
   *
   * @param filename The name of the file to delete.
   */
  ECardResult deleteFile(const char* filename) override;

  /**
   * @brief deleteFile
   *
   * @param fileno The file number indicating the file to delete.
   */
  ECardResult deleteFile(uint32_t fileno) override;

  /**
   * @brief renameFile
   *
   * @param oldName The old name of the file.
   * @param newName The new name to assign to the file.
   */
  ECardResult renameFile(const char* oldName, const char* newName) override;

  /**
   * @brief write
   *
   * @param fh   A valid file handle to write to.
   * @param buf  The buffer to write to the file.
   * @param size The size of the given buffer.
   */
  ECardResult fileWrite(FileHandle& fh, const void* buf, size_t size) override;

  /**
   * @brief read
   *
   * @param fh   A valid file handle to read from.
   * @param dst  A buffer to read data into.
   * @param size The size of the buffer to read into.
   */
  ECardResult fileRead(FileHandle& fh, void* dst, size_t size) override;

  /**
   * @brief seek
   *
   * @param fh     A valid file handle.
   * @param pos    The position to seek to.
   * @param whence The origin to seek relative to.
   */
  void seek(FileHandle& fh, int32_t pos, SeekOrigin whence) override;

  /**
   * @brief Returns the current offset of the specified file.
   *
   * @param fh The file to retrieve the offset from.
   *
   * @return The offset or -1 if an invalid handle is passed.
   */
  int32_t tell(const FileHandle& fh) const;

  /**
   * @brief setPublic
   *
   * @param fh  The file handle to make public.
   * @param pub Whether or not this file is public (i.e. readable by any game).
   */
  void setPublic(const FileHandle& fh, bool pub);

  /**
   * @brief isPublic
   *
   * @param fh The file handle to query.
   *
   * @return Whether or not this file is public (i.e. readable by any game).
   */
  bool isPublic(const FileHandle& fh) const;

  /**
   * @brief setCanCopy
   *
   * @param fh   The file handle to set as copyable.
   * @param copy Whether or not to set the file handle as copyable.
   */
  void setCanCopy(const FileHandle& fh, bool copy) const;

  /**
   * @brief canCopy
   *
   * @param fh The file handle to query.
   *
   * @return Whether or not this file handle is copyable by the IPL.
   */
  bool canCopy(const FileHandle& fh) const;

  /**
   * @brief setCanMove
   *
   * @param fh   The file handle to set as moveable.
   * @param move Whether or not to set the file handle as movable.
   */
  void setCanMove(const FileHandle& fh, bool move);

  /**
   * @brief canMove
   *
   * @param fh The file handle to query.
   *
   * @return whether or not the file can be moved by the IPL.
   */
  bool canMove(const FileHandle& fh) const;

  /**
   * @brief getStatus
   *
   * @param fh      Handle of requested file
   * @param statOut Structure to fill with file stat
   *
   * @return NOFILE or READY
   */
  ECardResult getStatus(const FileHandle& fh, CardStat& statOut) const override;

  /**
   * @brief getStatus
   *
   * @param fileNo  Number of requested file
   * @param statOut Structure to fill with file stat
   *
   * @return NOFILE or READY
   */
  ECardResult getStatus(uint32_t fileNo, CardStat& statOut) const override;

  /**
   * @brief setStatus
   *
   * @param fh   Handle of requested file
   * @param stat Structure to access for file stat
   *
   * @return NOFILE or READY
   */
  ECardResult setStatus(const FileHandle& fh, const CardStat& stat) override;

  /**
   * @brief setStatus
   *
   * @param fileNo Number of requested file
   * @param stat   Structure to access for file stat
   *
   * @return NOFILE or READY
   */
  ECardResult setStatus(uint32_t fileNo, const CardStat& stat) override;

#if 0 // TODO: Async-friendly implementations
    /**
     * @brief Copies a file from the current Card instance to a specified Card instance
     * @param fh The file to copy
     * @param dest The destination Card instance
     * @return True if successful, false otherwise
     */
    bool copyFileTo(FileHandle& fh, CardRawFile& dest);

    /**
     * @brief moveFileTo
     * @param fh
     * @param dest
     * @return
     */
    bool moveFileTo(FileHandle& fh, CardRawFile& dest);
#endif

  /**
   * @brief Sets the current game, if not null any openFile requests will only return files that match this game
   *
   * @param game The target game id, e.g "GM8E"
   *
   * @sa openFile
   */
  void setCurrentGame(const char* game) override;

  /**
   * @brief Returns the currently selected game.
   *
   * @return The selected game, or nullptr
   */
  const uint8_t* getCurrentGame() const override;

  /**
   * @brief Sets the current maker, if not null any openFile requests will only return files that match this maker.
   *
   * @param maker The target maker id, e.g "01".
   *
   * @sa openFile
   */
  void setCurrentMaker(const char* maker) override;

  /**
   * @brief Returns the currently selected maker.
   *
   * @return The selected maker, or nullptr.
   */
  const uint8_t* getCurrentMaker() const override;

  /**
   * @brief Retrieves the format assigned serial.
   *
   * @param[out] serial Out reference that will contain the serial number.
   */
  void getSerial(uint64_t& serial) override;

  /**
   * @brief Retrieves the checksum values of the Card system header.
   *
   * @param checksum The checksum of the system header.
   * @param inverse  The inverse checksum of the system header.
   */
  void getChecksum(uint16_t& checksum, uint16_t& inverse) const override;

  /**
   * @brief Retrieves the available storage and directory space.
   *
   * @param bytesNotUsed Number of free bytes out.
   * @param filesNotUsed Number of free files out.
   */
  void getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed) const override;

  /**
   * @brief Retrieves the current file's encoding.
   *
   * @param encoding File encoding out.
   */
  void getEncoding(uint16_t& encoding) const override;

  /**
   * @brief Formats the memory card and assigns a new serial.
   *
   * @param deviceId The slot to format.
   * @param size     The desired size of the file @sa ECardSize.
   * @param encoding The desired encoding @sa EEncoding.
   */
  void format(ECardSlot deviceId, ECardSize size = ECardSize::Card2043Mb, EEncoding encoding = EEncoding::ASCII) override;

  /**
   * @brief Returns basic stats about a card image without opening a handle.
   *
   * @return ProbeResults structure.
   */
  ProbeResults probeCardFile(const std::filesystem::path& filename) override;

  /**
   * @brief Writes any changes to the Card instance immediately to disk. <br />
   * <b>Note:</b> <i>Under normal circumstances there is no need to call this function.</i>
   */
  void commit() override;

  /**
   * @brief Opens card image (does nothing if currently open path matches).
   */
  bool open(const std::filesystem::path& filepath) override;

  /**
   * @brief Commits changes to disk and closes host file.
   */
  void close() override;

  /**
   * @brief Access host filename of card.
   *
   * @return A view to the card's filename.
   */
  const std::filesystem::path& cardFilename() const override { return m_filename; }

  /**
   * @brief Gets card-scope error state.
   *
   * @return READY, BROKEN, or NOCARD
   */
  ECardResult getError() const override;

  /**
   * @return Whether or not the card is within a ready state.
   */
  explicit operator bool() const { return getError() == ECardResult::READY; }
};
} // namespace aurora::card

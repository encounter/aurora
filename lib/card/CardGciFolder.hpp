#pragma once
#include <filesystem>
#include <vector>

#include "BlockAllocationTable.hpp"
#include "CommonData.h"
#include "File.hpp"
#include "ICard.hpp"

namespace aurora::card {

class CardGciFolder : public ICard {
private:
  struct GciFile {
    File file;
    size_t fileSize;
    std::u8string filename;
    bool opened = false;
  };

  std::vector<GciFile> m_files;
  std::filesystem::path m_folderPath;
  BlockAllocationTable m_bat;

  EEncoding m_encoding = EEncoding::ASCII;

  char m_game[5] = {'\0'};
  char m_maker[3] = {'\0'};

  GciFile* getFile(FileHandle& fh);
  const GciFile* getFile(FileHandle& fh) const;
  GciFile* getFile(uint32_t idx);
  const GciFile* getFile(uint32_t idx) const;
public:
  CardGciFolder();
  ~CardGciFolder() override = default;

  CardGciFolder(const CardGciFolder& other) = delete;
  CardGciFolder& operator=(const CardGciFolder& other) = delete;
  CardGciFolder(CardGciFolder&& other);
  CardGciFolder& operator=(CardGciFolder&& other);

  void InitCard(const char* game, const char* maker) override;

  ECardResult openFile(const char* filename, FileHandle& handleOut) override;
  ECardResult openFile(uint32_t fileno, FileHandle& handleOut) override;
  ECardResult createFile(const char* filename, size_t size, FileHandle& handleOut) override;
  ECardResult closeFile(FileHandle& fh) override;
  void deleteFile(const FileHandle& fh) override;
  ECardResult deleteFile(const char* filename) override;
  ECardResult deleteFile(uint32_t fileno) override;
  ECardResult renameFile(const char* oldName, const char* newName) override;
  ECardResult fileWrite(FileHandle& fh, const void* buf, size_t size) override;
  ECardResult fileRead(FileHandle& fh, void* dst, size_t size) override;
  void seek(FileHandle& fh, int32_t pos, SeekOrigin whence) override;
  ECardResult getStatus(const FileHandle& fh, CardStat& statOut) const override;
  ECardResult getStatus(uint32_t fileNo, CardStat& statOut) const override;
  ECardResult setStatus(const FileHandle& fh, const CardStat& stat) override;
  ECardResult setStatus(uint32_t fileNo, const CardStat& stat) override;
  void setCurrentGame(const char* game) override;
  const uint8_t* getCurrentGame() const override;
  void setCurrentMaker(const char* maker) override;
  const uint8_t* getCurrentMaker() const override;
  void getSerial(uint64_t& serial) override;
  void getChecksum(uint16_t& checksum, uint16_t& inverse) const override;
  void getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed) const override;
  void getEncoding(uint16_t& encoding) const override;
  void format(ECardSlot deviceId, ECardSize size = ECardSize::Card2043Mb, EEncoding encoding = EEncoding::ASCII) override;
  void commit() override;
  bool open(const std::filesystem::path& filepath) override;
  void close() override;
  const std::filesystem::path& cardFilename() const override;
  ECardResult getError() const override;
  ProbeResults probeCardFile(const std::filesystem::path& filename) override;
};

}

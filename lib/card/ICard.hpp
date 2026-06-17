#pragma once

#include "Constants.hpp"
#include "Util.hpp"
#include <filesystem>

namespace aurora::card {
class ICard {
public:
  virtual ~ICard() = default;

  virtual void InitCard(const char* game = nullptr, const char* maker = nullptr) = 0;

  virtual ECardResult openFile(const char* filename, FileHandle& handleOut) = 0;
  virtual ECardResult openFile(uint32_t fileno, FileHandle& handleOut) = 0;
  virtual ECardResult createFile(const char* filename, size_t size, FileHandle& handleOut) = 0;
  virtual ECardResult closeFile(FileHandle& fh) = 0;
  virtual void deleteFile(const FileHandle& fh) = 0;
  virtual ECardResult deleteFile(const char* filename) = 0;
  virtual ECardResult deleteFile(uint32_t fileno) = 0;
  virtual ECardResult renameFile(const char* oldName, const char* newName) = 0;
  virtual ECardResult fileWrite(FileHandle& fh, const void* buf, size_t size) = 0;
  virtual ECardResult fileRead(FileHandle& fh, void* dst, size_t size) = 0;
  virtual void seek(FileHandle& fh, int32_t pos, SeekOrigin whence) = 0;
  virtual ECardResult getStatus(const FileHandle& fh, CardStat& statOut) const = 0;
  virtual ECardResult getStatus(uint32_t fileNo, CardStat& statOut) const = 0;
  virtual ECardResult setStatus(const FileHandle& fh, const CardStat& stat) = 0;
  virtual ECardResult setStatus(uint32_t fileNo, const CardStat& stat) = 0;
  virtual void setCurrentGame(const char* game) = 0;
  virtual const uint8_t* getCurrentGame() const = 0;
  virtual void setCurrentMaker(const char* maker) = 0;
  virtual const uint8_t* getCurrentMaker() const = 0;
  virtual void getSerial(uint64_t& serial) = 0;
  virtual void getChecksum(uint16_t& checksum, uint16_t& inverse) const = 0;
  virtual void getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed) const = 0;
  virtual void getEncoding(uint16_t& encoding) const = 0;
  virtual void format(ECardSlot deviceId, ECardSize size = ECardSize::Card2043Mb, EEncoding encoding = EEncoding::ASCII) = 0;
  virtual void commit() = 0;
  virtual bool open(const std::filesystem::path& filepath) = 0;
  virtual void close() = 0;
  virtual const std::filesystem::path& cardFilename() const = 0;
  virtual ECardResult getError() const = 0;
  virtual ProbeResults probeCardFile(const std::filesystem::path& filename) = 0;
};
}
#include "CardGciFolder.hpp"

#include <filesystem>

#include "Directory.hpp"
#include "FileIO.hpp"
#include "../internal.hpp"

namespace {
aurora::Module Log("aurora::card");
}

namespace aurora::card {

CardGciFolder::CardGciFolder() {

}

CardGciFolder::CardGciFolder(CardGciFolder&& other) {

}

CardGciFolder& CardGciFolder::operator=(CardGciFolder&& other) {
  return *this;
}

void CardGciFolder::InitCard(const char* game, const char* maker) {
  setCurrentGame(game);
  setCurrentMaker(maker);
}

ECardResult CardGciFolder::openFile(const char* filename, FileHandle& handleOut) {
  int idx = 0;
  for (auto& gciFile : m_files) {
    if (strcmp(filename, gciFile.file.m_filename) == 0) {
      gciFile.opened = true;
      handleOut = FileHandle(idx, 0);
      return ECardResult::READY;
    }
    idx++;
  }

  return ECardResult::NOCARD;
}

ECardResult CardGciFolder::openFile(uint32_t fileno, FileHandle& handleOut) {
  if (m_files.size() > fileno) {
    handleOut = FileHandle(fileno, 0);
    m_files[fileno].opened = true;
    return ECardResult::READY;
  }

  return ECardResult::NOCARD;
}

ECardResult CardGciFolder::createFile(const char* filename, size_t size, FileHandle& handleOut) {
  std::string gciFilename = fmt::format("{}-{}-{}.gci", m_maker, m_game, filename);
  uint16_t neededBlocks = ROUND_UP_8192(size) / BlockSize;

  File gciFileHeader{""};

  std::memcpy(gciFileHeader.m_game, m_game, 4);
  std::memcpy(gciFileHeader.m_maker, m_maker, 2);
  gciFileHeader.m_blockCount = neededBlocks;

  FileIO file((m_folderPath / gciFilename).string(), true);
  file.fileWrite((void*)&gciFileHeader, sizeof(File), 0);

  m_files.push_back({gciFileHeader, gciFilename, false});

  return ECardResult::READY;
}
ECardResult CardGciFolder::closeFile(FileHandle& fh) { return ECardResult::NOCARD; }
void CardGciFolder::deleteFile(const FileHandle& fh) { }
ECardResult CardGciFolder::deleteFile(const char* filename) { return ECardResult::NOCARD; }
ECardResult CardGciFolder::deleteFile(uint32_t fileno) { return ECardResult::NOCARD; }
ECardResult CardGciFolder::renameFile(const char* oldName, const char* newName) { return ECardResult::NOCARD; }
ECardResult CardGciFolder::fileWrite(FileHandle& fh, const void* buf, size_t size) { return ECardResult::NOCARD; }
ECardResult CardGciFolder::fileRead(FileHandle& fh, void* dst, size_t size) { return ECardResult::NOCARD; }
void CardGciFolder::seek(FileHandle& fh, int32_t pos, SeekOrigin whence) { }
ECardResult CardGciFolder::getStatus(const FileHandle& fh, CardStat& statOut) const { return ECardResult::NOCARD; }
ECardResult CardGciFolder::getStatus(uint32_t fileNo, CardStat& statOut) const { return ECardResult::NOCARD; }
ECardResult CardGciFolder::setStatus(const FileHandle& fh, const CardStat& stat) { return ECardResult::NOCARD; }
ECardResult CardGciFolder::setStatus(uint32_t fileNo, const CardStat& stat) { return ECardResult::NOCARD; }

void CardGciFolder::setCurrentGame(const char* game) {
  if (game != nullptr && std::strlen(game) == 4) {
    std::memcpy(m_game, game, 4);
  }
}

const uint8_t* CardGciFolder::getCurrentGame() const {
  if (std::strlen(m_game) == sizeof(m_game) - 1) {
    return reinterpret_cast<const uint8_t*>(m_game);
  }

  return nullptr;
}

void CardGciFolder::setCurrentMaker(const char* maker) {
  if (maker != nullptr && std::strlen(maker) == 2) {
    std::memcpy(m_maker, maker, 2);
  }
}

const uint8_t* CardGciFolder::getCurrentMaker() const {
  if (std::strlen(m_maker) == sizeof(m_maker) - 1) {
    return reinterpret_cast<const uint8_t*>(m_maker);
  }

  return nullptr;
}

void CardGciFolder::getSerial(uint64_t& serial) { }
void CardGciFolder::getChecksum(uint16_t& checksum, uint16_t& inverse) const { }
void CardGciFolder::getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed) const { }
void CardGciFolder::getEncoding(uint16_t& encoding) const { }

void CardGciFolder::format(ECardSlot deviceId, ECardSize size, EEncoding encoding) {
  // only thing we really need to do here is create a folder at the previously stored directory path

  if (!std::filesystem::create_directories(m_folderPath)) {
    Log.error("Failed to create directory: {}", m_folderPath.string());
  }
}

void CardGciFolder::commit() {
  for (auto& gciFile : m_files) {
    if (gciFile.opened) {
      // FileIO file((m_folderPath / gciFile.filename).string(), true);
      // file.fileWrite((void*)&gciFile.file, sizeof(File), 0);
    }
  }
}

bool CardGciFolder::open(std::string_view filepath) {
  m_folderPath = filepath;

  if (!std::filesystem::exists(filepath) || !std::filesystem::is_directory(filepath))
    return false;

  for (const auto& dir : std::filesystem::directory_iterator(filepath)) {
    if (!dir.is_regular_file()) {
      continue;
    }
    auto path = dir.path();

    if (path.extension() != ".gci")
      continue;

    FileIO file(path.string());

    File fileData;
    file.fileRead((void*)&fileData, sizeof(File), 0);
    fileData.swapEndian();

    m_files.push_back({fileData, path.filename().string(), false});
  }

  return true;
}

void CardGciFolder::close() { }
std::string_view CardGciFolder::cardFilename() const { return ""; }
ECardResult CardGciFolder::getError() const { return ECardResult::NOCARD; }

} // namespace aurora::card

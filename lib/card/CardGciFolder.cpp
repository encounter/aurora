#include "CardGciFolder.hpp"

#include <algorithm>
#include <cstring>

#include <SDL3/SDL_filesystem.h>

#include <filesystem>
#include "../io.hpp"

#include "Directory.hpp"
#include "FileIO.hpp"
#include "../internal.hpp"

namespace aurora::card {
namespace {
Module Log("aurora::card");

ECardResult check_new_path(const std::filesystem::path& path) {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  if (ec && ec != std::errc::no_such_file_or_directory) {
    return ECardResult::IOERROR;
  }
  return std::filesystem::exists(status) ? ECardResult::EXIST : ECardResult::READY;
}
} // namespace

std::string gci_filename(std::string_view game, std::string_view maker, std::string_view filename) {
  if (game.size() != 4 || maker.size() != 2 || filename.size() > CARD_FILENAME_MAX ||
      game.find('\0') != std::string_view::npos || maker.find('\0') != std::string_view::npos ||
      filename.find('\0') != std::string_view::npos) {
    return {};
  }
  std::string result = fmt::format("{}-{}-{}", maker, game, filename);
  for (char& c : result) {
    if (static_cast<unsigned char>(c) < 32 || std::strchr("<>:\"/\\|?*", c)) {
      c = '_';
    }
  }
  return result + ".gci";
}

bool is_gci_filename(std::string_view filename) {
  if (filename.size() < 13 || filename.size() > 12 + CARD_FILENAME_MAX || filename[2] != '-' || filename[7] != '-' ||
      !filename.ends_with(".gci")) {
    return false;
  }
  return filename ==
         gci_filename(filename.substr(3, 4), filename.substr(0, 2), filename.substr(8, filename.size() - 12));
}

CardGciFolder::GciFile* CardGciFolder::get_file(uint32_t idx) {
  if (m_error == ECardResult::READY && m_files.size() > idx && !m_files[idx].deleted) {
    return &m_files[idx];
  }
  return nullptr;
}

const CardGciFolder::GciFile* CardGciFolder::get_file(uint32_t idx) const {
  if (m_error == ECardResult::READY && m_files.size() > idx && !m_files[idx].deleted) {
    return &m_files[idx];
  }
  return nullptr;
}

CardGciFolder::GciFile* CardGciFolder::get_open_file(const FileHandle& fh) {
  auto* file = get_file(fh.getFileNo());
  return file != nullptr && file->opened ? file : nullptr;
}

const CardGciFolder::GciFile* CardGciFolder::get_open_file(const FileHandle& fh) const {
  const auto* file = get_file(fh.getFileNo());
  return file != nullptr && file->opened ? file : nullptr;
}

CardGciFolder::CardGciFolder(CardGciFolder&& other) {
  m_files = std::move(other.m_files);
  m_error = other.m_error;
  m_folderPath = other.m_folderPath;
  m_encoding = other.m_encoding;

  CardGciFolder::setCurrentGame(other.m_game);
  CardGciFolder::setCurrentMaker(other.m_maker);
}

CardGciFolder& CardGciFolder::operator=(CardGciFolder&& other) {
  m_files = std::move(other.m_files);
  m_error = other.m_error;
  m_folderPath = other.m_folderPath;
  m_encoding = other.m_encoding;

  CardGciFolder::setCurrentGame(other.m_game);
  CardGciFolder::setCurrentMaker(other.m_maker);

  return *this;
}

void CardGciFolder::InitCard(const char* game, const char* maker) {
  setCurrentGame(game);
  setCurrentMaker(maker);
}

int32_t CardGciFolder::find_file(const char* filename) const {
  if (std::strlen(filename) > CARD_FILENAME_MAX) {
    return -1;
  }
  for (size_t idx = 0; idx < m_files.size(); ++idx) {
    const auto& entry = m_files[idx];
    if (!entry.deleted && std::memcmp(entry.file.m_game, m_game, 4) == 0 &&
        std::memcmp(entry.file.m_maker, m_maker, 2) == 0 &&
        std::strncmp(entry.file.m_filename, filename, CARD_FILENAME_MAX) == 0) {
      return static_cast<int32_t>(idx);
    }
  }
  return -1;
}

ECardResult CardGciFolder::openFile(const char* filename, FileHandle& handleOut) {
  if (m_error != ECardResult::READY) {
    return m_error;
  }
  const auto idx = find_file(filename);
  if (idx < 0) {
    const auto diskName = gci_filename(m_game, m_maker, filename);
    if (diskName.empty() || filename[0] == '\0') {
      return ECardResult::FATAL_ERROR;
    }
    if (check_new_path(m_folderPath / io::fs_path_from_string(diskName)) != ECardResult::READY) {
      Log.error("GCI path '{}' is occupied by a file that does not match its CARD identity", diskName);
      return m_error = ECardResult::IOERROR;
    }
  }
  return idx < 0 ? ECardResult::NOFILE : openFile(static_cast<uint32_t>(idx), handleOut);
}

ECardResult CardGciFolder::openFile(uint32_t fileno, FileHandle& handleOut) {
  if (m_error != ECardResult::READY) {
    return m_error;
  }
  if (auto* file = get_file(fileno)) {
    handleOut = FileHandle{fileno, 0};
    file->opened = true;
    return ECardResult::READY;
  }
  return ECardResult::NOFILE;
}

ECardResult CardGciFolder::createFile(const char* filename, size_t size, FileHandle& handleOut) {
  if (m_error != ECardResult::READY) {
    return m_error;
  }
  if (std::strlen(filename) > CARD_FILENAME_MAX) {
    return ECardResult::NAMETOOLONG;
  }
  if (size == 0 || size % BlockSize != 0) {
    return ECardResult::FATAL_ERROR;
  }
  if (find_file(filename) >= 0) {
    return ECardResult::EXIST;
  }
  int32_t freeBytes, freeFiles;
  getFreeBlocks(freeBytes, freeFiles);
  if (freeFiles <= 0) {
    return ECardResult::NOENT;
  }
  if (size > static_cast<size_t>(freeBytes)) {
    return ECardResult::INSSPACE;
  }

  const auto canonicalName = gci_filename(m_game, m_maker, filename);
  if (canonicalName.empty() || filename[0] == '\0') {
    return ECardResult::FATAL_ERROR;
  }
  const auto diskName = io::fs_path_from_string(canonicalName);
  if (const auto result = check_new_path(m_folderPath / diskName); result != ECardResult::READY) {
    return result;
  }

  File header{filename};
  std::memcpy(header.m_game, m_game, 4);
  std::memcpy(header.m_maker, m_maker, 2);
  header.m_modifiedTime = static_cast<uint32_t>(getGCTime());
  header.m_blockCount = static_cast<uint16_t>(size / BlockSize);
  header.m_firstBlock = FSTBlocks;
  header.m_permissions = EPermissions::Public;
  header.m_iconAddress = UINT32_MAX;
  header.m_commentAddr = UINT32_MAX;
  header.m_animSpeed = 1;

  std::vector<uint8_t> fileBuf;
  fileBuf.resize(sizeof(File) + size);
  File diskHeader = header;
  diskHeader.swapEndian();
  std::memcpy(fileBuf.data(), &diskHeader, sizeof(File));
  if (!io::write_file_atomic(m_folderPath / diskName, fileBuf)) {
    return ECardResult::IOERROR;
  }

  const auto freeSlot = std::ranges::find_if(m_files, [](const auto& file) { return file.deleted; });
  const auto idx = static_cast<uint32_t>(freeSlot - m_files.begin());
  GciFile entry{header, fileBuf.size(), diskName.u8string(), true};
  if (freeSlot == m_files.end()) {
    m_files.push_back(std::move(entry));
  } else {
    *freeSlot = std::move(entry);
  }
  handleOut = FileHandle{idx, 0};
  return ECardResult::READY;
}

ECardResult CardGciFolder::closeFile(FileHandle& fh) {
  auto file = get_open_file(fh);
  if (file) {
    file->opened = false;
    return ECardResult::READY;
  }

  return ECardResult::NOFILE;
}

void CardGciFolder::deleteFile(const FileHandle& fh) { deleteFile(fh.getFileNo()); }

ECardResult CardGciFolder::deleteFile(const char* filename) {
  if (m_error != ECardResult::READY) {
    return m_error;
  }
  const auto idx = find_file(filename);
  return idx < 0 ? ECardResult::NOFILE : deleteFile(static_cast<uint32_t>(idx));
}

ECardResult CardGciFolder::deleteFile(uint32_t fileno) {
  if (m_error != ECardResult::READY) {
    return m_error;
  }
  auto* file = get_file(fileno);
  if (!file) {
    return ECardResult::NOFILE;
  }
  const auto deletedPath = m_folderPath / "_deleted";
  const auto source = io::fs_path_to_string(m_folderPath / file->filename);
  const auto destination = io::fs_path_to_string(deletedPath / file->filename);
  if (!io::create_directories(deletedPath) || !SDL_RenamePath(source.c_str(), destination.c_str())) {
    return ECardResult::IOERROR;
  }
  file->opened = false;
  file->deleted = true;
  file->dirty = false;
  return ECardResult::READY;
}

ECardResult CardGciFolder::renameFile(const char* oldName, const char* newName) {
  if (m_error != ECardResult::READY) {
    return m_error;
  }
  if (std::strlen(newName) > CARD_FILENAME_MAX) {
    return ECardResult::NAMETOOLONG;
  }
  const auto idx = find_file(oldName);
  if (idx < 0) {
    return ECardResult::NOFILE;
  }
  if (find_file(newName) >= 0) {
    return ECardResult::EXIST;
  }
  auto& file = m_files[idx];
  const auto diskName = gci_filename(m_game, m_maker, newName);
  if (diskName.empty() || newName[0] == '\0') {
    return ECardResult::FATAL_ERROR;
  }
  const auto source = m_folderPath / file.filename;
  const auto destination = m_folderPath / io::fs_path_from_string(diskName);
  if (source != destination) {
    if (const auto result = check_new_path(destination); result != ECardResult::READY) {
      return result;
    }
  }

  auto contents = io::read_file(source);
  if (!contents || contents->size() != file.fileSize) {
    return ECardResult::IOERROR;
  }
  File header = file.file;
  std::strncpy(header.m_filename, newName, CARD_FILENAME_MAX);
  File diskHeader = header;
  diskHeader.swapEndian();
  std::memcpy(contents->data(), &diskHeader, sizeof(File));
  if (!io::write_file_atomic(destination, *contents)) {
    return ECardResult::IOERROR;
  }
  if (source != destination && !SDL_RemovePath(io::fs_path_to_string(source).c_str())) {
    if (!SDL_RemovePath(io::fs_path_to_string(destination).c_str())) {
      m_error = ECardResult::IOERROR;
    }
    return ECardResult::IOERROR;
  }
  file.file = header;
  file.filename = destination.filename().u8string();
  file.dirty = false;
  return ECardResult::READY;
}

ECardResult CardGciFolder::fileWrite(FileHandle& fh, const void* buf, size_t size) {
  auto file = get_open_file(fh);
  if (file) {
    const size_t dataSize = file->fileSize - sizeof(File);
    if (fh.offset < 0 || static_cast<size_t>(fh.offset) > dataSize || size > dataSize - fh.offset) {
      return ECardResult::LIMIT;
    }
    FileIO fileIO(m_folderPath / file->filename);
    if (fileIO) {
      if (fileIO.fileWrite(buf, size, sizeof(File) + fh.offset))
        return ECardResult::READY;
      return ECardResult::IOERROR;
    }
    return ECardResult::NOFILE;
  }

  return ECardResult::NOCARD;
}

ECardResult CardGciFolder::fileRead(FileHandle& fh, void* dst, size_t size) {
  auto file = get_open_file(fh);
  if (file) {
    const size_t dataSize = file->fileSize - sizeof(File);
    if (fh.offset < 0 || static_cast<size_t>(fh.offset) > dataSize || size > dataSize - fh.offset) {
      return ECardResult::LIMIT;
    }
    FileIO fileIO(m_folderPath / file->filename);
    if (fileIO) {
      if (fileIO.fileRead(dst, size, sizeof(File) + fh.offset))
        return ECardResult::READY;
      return ECardResult::IOERROR;
    }
    return ECardResult::NOFILE;
  }

  return ECardResult::NOCARD;
}

void CardGciFolder::seek(FileHandle& fh, int32_t pos, SeekOrigin whence) {
  auto file = get_open_file(fh);
  if (file) {
    switch (whence) {
    case SeekOrigin::Begin:
      fh.offset = pos;
      break;
    case SeekOrigin::Current:
      fh.offset += pos;
      break;
    case SeekOrigin::End:
      fh.offset = file->fileSize - sizeof(File) - pos;
      break;
    }
  }
}

ECardResult CardGciFolder::getStatus(const FileHandle& fh, CardStat& statOut) const {
  return getStatus(fh.getFileNo(), statOut);
}

ECardResult CardGciFolder::getStatus(uint32_t fileNo, CardStat& statOut) const {
  auto gciFile = get_file(fileNo);

  if (!gciFile)
    return ECardResult::NOFILE;
  const auto file = &gciFile->file;

  std::strncpy(statOut.x0_fileName, file->m_filename, 32);
  statOut.x20_length = file->m_blockCount * BlockSize;
  statOut.x24_time = file->m_modifiedTime;
  memmove(statOut.x28_gameName.data(), file->m_game, statOut.x28_gameName.size());
  memmove(statOut.x2c_company.data(), file->m_maker, statOut.x2c_company.size());

  statOut.x2e_bannerFormat = file->m_bannerFlags;
  statOut.x30_iconAddr = file->m_iconAddress;
  statOut.x34_iconFormat = file->m_iconFmt;
  statOut.x36_iconSpeed = file->m_animSpeed;
  statOut.x38_commentAddr = file->m_commentAddr;

  if (file->m_iconAddress == UINT32_MAX) {
    statOut.x3c_offsetBanner = UINT32_MAX;
    statOut.x40_offsetBannerTlut = UINT32_MAX;
    statOut.x44_offsetIcon.fill(UINT32_MAX);
    statOut.x64_offsetIconTlut = UINT32_MAX;
    statOut.x68_offsetData = file->m_commentAddr + 64;
  } else {
    uint32_t cur = file->m_iconAddress;
    statOut.x3c_offsetBanner = cur;
    cur += BannerSize(statOut.GetBannerFormat());
    statOut.x40_offsetBannerTlut = cur;
    cur += TlutSize(statOut.GetBannerFormat());
    bool palette = false;
    for (size_t i = 0; i < statOut.x44_offsetIcon.size(); ++i) {
      statOut.x44_offsetIcon[i] = cur;
      const EImageFormat fmt = statOut.GetIconFormat(static_cast<int>(i));
      if (fmt == EImageFormat::C8) {
        palette = true;
      }
      cur += IconSize(fmt);
    }
    if (palette) {
      statOut.x64_offsetIconTlut = cur;
      cur += TlutSize(EImageFormat::C8);
    } else
      statOut.x64_offsetIconTlut = UINT32_MAX;
    statOut.x68_offsetData = cur;
  }

  return ECardResult::READY;
}

ECardResult CardGciFolder::setStatus(const FileHandle& fh, const CardStat& stat) {
  return setStatus(fh.getFileNo(), stat);
}

ECardResult CardGciFolder::setStatus(uint32_t fileNo, const CardStat& stat) {
  auto gciFile = get_file(fileNo);

  if (!gciFile)
    return ECardResult::NOFILE;
  const auto file = &gciFile->file;

  file->m_bannerFlags = stat.x2e_bannerFormat;
  file->m_iconAddress = stat.x30_iconAddr;
  file->m_iconFmt = stat.x34_iconFormat;
  file->m_animSpeed = stat.x36_iconSpeed;
  file->m_commentAddr = stat.x38_commentAddr;
  gciFile->dirty = true;

  return ECardResult::READY;
}

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

void CardGciFolder::getSerial(uint64_t& serial) {
  serial = 0; // TODO
}

void CardGciFolder::getChecksum(uint16_t& checksum, uint16_t& inverse) const {
  // TODO
  checksum = 0;
  inverse = 0;
}

void CardGciFolder::getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed) const {
  bytesNotUsed = (static_cast<uint32_t>(ECardSize::Card2043Mb) * MbitToBlocks - FSTBlocks) * BlockSize;
  filesNotUsed = MaxFiles;
  for (const auto& file : m_files) {
    if (!file.deleted) {
      bytesNotUsed -= file.file.m_blockCount * BlockSize;
      --filesNotUsed;
    }
  }
  bytesNotUsed = std::max(bytesNotUsed, 0);
}

void CardGciFolder::getEncoding(uint16_t& encoding) const { encoding = (uint16_t)m_encoding; }

ECardResult CardGciFolder::format(ECardSlot deviceId, ECardSize size, EEncoding encoding) {
  m_encoding = encoding;
  if (m_folderPath.empty() || !io::create_directories(m_folderPath)) {
    return ECardResult::IOERROR;
  }
  return m_error = ECardResult::READY;
}

ECardResult CardGciFolder::commit() {
  if (m_error != ECardResult::READY) {
    return m_error;
  }
  for (auto& gciFile : m_files) {
    if (gciFile.deleted || !gciFile.dirty) {
      continue;
    }
    FileIO file{m_folderPath / gciFile.filename};
    File tempFile = gciFile.file;
    tempFile.swapEndian();
    if (!file || !file.fileWrite(&tempFile, sizeof(File), 0)) {
      if (file && file.fileRead(&tempFile, sizeof(File), 0)) {
        tempFile.swapEndian();
        gciFile.file = tempFile;
        gciFile.dirty = false;
      } else {
        m_error = ECardResult::IOERROR;
      }
      return ECardResult::IOERROR;
    }
    gciFile.dirty = false;
  }
  return ECardResult::READY;
}

bool CardGciFolder::open(const std::filesystem::path& filepath) {
  m_files.clear();
  m_error = ECardResult::NOCARD;
  m_folderPath = filepath;
  std::vector<GciFile> files;

  std::error_code ec;
  if (!std::filesystem::exists(filepath, ec) || !std::filesystem::is_directory(filepath, ec)) {
    if (ec) {
      Log.warn("Failed to inspect GCI folder '{}': {}", io::fs_path_to_string(filepath), ec.message());
    }
    return false;
  }

  m_error = ECardResult::IOERROR;
  std::filesystem::directory_iterator it{filepath, ec};
  if (ec) {
    Log.warn("Failed to enumerate GCI folder '{}': {}", io::fs_path_to_string(filepath), ec.message());
    return false;
  }

  const std::filesystem::directory_iterator end;
  while (it != end) {
    const auto entry = *it;
    it.increment(ec);
    if (ec) {
      Log.warn("Failed to continue enumerating GCI folder '{}': {}", io::fs_path_to_string(filepath), ec.message());
      return false;
    }
    const auto path = entry.path();
    if (path.extension() != ".gci") {
      continue;
    }
    const auto diskName = io::fs_path_to_string(path.filename());
    if (!is_gci_filename(diskName)) {
      Log.debug("Ignoring GCI file with a noncanonical filename: '{}'", io::fs_path_to_string(path));
      continue;
    }
    const auto status = entry.status(ec);
    if (ec) {
      Log.warn("Failed to inspect GCI folder entry '{}': {}", io::fs_path_to_string(path), ec.message());
      return false;
    }
    if (!std::filesystem::is_regular_file(status)) {
      continue;
    }

    auto file = io::open_file(path, "rb");
    if (!file) {
      Log.warn("Failed to open GCI file '{}'", io::fs_path_to_string(path));
      return false;
    }

    File fileData;
    if (!io::read_exact(file.get(), &fileData, sizeof(File))) {
      Log.warn("Failed to read GCI file '{}'", io::fs_path_to_string(path));
      return false;
    }
    fileData.swapEndian();

    const auto* nameEnd = std::ranges::find(fileData.m_filename, '\0');
    const auto canonicalName = gci_filename({reinterpret_cast<const char*>(fileData.m_game), 4},
                                            {reinterpret_cast<const char*>(fileData.m_maker), 2},
                                            {fileData.m_filename, static_cast<size_t>(nameEnd - fileData.m_filename)});
    if (canonicalName != diskName) {
      Log.debug("Ignoring GCI file whose filename does not match its CARD identity: '{}'", io::fs_path_to_string(path));
      continue;
    }
    if (files.size() == MaxFiles) {
      m_error = ECardResult::BROKEN;
      return false;
    }
    const auto fileSize = SDL_GetIOSize(file.get());
    if (fileSize < static_cast<Sint64>(sizeof(File)) || fileData.m_blockCount == 0 ||
        fileSize != sizeof(File) + static_cast<size_t>(fileData.m_blockCount) * BlockSize) {
      Log.warn("Invalid GCI file size: '{}'", io::fs_path_to_string(path));
      return false;
    }
    files.push_back({fileData, static_cast<size_t>(fileSize), path.filename().u8string(), false});
  }

  m_files = std::move(files);
  m_error = ECardResult::READY;
  return true;
}

void CardGciFolder::close() {
  m_files.clear();
  m_folderPath = "";
  m_error = ECardResult::NOCARD;
}

static std::filesystem::path g_cardFileNameEmpty = "";

const std::filesystem::path& CardGciFolder::cardFilename() const { return g_cardFileNameEmpty; }

ECardResult CardGciFolder::getError() const { return m_error; }

ProbeResults CardGciFolder::probeCardFile(const std::filesystem::path& filename) {
  if (!std::filesystem::exists(filename) || !std::filesystem::is_directory(filename))
    return {ECardResult::NOCARD, 0, 0};
  return {ECardResult::READY, static_cast<uint32_t>(ECardSize::Card2043Mb), BlockSize};
}

} // namespace aurora::card

extern "C" {
size_t aurora_card_gci_filename(const char* game, const char* maker, const char* fileName, char* buffer,
                                size_t capacity) {
  if (buffer != nullptr && capacity != 0) {
    buffer[0] = '\0';
  }
  if (game == nullptr || maker == nullptr || fileName == nullptr) {
    return 0;
  }
  const auto filename = aurora::card::gci_filename(game, maker, fileName);
  if (filename.empty()) {
    return 0;
  }
  const size_t required = filename.size() + 1;
  if (buffer != nullptr && capacity >= required) {
    std::memcpy(buffer, filename.c_str(), required);
  }
  return required;
}
}

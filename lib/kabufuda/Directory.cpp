#include "kabufuda/Directory.hpp"

#include <algorithm>
#include <cstring>

#include "kabufuda/Util.hpp"

namespace kabufuda {
void Directory::swapEndian() {
  std::for_each(std::begin(m_files), std::end(m_files), [](File& f) { f.swapEndian(); });

  m_updateCounter = SBig(m_updateCounter);
  m_checksum = SBig(m_checksum);
  m_checksumInv = SBig(m_checksumInv);
}

void Directory::updateChecksum() {
  swapEndian();
  calculateChecksumBE(reinterpret_cast<uint16_t*>(raw.data()), 0xFFE, &m_checksum, &m_checksumInv);
  swapEndian();
}

bool Directory::valid() const {
  uint16_t ckSum, ckSumInv;
  const_cast<Directory&>(*this).swapEndian();
  calculateChecksumBE(reinterpret_cast<const uint16_t*>(raw.data()), 0xFFE, &ckSum, &ckSumInv);
  bool res = (ckSum == m_checksum && ckSumInv == m_checksumInv);
  const_cast<Directory&>(*this).swapEndian();
  return res;
}

Directory::Directory() {
  raw.fill(0xFF);
  m_updateCounter = 0;
  updateChecksum();
}

Directory::Directory(uint8_t data[]) { std::memcpy(raw.data(), data, BlockSize); }

bool Directory::hasFreeFile() const {
  return std::any_of(m_files.cbegin(), m_files.cend(), [](const auto& file) { return file.m_game[0] == 0xFF; });
}

int32_t Directory::numFreeFiles() const {
  return int32_t(
      std::count_if(m_files.cbegin(), m_files.cend(), [](const auto& file) { return file.m_game[0] == 0xFF; }));
}

File* Directory::getFirstFreeFile(const char* game, const char* maker, const char* filename) {
  const auto iter =
      std::find_if(m_files.begin(), m_files.end(), [](const auto& file) { return file.m_game[0] == 0xFF; });

  if (iter == m_files.cend()) {
    return nullptr;
  }

  *iter = File(filename);
  if (game != nullptr && std::strlen(game) == iter->m_game.size()) {
    std::memcpy(iter->m_game.data(), game, iter->m_game.size());
  }
  if (maker != nullptr && std::strlen(maker) == iter->m_maker.size()) {
    std::memcpy(iter->m_maker.data(), maker, iter->m_maker.size());
  }

  return &*iter;
}

File* Directory::getFirstNonFreeFile(uint32_t start, const char* game, const char* maker) {
  const auto iter = std::find_if(m_files.begin(), m_files.end(), [game, maker](const auto& file) {
    if (file.m_game[0] == 0xFF) {
      return false;
    }

    const auto* const game_ptr = reinterpret_cast<const char*>(file.m_game.data());
    const auto game_size = file.m_game.size();
    if (game != nullptr && std::strlen(game) == game_size && std::strncmp(game_ptr, game, game_size) != 0) {
      return false;
    }

    const auto* const maker_ptr = reinterpret_cast<const char*>(file.m_maker.data());
    const auto maker_size = file.m_maker.size();
    if (maker != nullptr && std::strlen(maker) == maker_size && std::strncmp(maker_ptr, maker, maker_size) != 0) {
      return false;
    }

    return true;
  });

  if (iter == m_files.cend()) {
    return nullptr;
  }

  return &*iter;
}

File* Directory::getFile(const char* game, const char* maker, const char* filename) {
  const auto iter = std::find_if(m_files.begin(), m_files.end(), [=](const auto& file) {
    const auto game_size = file.m_game.size();
    if (game != nullptr && std::strlen(game) == game_size && std::memcmp(file.m_game.data(), game, game_size) != 0) {
      return false;
    }

    const auto maker_size = file.m_maker.size();
    if (maker != nullptr && std::strlen(maker) == maker_size &&
        std::memcmp(file.m_maker.data(), maker, maker_size) != 0) {
      return false;
    }

    return std::strcmp(file.m_filename, filename) == 0;
  });

  if (iter == m_files.cend()) {
    return nullptr;
  }

  return &*iter;
}

File* Directory::getFile(uint32_t idx) {
  if (idx >= m_files.size()) {
    return nullptr;
  }

  return &m_files[idx];
}

int32_t Directory::indexForFile(File* f) {
  if (f == nullptr) {
    return -1;
  }

  const auto it = std::find_if(std::cbegin(m_files), std::cend(m_files), [&f](const File& file) { return f == &file; });
  if (it == std::cend(m_files)) {
    return -1;
  }

  return it - std::cbegin(m_files);
}
} // namespace kabufuda

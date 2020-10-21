#include "kabufuda/Directory.hpp"

#include <algorithm>
#include <cstring>

#include "kabufuda/Util.hpp"

namespace kabufuda {
void Directory::swapEndian() {
  std::for_each(std::begin(data.m_files), std::end(data.m_files), [](File& f) { f.swapEndian(); });

  data.m_updateCounter = SBig(data.m_updateCounter);
  data.m_checksum = SBig(data.m_checksum);
  data.m_checksumInv = SBig(data.m_checksumInv);
}

void Directory::updateChecksum() {
  swapEndian();
  calculateChecksumBE(reinterpret_cast<uint16_t*>(raw.data()), 0xFFE, &data.m_checksum, &data.m_checksumInv);
  swapEndian();
}

bool Directory::valid() const {
  uint16_t ckSum, ckSumInv;
  const_cast<Directory&>(*this).swapEndian();
  calculateChecksumBE(reinterpret_cast<const uint16_t*>(raw.data()), 0xFFE, &ckSum, &ckSumInv);
  const bool res = (ckSum == data.m_checksum && ckSumInv == data.m_checksumInv);
  const_cast<Directory&>(*this).swapEndian();
  return res;
}

Directory::Directory() {
  raw.fill(0xFF);
  data.m_updateCounter = 0;
  updateChecksum();
}

Directory::Directory(const RawData& rawData) : raw{rawData} {}

bool Directory::hasFreeFile() const {
  return std::any_of(data.m_files.cbegin(), data.m_files.cend(),
                     [](const auto& file) { return file.m_game[0] == 0xFF; });
}

int32_t Directory::numFreeFiles() const {
  return int32_t(std::count_if(data.m_files.cbegin(), data.m_files.cend(),
                               [](const auto& file) { return file.m_game[0] == 0xFF; }));
}

File* Directory::getFirstFreeFile(const char* game, const char* maker, const char* filename) {
  const auto iter =
      std::find_if(data.m_files.begin(), data.m_files.end(), [](const auto& file) { return file.m_game[0] == 0xFF; });

  if (iter == data.m_files.cend()) {
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
  const auto iter = std::find_if(data.m_files.begin() + start, data.m_files.end(), [game, maker](const auto& file) {
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

  if (iter == data.m_files.cend()) {
    return nullptr;
  }

  return &*iter;
}

File* Directory::getFile(const char* game, const char* maker, const char* filename) {
  const auto iter = std::find_if(data.m_files.begin(), data.m_files.end(), [=](const auto& file) {
    const auto game_size = file.m_game.size();
    if (game != nullptr && std::strlen(game) == game_size && std::memcmp(file.m_game.data(), game, game_size) != 0) {
      return false;
    }

    const auto maker_size = file.m_maker.size();
    if (maker != nullptr && std::strlen(maker) == maker_size &&
        std::memcmp(file.m_maker.data(), maker, maker_size) != 0) {
      return false;
    }

    return std::strncmp(file.m_filename, filename, 32) == 0;
  });

  if (iter == data.m_files.cend()) {
    return nullptr;
  }

  return &*iter;
}

File* Directory::getFile(uint32_t idx) {
  if (idx >= data.m_files.size()) {
    return nullptr;
  }

  return &data.m_files[idx];
}

int32_t Directory::indexForFile(const File* f) const {
  if (f == nullptr) {
    return -1;
  }

  const auto it =
      std::find_if(std::cbegin(data.m_files), std::cend(data.m_files), [&f](const File& file) { return f == &file; });
  if (it == std::cend(data.m_files)) {
    return -1;
  }

  return it - std::cbegin(data.m_files);
}
} // namespace kabufuda

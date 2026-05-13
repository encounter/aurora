#include "FileIO.hpp"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_filesystem.h>

#include <cstdint>
#include <utility>

#include "../fs_helper.hpp"

namespace aurora::card {

FileIO::FileIO(const std::filesystem::path& filename, bool truncate) {
  m_path = fs_path_to_string(filename);;
  SDL_IOStream* stream = nullptr;

  stream = fileOpen(m_path, truncate ? "w+b" : "r+b");

  if (stream != nullptr) {
    SDL_FlushIO(stream);
    SDL_CloseIO(stream);
    m_ready = true;
  }
}

FileIO::FileIO(FileIO&& other) noexcept {
  m_path = std::move(other.m_path);
  m_ready = std::exchange(other.m_ready, false);
}

FileIO& FileIO::operator=(FileIO&& other) noexcept {
  if (this != &other) {
    m_path = std::move(other.m_path);
    m_ready = std::exchange(other.m_ready, false);
  }
  return *this;
}

bool FileIO::fileRead(void* buf, size_t length, off_t offset) {
  if (!isReady()) {
    return false;
  }

  SDL_IOStream* stream = fileOpen(m_path, "rb");
  if (stream == nullptr) {
    return false;
  }

  if (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) < 0) {
    SDL_CloseIO(stream);
    return false;
  }

  size_t total = 0;
  auto* dst = static_cast<uint8_t*>(buf);
  while (total < length) {
    const size_t read = SDL_ReadIO(stream, dst + total, length - total);
    if (read == 0) {
      SDL_CloseIO(stream);
      return false;
    }
    total += read;
  }

  SDL_CloseIO(stream);
  return true;
}

bool FileIO::fileWrite(const void* buf, size_t length, off_t offset) {
  if (!isReady()) {
    return false;
  }

  SDL_IOStream* stream = fileOpen(m_path, "r+b");
  if (stream == nullptr) {
    stream = fileOpen(m_path, "w+b");
  }
  if (stream == nullptr) {
    return false;
  }

  if (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) < 0) {
    SDL_FlushIO(stream);
    SDL_CloseIO(stream);
    return false;
  }

  size_t total = 0;
  auto* src = static_cast<const uint8_t*>(buf);
  while (total < length) {
    const size_t written = SDL_WriteIO(stream, src + total, length - total);
    if (written == 0) {
      SDL_CloseIO(stream);
      return false;
    }
    total += written;
  }

  SDL_FlushIO(stream);
  SDL_CloseIO(stream);
  return true;
}

size_t FileIO::fileSize() const {
  SDL_PathInfo info;
  const auto tr = fs_path_to_string(m_path);
  if (SDL_GetPathInfo(m_path.c_str(), &info)) {
    return info.size;
  }
  return 0;
}

bool FileIO::deleteFile() {
  if (SDL_RemovePath(m_path.c_str())) {
    m_ready = false;
    m_path.clear();
    return true;
  }
  return false;
}

FileIO::operator bool() const { return isReady(); }

} // namespace aurora::card

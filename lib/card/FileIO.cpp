#include "FileIO.hpp"

#include <SDL3/SDL_iostream.h>

#include <cstdint>
#include <utility>

namespace aurora::card {

FileIO::FileIO(std::string_view filename, bool truncate) {
  m_path = filename;

  m_stream = fileOpen(m_path.c_str(), truncate ? "w+b" : "r+b");

  if (m_stream != nullptr) {
    m_ready = true;
  }
}

FileIO::~FileIO() {
  if (m_stream != nullptr) {
    SDL_FlushIO(m_stream);
    SDL_CloseIO(m_stream);
    m_stream = nullptr;
  }
}

FileIO::FileIO(FileIO&& other) noexcept {
  m_path = std::move(other.m_path);
  m_stream = std::exchange(other.m_stream, nullptr);
  m_ready = std::exchange(other.m_ready, false);
}

FileIO& FileIO::operator=(FileIO&& other) noexcept {
  if (this != &other) {
    if (m_stream != nullptr) {
      SDL_FlushIO(m_stream);
      SDL_CloseIO(m_stream);
    }
    m_path = std::move(other.m_path);
    m_stream = std::exchange(other.m_stream, nullptr);
    m_ready = std::exchange(other.m_ready, false);
  }
  return *this;
}

bool FileIO::fileRead(void* buf, size_t length, off_t offset) {
  if (!isReady()) {
    return false;
  }

  if (SDL_SeekIO(m_stream, offset, SDL_IO_SEEK_SET) < 0) {
    return false;
  }

  size_t total = 0;
  auto* dst = static_cast<uint8_t*>(buf);
  while (total < length) {
    const size_t read = SDL_ReadIO(m_stream, dst + total, length - total);
    if (read == 0) {
      return false;
    }
    total += read;
  }

  return true;
}

bool FileIO::fileWrite(const void* buf, size_t length, off_t offset) {
  if (!isReady()) {
    return false;
  }

  if (SDL_SeekIO(m_stream, offset, SDL_IO_SEEK_SET) < 0) {
    return false;
  }

  size_t total = 0;
  auto* src = static_cast<const uint8_t*>(buf);
  while (total < length) {
    const size_t written = SDL_WriteIO(m_stream, src + total, length - total);
    if (written == 0) {
      return false;
    }
    total += written;
  }

  return true;
}

void FileIO::flush() {
  if (m_stream != nullptr) {
    SDL_FlushIO(m_stream);
  }
}

FileIO::operator bool() const { return isReady(); }

} // namespace aurora::card

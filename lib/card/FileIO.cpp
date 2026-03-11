#include "FileIO.hpp"

#include <SDL3/SDL_iostream.h>

#include <cstdint>
#include <utility>

namespace aurora::card {

bool FileIO::isReady() const { return m_stream != nullptr; }

FileIO::FileIO(std::string_view filename, bool truncate) {
  m_path = filename;
  if (!truncate) {
    m_stream = SDL_IOFromFile(m_path.c_str(), "r+b");
  }
  if (m_stream == nullptr) {
    m_stream = SDL_IOFromFile(m_path.c_str(), "w+b");
  }
}

FileIO::~FileIO() {
  if (m_stream != nullptr) {
    SDL_CloseIO(m_stream);
    m_stream = nullptr;
  }
}

FileIO::FileIO(FileIO&& other) noexcept {
  m_stream = std::exchange(other.m_stream, nullptr);
  m_path = std::move(other.m_path);
}

FileIO& FileIO::operator=(FileIO&& other) noexcept {
  if (this != &other) {
    if (m_stream != nullptr) {
      SDL_CloseIO(m_stream);
    }
    m_stream = std::exchange(other.m_stream, nullptr);
    m_path = std::move(other.m_path);
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

FileIO::operator bool() const { return isReady(); }

} // namespace aurora::card

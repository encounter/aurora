#include "FileIO.hpp"

#include <SDL3/SDL_filesystem.h>

#include <limits>
#include <utility>

#include "../io.hpp"

namespace aurora::card {

FileIO::FileIO(const std::filesystem::path& filename, bool truncate) : m_path(filename) {
  if (m_path.empty()) {
    return;
  }
  auto stream = io::open_file(m_path, truncate ? "w+b" : "r+b");
  m_ready = stream && SDL_CloseIO(stream.release());
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
  if (!isReady() || offset < 0) {
    return false;
  }
  auto stream = io::open_file(m_path, "rb");
  return stream && io::read_at(stream.get(), static_cast<uint64_t>(offset), buf, length);
}

bool FileIO::fileWrite(const void* buf, size_t length, off_t offset) {
  if (!isReady() || offset < 0) {
    return false;
  }
  auto stream = io::open_file(m_path, "r+b");
  if (!stream) {
    stream = io::open_file(m_path, "w+b");
  }
  return stream && io::write_at(stream.get(), static_cast<uint64_t>(offset), buf, length) &&
         SDL_FlushIO(stream.get()) && SDL_CloseIO(stream.release());
}

size_t FileIO::fileSize() const {
  SDL_PathInfo info;
  const auto path = io::fs_path_to_string(m_path);
  if (SDL_GetPathInfo(path.c_str(), &info) && info.size <= std::numeric_limits<size_t>::max()) {
    return static_cast<size_t>(info.size);
  }
  return 0;
}

bool FileIO::deleteFile() {
  const auto path = io::fs_path_to_string(m_path);
  if (SDL_RemovePath(path.c_str())) {
    m_ready = false;
    m_path.clear();
    return true;
  }
  return false;
}

FileIO::operator bool() const { return isReady(); }

} // namespace aurora::card

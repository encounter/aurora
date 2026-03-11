#pragma once

#include <cstddef>
#include <string>

#include <SDL3/SDL_iostream.h>

namespace aurora::card {

class FileIO {
  std::string m_path;
  SDL_IOStream* m_stream = nullptr;

  bool isReady() const;

public:
  FileIO() = default;
  explicit FileIO(std::string_view filename, bool truncate = false);
  ~FileIO();

  FileIO(FileIO&& other) noexcept;
  FileIO& operator=(FileIO&& other) noexcept;
  FileIO(const FileIO& other) = delete;
  FileIO& operator=(const FileIO& other) = delete;

  bool fileRead(void* buf, size_t length, off_t offset);
  bool fileWrite(const void* buf, size_t length, off_t offset);
  explicit operator bool() const;
};

} // namespace aurora::card

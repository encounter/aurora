#pragma once

#include <cstddef>
#include <string>

#include <SDL3/SDL_iostream.h>

namespace aurora::card {

class FileIO {
  std::string m_path;
  SDL_IOStream* m_stream = nullptr;
  bool m_ready = false;

  bool isReady() const { return m_ready && m_stream != nullptr; };

public:
  FileIO() = default;
  explicit FileIO(std::string_view filename, bool truncate = false);
  ~FileIO();

  FileIO(FileIO&& other) noexcept;
  FileIO& operator=(FileIO&& other) noexcept;
  FileIO(const FileIO& other) = delete;
  FileIO& operator=(const FileIO& other) = delete;

  static SDL_IOStream* fileOpen(const std::string& path, const char* mode) {return path.empty() ? nullptr : SDL_IOFromFile(path.c_str(), mode);}
  bool fileRead(void* buf, size_t length, off_t offset);
  bool fileWrite(const void* buf, size_t length, off_t offset);
  void flush();
  explicit operator bool() const;
};

} // namespace aurora::card

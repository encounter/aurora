#pragma once

#include <filesystem>
#include <fstream>
#include <cstddef>

#include "Util.hpp"

namespace aurora::card {

class FileIO {
  std::filesystem::path m_path;
  std::fstream m_stream;
  uintmax_t m_size;

  bool isReady() const;

public:
  FileIO() = default;
  explicit FileIO(std::string_view filename, bool truncate = false);
  ~FileIO();

  FileIO(FileIO&& other);
  FileIO& operator=(FileIO&& other);
  FileIO(const FileIO* other) = delete;
  FileIO& operator=(const FileIO& other) = delete;

  bool fileRead(void* buf, size_t length, off_t offset);
  bool fileWrite(const void* buf, size_t length, off_t offset);
  explicit operator bool() const;
};

} // namespace aurora::card

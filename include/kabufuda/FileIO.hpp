#pragma once
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#ifdef __SWITCH__
#include <sys/types.h>
using SizeReturn = ssize_t;
#else
#include <aio.h>
using SizeReturn = ssize_t;
#endif
#else
using SizeReturn = unsigned long;
#endif

#include <cstddef>
#include <vector>

#include "kabufuda/Util.hpp"

namespace kabufuda {
#if _WIN32
struct AsyncIOInner;
#endif

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

} // namespace kabufuda

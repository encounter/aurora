#pragma once

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
  AsyncIOInner* m_inner = nullptr;
  bool isReady() const;
public:
  FileIO() = default;
  explicit FileIO(std::string_view filename, bool truncate = false);
  ~FileIO();

  FileIO(FileIO&& other);
  FileIO& operator=(FileIO&& other);
  FileIO(const FileIO* other) = delete;
  FileIO& operator=(const FileIO& other) = delete;

  bool asyncRead(size_t qIdx, void* buf, size_t length, off_t offset);
  bool asyncWrite(size_t qIdx, const void* buf, size_t length, off_t offset);
  ECardResult pollStatus(size_t qIdx, SizeReturn* szRet = nullptr) const;
  ECardResult pollStatus() const;
  void waitForCompletion() const;
  explicit operator bool() const;
};

} // namespace kabufuda

#include "kabufuda/AsyncIO.hpp"

#include <cstdio>
#include <cstring>

namespace kabufuda {

AsyncIO::AsyncIO(std::string_view filename, bool truncate) { m_fd = fopen(filename.data(), truncate ? "rw" : "ra"); }

AsyncIO::~AsyncIO() {
  if (*this) {
    fclose(m_fd);
  }
}

AsyncIO::AsyncIO(AsyncIO&& other) {
  m_fd = other.m_fd;
  other.m_fd = nullptr;
  m_maxBlock = other.m_maxBlock;
}

AsyncIO& AsyncIO::operator=(AsyncIO&& other) {
  if (*this) {
    fclose(m_fd);
  }
  m_fd = other.m_fd;
  other.m_fd = nullptr;
  m_maxBlock = other.m_maxBlock;
  return *this;
}

void AsyncIO::_waitForOperation(size_t qIdx) const {}

bool AsyncIO::asyncRead(size_t qIdx, void* buf, size_t length, off_t offset) {
  return fread(buf, length, offset, m_fd);
}

bool AsyncIO::asyncWrite(size_t qIdx, const void* buf, size_t length, off_t offset) {
  return fwrite(buf, length, offset, m_fd);
}

ECardResult AsyncIO::pollStatus(size_t qIdx, SizeReturn* szRet) const { return ECardResult::READY; }

ECardResult AsyncIO::pollStatus() const { return ECardResult::READY; }

void AsyncIO::waitForCompletion() const {}

} // namespace kabufuda

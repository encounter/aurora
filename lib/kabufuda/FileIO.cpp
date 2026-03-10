#include "kabufuda/FileIO.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace kabufuda {


struct AsyncIOInner {
  std::filesystem::path m_path;
  std::fstream m_stream;
  uintmax_t m_size;
};

bool FileIO::isReady() const {
  return m_inner != nullptr && std::filesystem::exists(m_inner->m_path);
}

FileIO::FileIO(std::string_view filename, bool truncate) : m_inner(new AsyncIOInner) {
  m_inner->m_path = filename;
  int flags = std::ios::in | std::ios::out | std::ios::binary;
  bool exists = std::filesystem::exists(m_inner->m_path);

  if (exists) {
    if (truncate)
      flags |= std::ios::trunc;

    m_inner->m_stream.open(filename, flags);
    m_inner->m_size = std::filesystem::file_size(filename);
  }else {
    m_inner->m_stream.open(filename, std::ios::out | std::ios::binary);
    m_inner->m_stream.flags(flags);
    m_inner->m_size = 0;
  }
}

FileIO::~FileIO() {
  if (m_inner) {
    m_inner->m_stream.flush();
    m_inner->m_stream.close();
    delete m_inner;
  }
}

FileIO::FileIO(FileIO&& other) {
  delete m_inner;
  m_inner = other.m_inner;
  other.m_inner = nullptr;
}

FileIO& FileIO::operator=(FileIO&& other) {
  delete m_inner;
  m_inner = other.m_inner;
  other.m_inner = nullptr;
  return *this;
}

bool FileIO::asyncRead(size_t qIdx, void* buf, size_t length, off_t offset) {
  if (!isReady())
    return false;

  if (m_inner->m_size < offset + length)
    return false;

  m_inner->m_stream.seekg(offset);
  m_inner->m_stream.read(reinterpret_cast<char*>(buf), length);

  return true;
}

bool FileIO::asyncWrite(size_t qIdx, const void* buf, size_t length, off_t offset) {
  if (!isReady())
    return false;

  m_inner->m_stream.seekp(offset);
  m_inner->m_stream.write(reinterpret_cast<const char*>(buf), length);

  if (m_inner->m_size < offset + length)
    m_inner->m_size = std::filesystem::file_size(m_inner->m_path);

  return true;
}

ECardResult FileIO::pollStatus(size_t qIdx, SizeReturn* szRet) const {
  if (!isReady())
    return ECardResult::NOCARD;

  if (szRet)
    *szRet = m_inner->m_size;

  return ECardResult::READY;
}

ECardResult FileIO::pollStatus() const {
  if (!isReady())
    return ECardResult::NOCARD;
  return ECardResult::READY;
}

void FileIO::waitForCompletion() const {}

FileIO::operator bool() const { return isReady(); }

} // namespace kabufuda

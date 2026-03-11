#include "FileIO.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace aurora::card {

bool FileIO::isReady() const { return std::filesystem::exists(m_path); }

FileIO::FileIO(std::string_view filename, bool truncate) {
  m_path = filename;
  int flags = std::ios::in | std::ios::out | std::ios::binary;
  bool exists = std::filesystem::exists(m_path);

  if (exists) {
    if (truncate)
      flags |= std::ios::trunc;

    m_stream.open(filename, flags);
    m_size = std::filesystem::file_size(filename);
  } else {
    m_stream.open(filename, std::ios::out | std::ios::binary);
    m_stream.flags(flags);
    m_size = 0;
  }
}

FileIO::~FileIO() {}

FileIO::FileIO(FileIO&& other) {
  m_stream = std::move(other.m_stream);
  m_path = std::move(other.m_path);
  m_size = other.m_size;
}

FileIO& FileIO::operator=(FileIO&& other) {
  m_stream = std::move(other.m_stream);
  m_path = std::move(other.m_path);
  m_size = other.m_size;
  return *this;
}

bool FileIO::fileRead(void* buf, size_t length, off_t offset) {
  if (!isReady())
    return false;

  if (m_size < offset + length)
    return false;

  m_stream.seekg(offset);
  m_stream.read(static_cast<char*>(buf), length);

  return true;
}

bool FileIO::fileWrite(const void* buf, size_t length, off_t offset) {
  if (!isReady())
    return false;

  m_stream.seekp(offset);
  m_stream.write(static_cast<const char*>(buf), length);

  if (m_size < offset + length)
    m_size = std::filesystem::file_size(m_path);

  return true;
}

FileIO::operator bool() const { return isReady(); }

} // namespace aurora::card

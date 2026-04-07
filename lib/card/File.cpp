#include "File.hpp"

#include <cstring>
#include <iterator>

#include "../internal.hpp"

namespace aurora::card {
File::File() { raw.fill(0xFF); }

File::File(const RawData& rawData) : raw{rawData} {}

File::File(const char* filename) {
  raw.fill(0);
  std::memset(m_filename, 0, std::size(m_filename));
  std::strncpy(m_filename, filename, std::size(m_filename) - 1);
}
void File::swapEndian() {
  m_modifiedTime = bswap(m_modifiedTime);
  m_iconAddress = bswap(m_iconAddress);
  m_iconFmt = bswap(m_iconFmt);
  m_animSpeed = bswap(m_animSpeed);
  m_firstBlock = bswap(m_firstBlock);
  m_blockCount = bswap(m_blockCount);
  m_reserved2 = bswap(m_reserved2);
  m_commentAddr = bswap(m_commentAddr);
}
} // namespace aurora::card

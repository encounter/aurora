#include "kabufuda/File.hpp"

#include <cstring>
#include <iterator>

#include "kabufuda/Util.hpp"

namespace kabufuda {
File::File() { raw.fill(0xFF); }

File::File(char data[]) { std::memcpy(raw.data(), data, raw.size()); }

File::File(const char* filename) {
  raw.fill(0);
  std::memset(m_filename, 0, std::size(m_filename));
  std::strncpy(m_filename, filename, std::size(m_filename));
}
void File::swapEndian() {
  m_modifiedTime = SBig(m_modifiedTime);
  m_iconAddress = SBig(m_iconAddress);
  m_iconFmt = SBig(m_iconFmt);
  m_animSpeed = SBig(m_animSpeed);
  m_firstBlock = SBig(m_firstBlock);
  m_blockCount = SBig(m_blockCount);
  m_reserved2 = SBig(m_reserved2);
  m_commentAddr = SBig(m_commentAddr);
}
} // namespace kabufuda

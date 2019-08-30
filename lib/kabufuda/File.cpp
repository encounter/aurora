#include "kabufuda/File.hpp"

#include <cstring>

#include "kabufuda/Util.hpp"

namespace kabufuda {
File::File() { memset(__raw, 0xFF, 0x40); }

File::File(char data[]) { memcpy(__raw, data, 0x40); }

File::File(const char* filename) {
  memset(__raw, 0, 0x40);
  memset(m_filename, 0, 32);
  strncpy(m_filename, filename, 32);
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

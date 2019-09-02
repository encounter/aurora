#include "kabufuda/Card.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "kabufuda/SRAM.hpp"

namespace kabufuda {

#define ROUND_UP_8192(val) (((val) + 8191) & ~8191)

static void NullFileAccess() { fprintf(stderr, "Attempted to access null file\n"); }

void Card::CardHeader::_swapEndian() {
  m_formatTime = SBig(m_formatTime);
  m_sramBias = SBig(m_sramBias);
  m_sramLanguage = SBig(m_sramLanguage);
  m_unknown = SBig(m_unknown);
  m_deviceId = SBig(m_deviceId);
  m_sizeMb = SBig(m_sizeMb);
  m_encoding = SBig(m_encoding);
  m_updateCounter = SBig(m_updateCounter);
  m_checksum = SBig(m_checksum);
  m_checksumInv = SBig(m_checksumInv);
}

Card::Card() { m_ch.raw.fill(0xFF); }

Card::Card(Card&& other) {
  m_ch.raw = other.m_ch.raw;
  m_filename = std::move(other.m_filename);
  m_fileHandle = std::move(other.m_fileHandle);
  m_dirs = std::move(other.m_dirs);
  m_bats = std::move(other.m_bats);
  m_currentDir = other.m_currentDir;
  m_currentBat = other.m_currentBat;

  m_maxBlock = other.m_maxBlock;
  std::copy(std::cbegin(other.m_game), std::cend(other.m_game), std::begin(m_game));
  std::copy(std::cbegin(other.m_maker), std::cend(other.m_maker), std::begin(m_maker));
}

Card& Card::operator=(Card&& other) {
  close();

  m_ch.raw = other.m_ch.raw;
  m_filename = std::move(other.m_filename);
  m_fileHandle = std::move(other.m_fileHandle);
  m_dirs = std::move(other.m_dirs);
  m_bats = std::move(other.m_bats);
  m_currentDir = other.m_currentDir;
  m_currentBat = other.m_currentBat;

  m_maxBlock = other.m_maxBlock;
  std::copy(std::cbegin(other.m_game), std::cend(other.m_game), std::begin(m_game));
  std::copy(std::cbegin(other.m_maker), std::cend(other.m_maker), std::begin(m_maker));

  return *this;
}

ECardResult Card::_pumpOpen() {
  if (m_opened)
    return ECardResult::READY;

  if (!m_fileHandle)
    return ECardResult::NOCARD;

  ECardResult res = m_fileHandle.pollStatus();
  if (res == ECardResult::READY) {
    m_ch._swapEndian();
    m_maxBlock = m_ch.m_sizeMb * MbitToBlocks;
    m_fileHandle.resizeQueue(m_maxBlock);

    m_dirs[0].swapEndian();
    m_dirs[1].swapEndian();
    m_bats[0].swapEndian();
    m_bats[1].swapEndian();

    /* Check for data integrity, restoring valid data in case of corruption if possible */
    if (!m_dirs[0].valid() && m_dirs[1].valid())
      m_dirs[0] = m_dirs[1];
    else if (!m_dirs[1].valid() && m_dirs[0].valid())
      m_dirs[1] = m_dirs[0];
    if (!m_bats[0].valid() && m_bats[1].valid())
      m_bats[0] = m_bats[1];
    else if (!m_bats[1].valid() && m_bats[0].valid())
      m_bats[1] = m_bats[0];

    if (m_dirs[0].m_updateCounter > m_dirs[1].m_updateCounter)
      m_currentDir = 0;
    else
      m_currentDir = 1;

    if (m_bats[0].m_updateCounter > m_bats[1].m_updateCounter)
      m_currentBat = 0;
    else
      m_currentBat = 1;

    m_opened = true;
  }

  return res;
}

Card::Card(const char* game, const char* maker) {
  m_ch.raw.fill(0xFF);

  if (game != nullptr && std::strlen(game) == 4) {
    std::memcpy(m_game, game, 4);
  }
  if (maker != nullptr && std::strlen(maker) == 2) {
    std::memcpy(m_maker, maker, 2);
  }
}

Card::~Card() { close(); }

ECardResult Card::openFile(const char* filename, FileHandle& handleOut) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  handleOut = {};
  File* f = m_dirs[m_currentDir].getFile(m_game, m_maker, filename);
  if (!f || f->m_game[0] == 0xFF)
    return ECardResult::NOFILE;
  int32_t idx = m_dirs[m_currentDir].indexForFile(f);
  if (idx != -1) {
    handleOut = FileHandle(idx);
    return ECardResult::READY;
  }
  return ECardResult::FATAL_ERROR;
}

ECardResult Card::openFile(uint32_t fileno, FileHandle& handleOut) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  handleOut = {};
  File* f = m_dirs[m_currentDir].getFile(fileno);
  if (!f || f->m_game[0] == 0xFF)
    return ECardResult::NOFILE;
  handleOut = FileHandle(fileno);
  return ECardResult::READY;
}

void Card::_updateDirAndBat(const Directory& dir, const BlockAllocationTable& bat) {
  m_currentDir = !m_currentDir;
  Directory& updateDir = m_dirs[m_currentDir];
  updateDir = dir;
  updateDir.m_updateCounter++;
  updateDir.updateChecksum();

  m_currentBat = !m_currentBat;
  BlockAllocationTable& updateBat = m_bats[m_currentBat];
  updateBat = bat;
  updateBat.m_updateCounter++;
  updateBat.updateChecksum();

  m_dirty = true;
}

void Card::_updateChecksum() {
  m_ch._swapEndian();
  calculateChecksumBE(reinterpret_cast<uint16_t*>(m_ch.raw.data()), 0xFE, &m_ch.m_checksum, &m_ch.m_checksumInv);
  m_ch._swapEndian();
}

File* Card::_fileFromHandle(const FileHandle& fh) const {
  if (!fh) {
    NullFileAccess();
    return nullptr;
  }
  return const_cast<Directory&>(m_dirs[m_currentDir]).getFile(fh.idx);
}

ECardResult Card::createFile(const char* filename, size_t size, FileHandle& handleOut) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  handleOut = {};

  if (size <= 0)
    return ECardResult::FATAL_ERROR;
  if (strlen(filename) > 32)
    return ECardResult::NAMETOOLONG;
  if (m_dirs[m_currentDir].getFile(m_game, m_maker, filename))
    return ECardResult::EXIST;
  uint16_t neededBlocks = ROUND_UP_8192(size) / BlockSize;
  if (neededBlocks > m_bats[m_currentBat].numFreeBlocks())
    return ECardResult::INSSPACE;
  if (!m_dirs[m_currentDir].hasFreeFile())
    return ECardResult::NOENT;

  Directory dir = m_dirs[m_currentDir];
  BlockAllocationTable bat = m_bats[m_currentBat];
  File* f = dir.getFirstFreeFile(m_game, m_maker, filename);
  uint16_t block = bat.allocateBlocks(neededBlocks, m_maxBlock - FSTBlocks);
  if (f && block != 0xFFFF) {
    f->m_modifiedTime = uint32_t(getGCTime());
    f->m_firstBlock = block;
    f->m_blockCount = neededBlocks;

    handleOut = FileHandle(dir.indexForFile(f));
    _updateDirAndBat(dir, bat);
    return ECardResult::READY;
  }

  return ECardResult::FATAL_ERROR;
}

ECardResult Card::closeFile(FileHandle& fh) {
  fh.offset = 0;
  return ECardResult::READY;
}

FileHandle Card::firstFile() {
  File* f = m_dirs[m_currentDir].getFirstNonFreeFile(0, m_game, m_maker);
  if (f)
    return FileHandle(m_dirs[m_currentDir].indexForFile(f));

  return {};
}

FileHandle Card::nextFile(const FileHandle& cur) {
  if (!cur) {
    NullFileAccess();
    return {};
  }
  File* next = m_dirs[m_currentDir].getFirstNonFreeFile(cur.idx + 1, m_game, m_maker);
  if (!next)
    return {};
  return FileHandle(m_dirs[m_currentDir].indexForFile(next));
}

const char* Card::getFilename(const FileHandle& fh) {
  File* f = _fileFromHandle(fh);
  if (!f)
    return nullptr;
  return f->m_filename;
}

void Card::_deleteFile(File& f, BlockAllocationTable& bat) {
  uint16_t block = f.m_firstBlock;
  while (block != 0xFFFF) {
    /* TODO: add a fragmentation check */
    uint16_t nextBlock = bat.getNextBlock(block);
    bat.clear(block, 1);
    block = nextBlock;
  }
  f = File();
}

void Card::deleteFile(const FileHandle& fh) {
  if (!fh) {
    NullFileAccess();
    return;
  }
  Directory dir = m_dirs[m_currentDir];
  BlockAllocationTable bat = m_bats[m_currentBat];
  _deleteFile(*dir.getFile(fh.idx), bat);
  _updateDirAndBat(dir, bat);
}

ECardResult Card::deleteFile(const char* filename) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  Directory dir = m_dirs[m_currentDir];
  File* f = dir.getFile(m_game, m_maker, filename);
  if (!f)
    return ECardResult::NOFILE;

  BlockAllocationTable bat = m_bats[m_currentBat];
  _deleteFile(*f, bat);
  _updateDirAndBat(dir, bat);
  return ECardResult::READY;
}

ECardResult Card::deleteFile(uint32_t fileno) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  Directory dir = m_dirs[m_currentDir];
  File* f = dir.getFile(fileno);
  if (!f)
    return ECardResult::NOFILE;

  BlockAllocationTable bat = m_bats[m_currentBat];
  _deleteFile(*f, bat);
  _updateDirAndBat(dir, bat);
  return ECardResult::READY;
}

ECardResult Card::renameFile(const char* oldName, const char* newName) {
  const ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY) {
    return openRes;
  }

  if (std::strlen(newName) > 32) {
    return ECardResult::NAMETOOLONG;
  }

  Directory dir = m_dirs[m_currentDir];
  File* f = dir.getFile(m_game, m_maker, oldName);
  if (f == nullptr) {
    return ECardResult::NOFILE;
  }

  if (File* replF = dir.getFile(m_game, m_maker, newName)) {
    BlockAllocationTable bat = m_bats[m_currentBat];
    _deleteFile(*replF, bat);
    std::strncpy(f->m_filename, newName, 32);
    _updateDirAndBat(dir, bat);
  } else {
    std::strncpy(f->m_filename, newName, 32);
    _updateDirAndBat(dir, m_bats[m_currentBat]);
  }
  return ECardResult::READY;
}

ECardResult Card::asyncWrite(FileHandle& fh, const void* buf, size_t size) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  if (!fh) {
    NullFileAccess();
    return ECardResult::NOFILE;
  }
  File* file = m_dirs[m_currentDir].getFile(fh.idx);
  if (!file)
    return ECardResult::NOFILE;

  /* Block handling is a little different from cache handling,
   * since each block can be in an arbitrary location we must
   * first find our starting block.
   */
  const uint16_t blockId = uint16_t(fh.offset / BlockSize);
  uint16_t block = file->m_firstBlock;
  for (uint16_t i = 0; i < blockId; i++)
    block = m_bats[m_currentBat].getNextBlock(block);

  const uint8_t* tmpBuf = reinterpret_cast<const uint8_t*>(buf);
  uint16_t curBlock = block;
  uint32_t blockOffset = fh.offset % BlockSize;
  size_t rem = size;
  while (rem) {
    if (curBlock == 0xFFFF)
      return ECardResult::NOFILE;

    size_t cacheSize = rem;
    if (cacheSize + blockOffset > BlockSize)
      cacheSize = BlockSize - blockOffset;
    uint32_t offset = (curBlock * BlockSize) + blockOffset;
    if (!m_fileHandle.asyncWrite(curBlock, tmpBuf, cacheSize, offset))
      return ECardResult::FATAL_ERROR;
    tmpBuf += cacheSize;
    rem -= cacheSize;
    blockOffset += cacheSize;
    if (blockOffset >= BlockSize) {
      curBlock = m_bats[m_currentBat].getNextBlock(curBlock);
      blockOffset = 0;
    }
  }
  fh.offset += size;

  return ECardResult::READY;
}

ECardResult Card::asyncRead(FileHandle& fh, void* dst, size_t size) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  if (!fh) {
    NullFileAccess();
    return ECardResult::NOFILE;
  }
  File* file = m_dirs[m_currentDir].getFile(fh.idx);
  if (!file)
    return ECardResult::NOFILE;
  /* Block handling is a little different from cache handling,
   * since each block can be in an arbitrary location we must
   * first find our starting block.
   */
  const uint16_t blockId = uint16_t(fh.offset / BlockSize);
  uint16_t block = file->m_firstBlock;
  for (uint16_t i = 0; i < blockId; i++)
    block = m_bats[m_currentBat].getNextBlock(block);

  uint8_t* tmpBuf = reinterpret_cast<uint8_t*>(dst);
  uint16_t curBlock = block;
  uint32_t blockOffset = fh.offset % BlockSize;
  size_t rem = size;
  while (rem) {
    if (curBlock == 0xFFFF)
      return ECardResult::NOFILE;

    size_t cacheSize = rem;
    if (cacheSize + blockOffset > BlockSize)
      cacheSize = BlockSize - blockOffset;
    uint32_t offset = (curBlock * BlockSize) + blockOffset;
    if (!m_fileHandle.asyncRead(curBlock, tmpBuf, cacheSize, offset))
      return ECardResult::FATAL_ERROR;
    tmpBuf += cacheSize;
    rem -= cacheSize;
    blockOffset += cacheSize;
    if (blockOffset >= BlockSize) {
      curBlock = m_bats[m_currentBat].getNextBlock(curBlock);
      blockOffset = 0;
    }
  }
  fh.offset += size;

  return ECardResult::READY;
}

void Card::seek(FileHandle& fh, int32_t pos, SeekOrigin whence) {
  if (!fh) {
    NullFileAccess();
    return;
  }
  File* file = m_dirs[m_currentDir].getFile(fh.idx);
  if (!file)
    return;

  switch (whence) {
  case SeekOrigin::Begin:
    fh.offset = pos;
    break;
  case SeekOrigin::Current:
    fh.offset += pos;
    break;
  case SeekOrigin::End:
    fh.offset = int32_t(file->m_blockCount * BlockSize) - pos;
    break;
  }
}

int32_t Card::tell(const FileHandle& fh) { return fh.offset; }

void Card::setPublic(const FileHandle& fh, bool pub) {
  File* file = _fileFromHandle(fh);
  if (!file)
    return;

  if (pub)
    file->m_permissions |= EPermissions::Public;
  else
    file->m_permissions &= ~EPermissions::Public;
}

bool Card::isPublic(const FileHandle& fh) const {
  File* file = _fileFromHandle(fh);
  if (!file)
    return false;

  return bool(file->m_permissions & EPermissions::Public);
}

void Card::setCanCopy(const FileHandle& fh, bool copy) const {
  File* file = _fileFromHandle(fh);
  if (!file)
    return;

  if (copy)
    file->m_permissions &= ~EPermissions::NoCopy;
  else
    file->m_permissions |= EPermissions::NoCopy;
}

bool Card::canCopy(const FileHandle& fh) const {
  File* file = _fileFromHandle(fh);
  if (!file)
    return false;

  return !bool(file->m_permissions & EPermissions::NoCopy);
}

void Card::setCanMove(const FileHandle& fh, bool move) {
  File* file = _fileFromHandle(fh);
  if (!file)
    return;

  if (move)
    file->m_permissions &= ~EPermissions::NoMove;
  else
    file->m_permissions |= EPermissions::NoMove;
}

bool Card::canMove(const FileHandle& fh) const {
  File* file = _fileFromHandle(fh);
  if (!file)
    return false;

  return !bool(file->m_permissions & EPermissions::NoMove);
}

static uint32_t BannerSize(EImageFormat fmt) {
  switch (fmt) {
  default:
  case EImageFormat::None:
    return 0;
  case EImageFormat::C8:
    return 3584;
  case EImageFormat::RGB5A3:
    return 6144;
  }
}

static uint32_t IconSize(EImageFormat fmt) {
  switch (fmt) {
  default:
  case EImageFormat::None:
    return 0;
  case EImageFormat::C8:
    return 1024;
  case EImageFormat::RGB5A3:
    return 2048;
  }
}

static uint32_t TlutSize(EImageFormat fmt) {
  switch (fmt) {
  default:
  case EImageFormat::None:
  case EImageFormat::RGB5A3:
    return 0;
  case EImageFormat::C8:
    return 512;
  }
}

ECardResult Card::getStatus(const FileHandle& fh, CardStat& statOut) const {
  if (!fh) {
    NullFileAccess();
    return ECardResult::NOFILE;
  }
  return getStatus(fh.idx, statOut);
}

ECardResult Card::getStatus(uint32_t fileNo, CardStat& statOut) const {
  ECardResult openRes = const_cast<Card*>(this)->_pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  const File* file = const_cast<Directory&>(m_dirs[m_currentDir]).getFile(fileNo);
  if (!file || file->m_game[0] == 0xFF)
    return ECardResult::NOFILE;

  std::strncpy(statOut.x0_fileName, file->m_filename, 32);
  statOut.x20_length = file->m_blockCount * BlockSize;
  statOut.x24_time = file->m_modifiedTime;
  statOut.x28_gameName = file->m_game;
  statOut.x2c_company = file->m_maker;

  statOut.x2e_bannerFormat = file->m_bannerFlags;
  statOut.x30_iconAddr = file->m_iconAddress;
  statOut.x34_iconFormat = file->m_iconFmt;
  statOut.x36_iconSpeed = file->m_animSpeed;
  statOut.x38_commentAddr = file->m_commentAddr;

  if (file->m_iconAddress == UINT32_MAX) {
    statOut.x3c_offsetBanner = UINT32_MAX;
    statOut.x40_offsetBannerTlut = UINT32_MAX;
    statOut.x44_offsetIcon.fill(UINT32_MAX);
    statOut.x64_offsetIconTlut = UINT32_MAX;
    statOut.x68_offsetData = file->m_commentAddr + 64;
  } else {
    uint32_t cur = file->m_iconAddress;
    statOut.x3c_offsetBanner = cur;
    cur += BannerSize(statOut.GetBannerFormat());
    statOut.x40_offsetBannerTlut = cur;
    cur += TlutSize(statOut.GetBannerFormat());
    bool palette = false;
    for (size_t i = 0; i < statOut.x44_offsetIcon.size(); ++i) {
      statOut.x44_offsetIcon[i] = cur;
      const EImageFormat fmt = statOut.GetIconFormat(int(i));
      if (fmt == EImageFormat::C8) {
        palette = true;
      }
      cur += IconSize(fmt);
    }
    if (palette) {
      statOut.x64_offsetIconTlut = cur;
      cur += TlutSize(EImageFormat::C8);
    } else
      statOut.x64_offsetIconTlut = UINT32_MAX;
    statOut.x68_offsetData = cur;
  }

  return ECardResult::READY;
}

ECardResult Card::setStatus(const FileHandle& fh, const CardStat& stat) {
  if (!fh) {
    NullFileAccess();
    return ECardResult::NOFILE;
  }
  return setStatus(fh.idx, stat);
}

ECardResult Card::setStatus(uint32_t fileNo, const CardStat& stat) {
  ECardResult openRes = _pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  Directory dir = m_dirs[m_currentDir];
  File* file = dir.getFile(fileNo);
  if (!file || file->m_game[0] == 0xFF)
    return ECardResult::NOFILE;

  file->m_bannerFlags = stat.x2e_bannerFormat;
  file->m_iconAddress = stat.x30_iconAddr;
  file->m_iconFmt = stat.x34_iconFormat;
  file->m_animSpeed = stat.x36_iconSpeed;
  file->m_commentAddr = stat.x38_commentAddr;

  _updateDirAndBat(dir, m_bats[m_currentBat]);
  return ECardResult::READY;
}

#if 0 // TODO: Async-friendly implementations
bool Card::copyFileTo(FileHandle& fh, Card& dest)
{
    if (!canCopy(fh))
        return false;

    /* Do a self test to avoid adding a file to itself */
    if (this == &dest)
        return false;

    /* Now to add fh */
    File* toCopy = _fileFromHandle(fh);
    if (!toCopy)
        return false;

    /* Check to make sure dest does not already contain fh */
    FileHandle tmpHandle;
    dest.openFile(toCopy->m_filename, tmpHandle);
    if (tmpHandle)
        return false;

    /* Try to allocate a new file */
    dest.createFile(toCopy->m_filename, toCopy->m_blockCount * BlockSize, tmpHandle);
    if (!tmpHandle)
        return false;

    /* Now copy the file information over */
    File* copyDest = dest._fileFromHandle(tmpHandle);
    File copyTmp = *copyDest;
    *copyDest = *toCopy;
    copyDest->m_firstBlock = copyTmp.m_firstBlock;
    copyDest->m_copyCounter++;

    /* Finally lets get the data copied over! */
    uint32_t len = toCopy->m_blockCount * BlockSize;
    uint32_t oldPos = tell(fh);
    seek(fh, 0, SeekOrigin::Begin);
    while (len > 0)
    {
        uint8_t tmp[BlockSize];
        read(fh, tmp, BlockSize);
        dest.write(tmpHandle, tmp, BlockSize);
        len -= BlockSize;
    }

    seek(fh, oldPos, SeekOrigin::Begin);
    return true;
}

bool Card::moveFileTo(FileHandle& fh, Card& dest)
{
    if (copyFileTo(fh, dest) && canMove(fh))
    {
        deleteFile(fh);
        return true;
    }

    return false;
}
#endif

void Card::setCurrentGame(const char* game) {
  if (game == nullptr) {
    std::memset(m_game, 0, sizeof(m_game));
    return;
  }

  constexpr size_t copy_amount = sizeof(m_game) - 1;
  if (std::strlen(game) != copy_amount) {
    return;
  }

  std::memcpy(m_game, game, copy_amount);
}

const uint8_t* Card::getCurrentGame() const {
  if (std::strlen(m_game) == sizeof(m_game) - 1) {
    return reinterpret_cast<const uint8_t*>(m_game);
  }

  return nullptr;
}

void Card::setCurrentMaker(const char* maker) {
  if (maker == nullptr) {
    std::memset(m_maker, 0, sizeof(m_maker));
    return;
  }

  constexpr size_t copy_amount = sizeof(m_maker) - 1;
  if (std::strlen(maker) != copy_amount) {
    return;
  }

  std::memcpy(m_maker, maker, copy_amount);
}

const uint8_t* Card::getCurrentMaker() const {
  if (std::strlen(m_maker) == sizeof(m_maker) - 1) {
    return reinterpret_cast<const uint8_t*>(m_maker);
  }

  return nullptr;
}

void Card::getSerial(uint64_t& serial) {
  m_ch._swapEndian();

  std::array<uint32_t, 8> serialBuf{};
  for (size_t i = 0; i < serialBuf.size(); i++) {
    serialBuf[i] = SBig(*reinterpret_cast<uint32_t*>(m_ch.raw.data() + (i * 4)));
  }
  serial = uint64_t(serialBuf[0] ^ serialBuf[2] ^ serialBuf[4] ^ serialBuf[6]) << 32 |
           (serialBuf[1] ^ serialBuf[3] ^ serialBuf[5] ^ serialBuf[7]);

  m_ch._swapEndian();
}

void Card::getChecksum(uint16_t& checksum, uint16_t& inverse) {
  checksum = m_ch.m_checksum;
  inverse = m_ch.m_checksumInv;
}

void Card::getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed) {
  bytesNotUsed = m_bats[m_currentBat].numFreeBlocks() * 0x2000;
  filesNotUsed = m_dirs[m_currentDir].numFreeFiles();
}

static std::unique_ptr<uint8_t[]> DummyBlock;

void Card::format(ECardSlot id, ECardSize size, EEncoding encoding) {
  m_ch.raw.fill(0xFF);

  uint64_t rand = uint64_t(getGCTime());
  m_ch.m_formatTime = rand;
  for (size_t i = 0; i < m_ch.m_serial.size(); i++) {
    rand = (((rand * uint64_t(0x41c64e6d)) + uint64_t(0x3039)) >> 16);
    m_ch.m_serial[i] = uint8_t(g_SRAM.flash_id[uint32_t(id)][i] + uint32_t(rand));
    rand = (((rand * uint64_t(0x41c64e6d)) + uint64_t(0x3039)) >> 16);
    rand &= uint64_t(0x7fffULL);
  }

  m_ch.m_sramBias = int32_t(SBig(g_SRAM.counter_bias));
  m_ch.m_sramLanguage = uint32_t(SBig(g_SRAM.lang));
  m_ch.m_unknown = 0; /* 1 works for slot A, 0 both */
  m_ch.m_deviceId = 0;
  m_ch.m_sizeMb = uint16_t(size);
  m_maxBlock = m_ch.m_sizeMb * MbitToBlocks;
  m_ch.m_encoding = uint16_t(encoding);
  _updateChecksum();
  m_dirs[0] = Directory();
  m_dirs[1] = m_dirs[0];
  m_bats[0] = BlockAllocationTable(uint32_t(size) * MbitToBlocks);
  m_bats[1] = m_bats[0];
  m_currentDir = 1;
  m_currentBat = 1;

  m_fileHandle = {};
  m_fileHandle = AsyncIO(m_filename.c_str(), true);

  if (m_fileHandle) {
    const uint32_t blockCount = (uint32_t(size) * MbitToBlocks) - 5;

    m_tmpCh = m_ch;
    m_tmpCh._swapEndian();
    m_fileHandle.resizeQueue(5 + blockCount);
    m_fileHandle.asyncWrite(0, m_tmpCh.raw.data(), BlockSize, 0);
    m_tmpDirs[0] = m_dirs[0];
    m_tmpDirs[0].swapEndian();
    m_fileHandle.asyncWrite(1, m_tmpDirs[0].raw.data(), BlockSize, BlockSize * 1);
    m_tmpDirs[1] = m_dirs[1];
    m_tmpDirs[1].swapEndian();
    m_fileHandle.asyncWrite(2, m_tmpDirs[1].raw.data(), BlockSize, BlockSize * 2);
    m_tmpBats[0] = m_bats[0];
    m_tmpBats[0].swapEndian();
    m_fileHandle.asyncWrite(3, m_tmpBats[0].raw.data(), BlockSize, BlockSize * 3);
    m_tmpBats[1] = m_bats[1];
    m_tmpBats[1].swapEndian();
    m_fileHandle.asyncWrite(4, m_tmpBats[1].raw.data(), BlockSize, BlockSize * 4);
    if (!DummyBlock) {
      DummyBlock.reset(new uint8_t[BlockSize]);
      memset(DummyBlock.get(), 0xFF, BlockSize);
    }
    for (uint32_t i = 0; i < blockCount; ++i) {
      m_fileHandle.asyncWrite(i + 5, DummyBlock.get(), BlockSize, BlockSize * (i + 5));
    }
    m_dirty = false;
  }
}

ProbeResults Card::probeCardFile(SystemStringView filename) {
  Sstat stat;
  if (Stat(filename.data(), &stat) || !S_ISREG(stat.st_mode))
    return {ECardResult::NOCARD, 0, 0};
  return {ECardResult::READY, uint32_t(stat.st_size / BlockSize) / MbitToBlocks, 0x2000};
}

void Card::commit() {
  if (!m_dirty)
    return;
  if (m_fileHandle) {
    m_tmpCh = m_ch;
    m_tmpCh._swapEndian();
    m_fileHandle.asyncWrite(0, &m_tmpCh, BlockSize, 0);
    m_tmpDirs[0] = m_dirs[0];
    m_tmpDirs[0].updateChecksum();
    m_tmpDirs[0].swapEndian();
    m_fileHandle.asyncWrite(1, m_tmpDirs[0].raw.data(), BlockSize, BlockSize * 1);
    m_tmpDirs[1] = m_dirs[1];
    m_tmpDirs[1].updateChecksum();
    m_tmpDirs[1].swapEndian();
    m_fileHandle.asyncWrite(2, m_tmpDirs[1].raw.data(), BlockSize, BlockSize * 2);
    m_tmpBats[0] = m_bats[0];
    m_tmpBats[0].updateChecksum();
    m_tmpBats[0].swapEndian();
    m_fileHandle.asyncWrite(3, m_tmpBats[0].raw.data(), BlockSize, BlockSize * 3);
    m_tmpBats[1] = m_bats[1];
    m_tmpBats[1].updateChecksum();
    m_tmpBats[1].swapEndian();
    m_fileHandle.asyncWrite(4, m_tmpBats[1].raw.data(), BlockSize, BlockSize * 4);
    m_dirty = false;
  }
}

bool Card::open(SystemStringView filepath) {
  m_opened = false;
  m_filename = filepath;
  m_fileHandle = AsyncIO(m_filename);
  if (m_fileHandle) {
    m_fileHandle.resizeQueue(5);
    if (!m_fileHandle.asyncRead(0, m_ch.raw.data(), BlockSize, 0))
      return false;
    if (!m_fileHandle.asyncRead(1, m_dirs[0].raw.data(), BlockSize, BlockSize * 1))
      return false;
    if (!m_fileHandle.asyncRead(2, m_dirs[1].raw.data(), BlockSize, BlockSize * 2))
      return false;
    if (!m_fileHandle.asyncRead(3, m_bats[0].raw.data(), BlockSize, BlockSize * 3))
      return false;
    if (!m_fileHandle.asyncRead(4, m_bats[1].raw.data(), BlockSize, BlockSize * 4))
      return false;
    return true;
  }
  return false;
}

void Card::close() {
  m_opened = false;
  if (m_fileHandle) {
    commit();
    m_fileHandle.waitForCompletion();
    m_fileHandle = {};
  }
}

ECardResult Card::getError() const {
  if (!m_fileHandle)
    return ECardResult::NOCARD;

  ECardResult pollRes = m_fileHandle.pollStatus();
  if (pollRes != ECardResult::READY)
    return pollRes;

  ECardResult openRes = const_cast<Card*>(this)->_pumpOpen();
  if (openRes != ECardResult::READY)
    return openRes;

  uint16_t ckSum, ckSumInv;
  const_cast<Card&>(*this).m_ch._swapEndian();
  calculateChecksumBE(reinterpret_cast<const uint16_t*>(m_ch.raw.data()), 0xFE, &ckSum, &ckSumInv);
  bool res = (ckSum == m_ch.m_checksum && ckSumInv == m_ch.m_checksumInv);
  const_cast<Card&>(*this).m_ch._swapEndian();

  if (!res)
    return ECardResult::BROKEN;
  if (!m_dirs[0].valid() && !m_dirs[1].valid())
    return ECardResult::BROKEN;
  if (!m_bats[0].valid() && !m_bats[1].valid())
    return ECardResult::BROKEN;

  return ECardResult::READY;
}

void Card::waitForCompletion() const {
  if (!m_fileHandle)
    return;
  m_fileHandle.waitForCompletion();
}
} // namespace kabufuda

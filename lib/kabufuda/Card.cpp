#include "kabufuda/Card.hpp"
#include "kabufuda/SRAM.hpp"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <memory>

namespace kabufuda
{

IFileHandle::~IFileHandle()
{
}

class FileHandle : public IFileHandle
{
    friend class Card;
    const char* game;
    const char* maker;
    const char* filename;
    int32_t offset =0;
public:
    FileHandle() = default;
    FileHandle(const char* game, const char* maker, const char* filename)
        : game(game),
          maker(maker),
          filename(filename)
    {}
    virtual ~FileHandle();
};

FileHandle::~FileHandle()
{}


void Card::_swapEndian()
{
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

Card::Card()
{
    memset(__raw, 0xFF, BlockSize);
}

Card::Card(const Card& other)
{
    memcpy(__raw, other.__raw, BlockSize);
    m_dir = other.m_dir;
    m_dirBackup = other.m_dirBackup;
    m_bat = other.m_bat;
    m_batBackup = other.m_batBackup;
    memcpy(m_game, other.m_game, 4);
    memcpy(m_maker, other.m_maker, 2);
}

Card::Card(const SystemString& filename, const char* game, const char* maker)
    : m_filename(filename)
{
    memset(__raw, 0xFF, BlockSize);
    if (game && strlen(game) == 4)
        memcpy(m_game, game, 4);
    if (maker && strlen(maker) == 2)
        memcpy(m_maker, maker, 2);

    m_fileHandle = Fopen(m_filename.c_str(), _S("rb"));
    if (m_fileHandle)
    {
        fread(__raw, 1, BlockSize, m_fileHandle);
        m_maxBlock = m_sizeMb * MbitToBlocks;
        _swapEndian();
        fread(m_dir.__raw, 1, BlockSize, m_fileHandle);
        fread(m_dirBackup.__raw, 1, BlockSize, m_fileHandle);
        fread(m_bat.__raw, 1, BlockSize, m_fileHandle);
        fread(m_batBackup.__raw, 1, BlockSize, m_fileHandle);

        m_dir.swapEndian();
        m_dirBackup.swapEndian();
        m_bat.swapEndian();
        m_batBackup.swapEndian();

        /* Check for data integrity, restoring valid data in case of corruption if possible */
        if (!m_dir.valid() && m_dirBackup.valid())
            m_dir = m_dirBackup;
        else if (!m_dirBackup.valid() && m_dir.valid())
            m_dirBackup = m_dir;
        if (!m_bat.valid() && m_batBackup.valid())
            m_bat = m_batBackup;
        else if (!m_batBackup.valid() && m_bat.valid())
            m_batBackup = m_bat;

        if (m_dir.m_updateCounter > m_dirBackup.m_updateCounter)
        {
            m_currentDir = &m_dir;
            m_previousDir = &m_dirBackup;
        }
        else
        {
            m_currentDir = &m_dirBackup;
            m_previousDir = &m_dir;
        }

        if (m_bat.m_updateCounter > m_batBackup.m_updateCounter)
        {
            m_currentBat = &m_bat;
            m_previousBat = &m_batBackup;
        }
        else
        {
            m_currentBat = &m_batBackup;
            m_previousBat = &m_bat;
        }

        /* Close and reopen in read/write mode */
        fclose(m_fileHandle);
        m_fileHandle = nullptr;
        m_fileHandle = Fopen(m_filename.c_str(), _S("r+"));
        rewind(m_fileHandle);
    }
}

Card::~Card()
{
    commit();
    if (m_fileHandle)
        fclose(m_fileHandle);
    m_fileHandle = nullptr;
}

std::unique_ptr<IFileHandle> Card::openFile(const char* filename)
{
    File* f = m_currentDir->getFile(m_game, m_maker, filename);
    if (f)
        return std::unique_ptr<IFileHandle>(new FileHandle(m_game, m_maker, filename));
    return nullptr;
}

void Card::_updateDirAndBat()
{
    Directory updateDir = *m_currentDir;
    updateDir.m_updateCounter++;
    *m_previousDir = updateDir;
    std::swap(m_currentDir, m_previousDir);

    BlockAllocationTable updateBat = *m_currentBat;
    updateBat.m_updateCounter++;
    *m_previousBat = updateBat;
    std::swap(m_currentBat, m_previousBat);
}

void Card::_updateChecksum()
{
    _swapEndian();
    calculateChecksumBE(reinterpret_cast<uint16_t*>(__raw), 0xFE, &m_checksum, &m_checksumInv);
    _swapEndian();
}

File* Card::_fileFromHandle(const std::unique_ptr<IFileHandle> &fh) const
{
    if (!fh)
        return nullptr;

    FileHandle* handle = dynamic_cast<FileHandle*>(fh.get());
    if (!handle)
        return nullptr;

    File* file = m_currentDir->getFile(handle->game, handle->maker, handle->filename);
    return file;
}

std::unique_ptr<IFileHandle> Card::createFile(const char* filename, size_t size)
{
    _updateDirAndBat();
    File* f = m_currentDir->getFirstFreeFile(m_game, m_maker, filename);
    uint16_t block = m_currentBat->allocateBlocks(uint16_t(size / BlockSize), m_maxBlock);
    if (f && block != 0xFFFF)
    {
        f->m_modifiedTime = uint32_t(getGCTime());
        f->m_firstBlock = block;
        f->m_blockCount = uint16_t(size / BlockSize);


        return std::unique_ptr<FileHandle>(new FileHandle(m_game, m_maker, filename));
    }
    return nullptr;
}

void Card::deleteFile(const std::unique_ptr<IFileHandle> &fh)
{
    _updateDirAndBat();
    if (!fh)
        return;
    FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
    uint16_t block = m_currentDir->getFile(f->game, f->maker, f->filename)->m_firstBlock;

    while(block != 0xFFFF)
    {
        /* TODO: add a fragmentation check */
        uint16_t nextBlock = m_currentBat->getNextBlock(block);
        m_currentBat->clear(block, 1);
        block = nextBlock;
    }
    *m_currentDir->getFile(f->game, f->maker, f->filename) = File();

}

void Card::write(const std::unique_ptr<IFileHandle>& fh, const void* buf, size_t size)
{
    if (!fh)
        return;

    if (m_fileHandle)
    {
        FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
        File* file = m_currentDir->getFile(f->game, f->maker, f->filename);
        if (!file)
            return;

        /* Block handling is a little different from cache handling,
         * since each block can be in an arbitrary location we must
         * first find our starting block.
         */
        const uint16_t blockId = uint16_t(f->offset / BlockSize);
        uint16_t block = file->m_firstBlock;
        for (uint16_t i = 0; i < blockId; i++)
            block = m_currentBat->getNextBlock(block);

        const uint8_t* tmpBuf = reinterpret_cast<const uint8_t*>(buf);
        uint16_t curBlock = block;
        uint32_t blockOffset = f->offset % BlockSize;
        size_t rem = size;
        while (rem)
        {
            if (curBlock == 0xFFFF)
                return;

            size_t cacheSize = rem;
            if (cacheSize + blockOffset > BlockSize)
                cacheSize = BlockSize - blockOffset;
            uint32_t offset = (curBlock * BlockSize) + blockOffset;
            fseek(m_fileHandle, offset, SEEK_SET);
            fwrite(tmpBuf, 1, cacheSize, m_fileHandle);
            tmpBuf += cacheSize;
            rem -= cacheSize;
            blockOffset += cacheSize;
            if (blockOffset >= BlockSize)
            {
                curBlock = m_currentBat->getNextBlock(curBlock);
                blockOffset = 0;
            }
        }
        f->offset += size;
    }
}

void Card::read(const std::unique_ptr<IFileHandle> &fh, void *dst, size_t size)
{
    if (!fh)
        return;

    if (m_fileHandle)
    {
        FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
        File* file = m_currentDir->getFile(f->game, f->maker, f->filename);
        if (!file)
            return;
        /* Block handling is a little different from cache handling,
         * since each block can be in an arbitrary location we must
         * first find our starting block.
         */
        const uint16_t blockId = uint16_t(f->offset / BlockSize);
        uint16_t block = file->m_firstBlock;
        for (uint16_t i = 0; i < blockId; i++)
            block = m_currentBat->getNextBlock(block);

        uint8_t* tmpBuf = reinterpret_cast<uint8_t*>(dst);
        uint16_t curBlock = block;
        uint32_t blockOffset = f->offset % BlockSize;
        size_t rem = size;
        while (rem)
        {
            if (curBlock == 0xFFFF)
                return;

            size_t cacheSize = rem;
            if (cacheSize + blockOffset > BlockSize)
                cacheSize = BlockSize - blockOffset;
            uint32_t offset = (curBlock * BlockSize) + blockOffset;
            fseek(m_fileHandle, offset, SEEK_SET);
            fread(tmpBuf, 1, cacheSize, m_fileHandle);
            tmpBuf += cacheSize;
            rem -= cacheSize;
            blockOffset += cacheSize;
            if (blockOffset >= BlockSize)
            {
                curBlock = m_currentBat->getNextBlock(curBlock);
                blockOffset = 0;
            }
        }
        f->offset += size;
    }
}

void Card::seek(const std::unique_ptr<IFileHandle> &fh, int32_t pos, SeekOrigin whence)
{
    if (!fh)
        return;

    FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
    File* file = m_currentDir->getFile(f->game, f->maker, f->filename);
    if (!file)
        return;

    switch(whence)
    {
    case SeekOrigin::Begin:
        f->offset = pos;
        break;
    case SeekOrigin::Current:
        f->offset += pos;
        break;
    case SeekOrigin::End:
        f->offset = int32_t(file->m_blockCount * BlockSize) - pos;
        break;
    }
}

int32_t Card::tell(const std::unique_ptr<IFileHandle> &fh)
{
    if (!fh)
        return -1;
    FileHandle* handle = dynamic_cast<FileHandle*>(fh.get());
    if (!handle)
        return -1;
    return handle->offset;
}

void Card::setPublic(const std::unique_ptr<IFileHandle>& fh, bool pub)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    if (pub)
        file->m_permissions |= EPermissions::Public;
    else
        file->m_permissions &= ~EPermissions::Public;
}

bool Card::isPublic(const std::unique_ptr<IFileHandle> &fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return false;

    return bool(file->m_permissions & EPermissions::Public);
}

void Card::setCanCopy(const std::unique_ptr<IFileHandle> &fh, bool copy) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    if (copy)
        file->m_permissions &= ~EPermissions::NoCopy;
    else
        file->m_permissions |= EPermissions::NoCopy;
}

bool Card::canCopy(const std::unique_ptr<IFileHandle> &fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return false;

    return !bool(file->m_permissions & EPermissions::NoCopy);
}

void Card::setCanMove(const std::unique_ptr<IFileHandle> &fh, bool move)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    if (move)
        file->m_permissions &= ~EPermissions::NoMove;
    else
        file->m_permissions |= EPermissions::NoMove;
}

bool Card::canMove(const std::unique_ptr<IFileHandle> &fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return false;

    return !bool(file->m_permissions & EPermissions::NoMove);
}

void Card::setBannerFormat(const std::unique_ptr<IFileHandle>& fh, EImageFormat fmt)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_bannerFlags = (file->m_bannerFlags & ~3) | (uint8_t(fmt));
}

EImageFormat Card::bannerFormat(const std::unique_ptr<IFileHandle> &fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return EImageFormat::None;
    return EImageFormat(file->m_bannerFlags & 3);
}

void Card::setIconAnimationType(const std::unique_ptr<IFileHandle> &fh, EAnimationType type)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_bannerFlags =  (file->m_bannerFlags & ~4) | uint8_t(type);
}

EAnimationType Card::iconAnimationType(const std::unique_ptr<IFileHandle> &fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
       return EAnimationType::Loop;

    return EAnimationType(file->m_bannerFlags & 4);
}

void Card::setIconFormat(const std::unique_ptr<IFileHandle> &fh, uint32_t idx, EImageFormat fmt)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    file->m_iconFmt = (file->m_iconFmt & ~(3  << (2 * idx))) | (uint16_t(fmt) << (2 * idx));
}

EImageFormat Card::iconFormat(const std::unique_ptr<IFileHandle> &fh, uint32_t idx) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return EImageFormat::None;

    return EImageFormat(file->m_iconFmt >> (2 * (idx)) & 3);
}

void Card::setIconSpeed(const std::unique_ptr<IFileHandle> &fh, uint32_t idx, EAnimationSpeed speed)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    file->m_animSpeed = (file->m_animSpeed  & ~(3 << (2 * idx))) | (uint16_t(speed) << (2 * idx));
}

EAnimationSpeed Card::iconSpeed(const std::unique_ptr<IFileHandle> &fh, uint32_t idx) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return EAnimationSpeed::End;

    return EAnimationSpeed((file->m_animSpeed  >> (2 * (idx))) & 3);
}

void Card::setIconAddress(const std::unique_ptr<IFileHandle> &fh, uint32_t addr)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_iconAddress = addr;
}

int32_t Card::iconAddress(const std::unique_ptr<IFileHandle> &fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return -1;
    return file->m_iconAddress;
}

void Card::setCommentAddress(const std::unique_ptr<IFileHandle> &fh, uint32_t addr)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_commentAddr = addr;
}

int32_t Card::commentAddress(const std::unique_ptr<IFileHandle> &fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return -1;
    return file->m_commentAddr;
}

bool Card::copyFileTo(const std::unique_ptr<IFileHandle> &fh, Card &dest)
{
    if (!fh)
        return false;
    /* Do a self test to avoid adding a file to itself */
    if (this == &dest)
        return false;

    /* Check to make sure dest does not already contain fh */
    if (dest._fileFromHandle(fh) != nullptr)
        return false;

    /* Now to add fh */
    File* toCopy = _fileFromHandle(fh);
    if (!toCopy)
        return false;

    /* Try to allocate a new file */
    std::unique_ptr<IFileHandle> tmpHandle = dest.createFile(toCopy->m_filename, toCopy->m_blockCount * BlockSize);
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

bool Card::moveFileTo(const std::unique_ptr<IFileHandle> &fh, Card &dest)
{
    if (copyFileTo(fh, dest))
    {
        deleteFile(fh);
        return true;
    }

    return false;
}

void Card::setGame(const char* game)
{
    if (game == nullptr)
    {
        memset(m_game, 0, 2);
        return;
    }

    if (strlen(game) != 4)
        return;

    memcpy(m_game, game, 4);
}

const uint8_t* Card::getGame() const
{
    if (strlen(m_game) == 4)
        return reinterpret_cast<const uint8_t*>(m_game);

    return nullptr;
}

void Card::setMaker(const char* maker)
{
    if (maker == nullptr)
    {
        memset(m_maker, 0, 2);
        return;
    }

    if (strlen(maker) != 2)
        return;

    memcpy(m_maker, maker, 2);
}

const uint8_t* Card::getMaker() const
{
    if (strlen(m_maker) == 2)
        return reinterpret_cast<const uint8_t*>(m_maker);
    return nullptr;
}

void Card::getSerial(uint32_t *s0, uint32_t *s1)
{
    uint32_t serial[8];
    for (uint32_t i = 0; i < 8; i++)
        memcpy(&serial[i], reinterpret_cast<uint8_t*>(m_serial + (i * 4)), 4);
    *s0 = serial[0] ^ serial[2] ^ serial[4] ^ serial[6];
    *s1 = serial[1] ^ serial[3] ^ serial[5] ^ serial[7];
}

void Card::getChecksum(uint16_t* checksum, uint16_t* inverse)
{
    *checksum = m_checksum;
    *inverse = m_checksumInv;
}

void Card::format(EDeviceId id, ECardSize size, EEncoding encoding)
{
    memset(__raw, 0xFF, BlockSize);
    uint64_t rand = uint64_t(getGCTime());
    m_formatTime = rand;
    for (int i = 0; i < 12; i++)
    {
        rand = (((rand * uint64_t(0x41c64e6d)) + uint64_t(0x3039)) >> 16);
        m_serial[i] = uint8_t(g_SRAM.flash_id[uint32_t(id)][i] + uint32_t(rand));
        rand = (((rand * uint64_t(0x41c64e6d)) + uint64_t(0x3039)) >> 16);
        rand &= uint64_t(0x7fffULL);
    }

    m_sramBias   = int32_t(SBig(g_SRAM.counter_bias));
    m_sramLanguage = uint32_t(SBig(g_SRAM.lang));
    m_unknown    = 0; /* 1 works for slot A, 0 both */
    m_deviceId    = 0;
    m_sizeMb      = uint16_t(size);
    m_maxBlock    = m_sizeMb * MbitToBlocks;
    m_encoding    = uint16_t(encoding);
    _updateChecksum();
    m_dir         = Directory();
    m_dirBackup   = m_dir;
    m_bat         = BlockAllocationTable(uint32_t(size) * MbitToBlocks);
    m_batBackup   = m_bat;
    m_currentDir  = &m_dirBackup;
    m_previousDir = &m_dir;
    m_currentBat  = &m_batBackup;
    m_previousBat = &m_bat;

    if (m_fileHandle)
        fclose(m_fileHandle);

    m_fileHandle = Fopen(m_filename.c_str(), _S("wb"));

    if (m_fileHandle)
    {
        _swapEndian();
        fwrite(__raw, 1, BlockSize, m_fileHandle);
        _swapEndian();
        Directory tmpDir = m_dir;
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        tmpDir = m_dirBackup;
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        BlockAllocationTable tmpBat = m_bat;
        tmpBat.swapEndian();
        fwrite(tmpBat.__raw, 1, BlockSize, m_fileHandle);
        tmpBat = m_batBackup;
        tmpBat.swapEndian();
        fwrite(tmpBat.__raw, 1, BlockSize, m_fileHandle);
        uint32_t dataLen = ((uint32_t(size) * MbitToBlocks) - 5) * BlockSize;
        std::unique_ptr<uint8_t[]> data(new uint8_t[dataLen]);
        memset(data.get(), 0xFF, dataLen);
        fwrite(data.get(), 1, dataLen, m_fileHandle);
        fclose(m_fileHandle);
        m_fileHandle = Fopen(m_filename.c_str(), _S("r+"));
    }
}

uint32_t Card::getSizeMbitFromFile(const SystemString& filename)
{
    Sstat stat;
    Stat(filename.c_str(), &stat);
    return uint32_t(stat.st_size / BlockSize) / MbitToBlocks;
}

void Card::commit()
{
    if (m_fileHandle)
    {
        rewind(m_fileHandle);

        _swapEndian();
        fwrite(__raw, 1, BlockSize, m_fileHandle);
        _swapEndian();
        Directory tmpDir = m_dir;
        tmpDir.updateChecksum();
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        tmpDir = m_dirBackup;
        tmpDir.updateChecksum();
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        BlockAllocationTable tmpBat = m_bat;
        tmpBat.updateChecksum();
        tmpBat.swapEndian();
        fwrite(tmpBat.__raw, 1, BlockSize, m_fileHandle);
        tmpBat = m_batBackup;
        tmpBat.updateChecksum();
        tmpBat.swapEndian();
        fwrite(tmpBat.__raw, 1, BlockSize, m_fileHandle);
    }
}

Card::operator bool() const
{
    if (m_fileHandle == nullptr)
        return false;

    uint16_t ckSum, ckSumInv;
    Card tmp = *this;
    tmp._swapEndian();
    calculateChecksumBE(reinterpret_cast<const uint16_t*>(tmp.__raw), 0xFE, &ckSum, &ckSumInv);
    if (SBig(ckSum) != m_checksum || SBig(ckSumInv) != m_checksumInv)
        return false;
    if (!m_dir.valid() && !m_dirBackup.valid())
        return false;
    if (!m_bat.valid() && !m_batBackup.valid())
        return false;

    return true;
}
}

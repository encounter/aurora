#include "kabufuda/Card.hpp"
#include "kabufuda/SRAM.hpp"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <memory>

namespace kabufuda
{

#define ROUND_UP_8192(val) (((val) + 8191) & ~8191)

IFileHandle::~IFileHandle() {}

class FileHandle : public IFileHandle
{
    friend class Card;
    int32_t offset = 0;

public:
    FileHandle() = default;
    FileHandle(uint32_t idx) : IFileHandle(idx) {}
    virtual ~FileHandle();
};

FileHandle::~FileHandle() {}

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

Card::Card() { memset(__raw, 0xFF, BlockSize); }

Card::Card(const SystemString& filename, const char* game, const char* maker) : m_filename(filename)
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
        fread(m_dirs[0].__raw, 1, BlockSize, m_fileHandle);
        fread(m_dirs[1].__raw, 1, BlockSize, m_fileHandle);
        fread(m_bats[0].__raw, 1, BlockSize, m_fileHandle);
        fread(m_bats[1].__raw, 1, BlockSize, m_fileHandle);

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

        /* Close and reopen in read/write mode */
        fclose(m_fileHandle);
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

ECardResult Card::openFile(const char* filename, std::unique_ptr<IFileHandle>& handleOut)
{
    handleOut.reset();
    File* f = m_dirs[m_currentDir].getFile(m_game, m_maker, filename);
    if (!f || f->m_game[0] == 0xFF)
        return ECardResult::NOFILE;
    int32_t idx = m_dirs[m_currentDir].indexForFile(f);
    if (idx != -1)
    {
        handleOut = std::make_unique<FileHandle>(idx);
        return ECardResult::READY;
    }
    return ECardResult::FATAL_ERROR;
}

ECardResult Card::openFile(uint32_t fileno, std::unique_ptr<IFileHandle>& handleOut)
{
    handleOut.reset();
    File* f = m_dirs[m_currentDir].getFile(fileno);
    if (!f || f->m_game[0] == 0xFF)
        return ECardResult::NOFILE;
    handleOut = std::make_unique<FileHandle>(fileno);
    return ECardResult::READY;
}

void Card::_updateDirAndBat()
{
    Directory updateDir = m_dirs[m_currentDir];
    updateDir.m_updateCounter++;
    m_dirs[!m_currentDir] = updateDir;
    m_currentDir = !m_currentDir;

    BlockAllocationTable updateBat = m_bats[m_currentBat];
    updateBat.m_updateCounter++;
    m_bats[!m_currentBat] = updateBat;
    m_currentBat = !m_currentBat;
}

void Card::_updateChecksum()
{
    calculateChecksumLE(reinterpret_cast<uint16_t*>(__raw), 0xFE, &m_checksum, &m_checksumInv);
}

File* Card::_fileFromHandle(const std::unique_ptr<IFileHandle>& fh) const
{
    if (!fh)
        return nullptr;

    FileHandle* handle = dynamic_cast<FileHandle*>(fh.get());
    if (!handle)
        return nullptr;

    File* file = const_cast<Directory&>(m_dirs[m_currentDir]).getFile(handle->idx);
    return file;
}

ECardResult Card::createFile(const char* filename, size_t size,
                             std::unique_ptr<IFileHandle>& handleOut)
{
    handleOut.reset();

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

    _updateDirAndBat();
    File* f = m_dirs[m_currentDir].getFirstFreeFile(m_game, m_maker, filename);
    uint16_t block = m_bats[m_currentBat].allocateBlocks(neededBlocks, m_maxBlock - FSTBlocks);
    if (f && block != 0xFFFF)
    {
        f->m_modifiedTime = uint32_t(getGCTime());
        f->m_firstBlock = block;
        f->m_blockCount = neededBlocks;

        handleOut = std::make_unique<FileHandle>(m_dirs[m_currentDir].indexForFile(f));
        return ECardResult::READY;
    }

    return ECardResult::FATAL_ERROR;
}

std::unique_ptr<IFileHandle> Card::firstFile()
{
    File* f = m_dirs[m_currentDir].getFirstNonFreeFile(0, m_game, m_maker);
    if (f)
        return std::make_unique<FileHandle>(m_dirs[m_currentDir].indexForFile(f));

    return nullptr;
}

std::unique_ptr<IFileHandle> Card::nextFile(const std::unique_ptr<IFileHandle>& cur)
{
    FileHandle* handle = dynamic_cast<FileHandle*>(cur.get());
    if (!handle)
        return nullptr;

    File* next = m_dirs[m_currentDir].getFirstNonFreeFile(handle->idx + 1, m_game, m_maker);
    if (!next)
        return nullptr;
    return std::make_unique<FileHandle>(m_dirs[m_currentDir].indexForFile(next));
}

const char* Card::getFilename(const std::unique_ptr<IFileHandle>& fh)
{
    File* f = _fileFromHandle(fh);
    if (!f)
        return nullptr;
    return f->m_filename;
}

void Card::_deleteFile(File& f)
{
    uint16_t block = f.m_firstBlock;
    while (block != 0xFFFF)
    {
        /* TODO: add a fragmentation check */
        uint16_t nextBlock = m_bats[m_currentBat].getNextBlock(block);
        m_bats[m_currentBat].clear(block, 1);
        block = nextBlock;
    }
    f = File();
}

void Card::deleteFile(const std::unique_ptr<IFileHandle>& fh)
{
    _updateDirAndBat();
    if (!fh)
        return;
    FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
    _deleteFile(*m_dirs[m_currentDir].getFile(f->idx));
}

ECardResult Card::deleteFile(const char* filename)
{
    _updateDirAndBat();
    File* f = m_dirs[m_currentDir].getFile(m_game, m_maker, filename);
    if (!f)
        return ECardResult::NOFILE;

    _deleteFile(*f);
    return ECardResult::READY;
}

ECardResult Card::deleteFile(uint32_t fileno)
{
    _updateDirAndBat();
    File* f = m_dirs[m_currentDir].getFile(fileno);
    if (!f)
        return ECardResult::NOFILE;

    _deleteFile(*f);
    return ECardResult::READY;
}

ECardResult Card::renameFile(const char* oldName, const char* newName)
{
    if (strlen(newName) > 32)
        return ECardResult::NAMETOOLONG;

    _updateDirAndBat();
    File* f = m_dirs[m_currentDir].getFile(m_game, m_maker, oldName);
    if (!f)
        return ECardResult::NOFILE;

    strncpy(f->m_filename, newName, 32);
    return ECardResult::READY;
}

void Card::write(const std::unique_ptr<IFileHandle>& fh, const void* buf, size_t size)
{
    if (!fh)
        return;

    if (m_fileHandle)
    {
        FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
        File* file = m_dirs[m_currentDir].getFile(f->idx);
        if (!file)
            return;

        /* Block handling is a little different from cache handling,
         * since each block can be in an arbitrary location we must
         * first find our starting block.
         */
        const uint16_t blockId = uint16_t(f->offset / BlockSize);
        uint16_t block = file->m_firstBlock;
        for (uint16_t i = 0; i < blockId; i++)
            block = m_bats[m_currentBat].getNextBlock(block);

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
                curBlock = m_bats[m_currentBat].getNextBlock(curBlock);
                blockOffset = 0;
            }
        }
        f->offset += size;
    }
}

void Card::read(const std::unique_ptr<IFileHandle>& fh, void* dst, size_t size)
{
    if (!fh)
        return;

    if (m_fileHandle)
    {
        FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
        File* file = m_dirs[m_currentDir].getFile(f->idx);
        if (!file)
            return;
        /* Block handling is a little different from cache handling,
         * since each block can be in an arbitrary location we must
         * first find our starting block.
         */
        const uint16_t blockId = uint16_t(f->offset / BlockSize);
        uint16_t block = file->m_firstBlock;
        for (uint16_t i = 0; i < blockId; i++)
            block = m_bats[m_currentBat].getNextBlock(block);

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
                curBlock = m_bats[m_currentBat].getNextBlock(curBlock);
                blockOffset = 0;
            }
        }
        f->offset += size;
    }
}

void Card::seek(const std::unique_ptr<IFileHandle>& fh, int32_t pos, SeekOrigin whence)
{
    if (!fh)
        return;

    FileHandle* f = dynamic_cast<FileHandle*>(fh.get());
    File* file = m_dirs[m_currentDir].getFile(f->idx);
    if (!file)
        return;

    switch (whence)
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

int32_t Card::tell(const std::unique_ptr<IFileHandle>& fh)
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

bool Card::isPublic(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return false;

    return bool(file->m_permissions & EPermissions::Public);
}

void Card::setCanCopy(const std::unique_ptr<IFileHandle>& fh, bool copy) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    if (copy)
        file->m_permissions &= ~EPermissions::NoCopy;
    else
        file->m_permissions |= EPermissions::NoCopy;
}

bool Card::canCopy(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return false;

    return !bool(file->m_permissions & EPermissions::NoCopy);
}

void Card::setCanMove(const std::unique_ptr<IFileHandle>& fh, bool move)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    if (move)
        file->m_permissions &= ~EPermissions::NoMove;
    else
        file->m_permissions |= EPermissions::NoMove;
}

bool Card::canMove(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return false;

    return !bool(file->m_permissions & EPermissions::NoMove);
}

static uint32_t BannerSize(EImageFormat fmt)
{
    switch (fmt)
    {
    default:
    case EImageFormat::None:
        return 0;
    case EImageFormat::C8:
        return 3584;
    case EImageFormat::RGB5A3:
        return 6144;
    }
}

static uint32_t IconSize(EImageFormat fmt)
{
    switch (fmt)
    {
    default:
    case EImageFormat::None:
        return 0;
    case EImageFormat::C8:
        return 1024;
    case EImageFormat::RGB5A3:
        return 2048;
    }
}

static uint32_t TlutSize(EImageFormat fmt)
{
    switch (fmt)
    {
    default:
    case EImageFormat::None:
    case EImageFormat::RGB5A3:
        return 0;
    case EImageFormat::C8:
        return 512;
    }
}

ECardResult Card::getStatus(const std::unique_ptr<IFileHandle>& fh, CardStat& statOut) const
{
    File* file = _fileFromHandle(fh);
    if (!file || file->m_game[0] == 0xFF)
        return ECardResult::NOFILE;

    strncpy(statOut.x0_fileName, file->m_filename, 32);
    statOut.x20_length = file->m_blockCount * BlockSize;
    statOut.x24_time = file->m_modifiedTime;
    memcpy(statOut.x28_gameName, file->m_game, 4);
    memcpy(statOut.x2c_company, file->m_maker, 2);

    statOut.x2e_bannerFormat = file->m_bannerFlags;
    statOut.x30_iconAddr = file->m_iconAddress;
    statOut.x34_iconFormat = file->m_iconFmt;
    statOut.x36_iconSpeed = file->m_animSpeed;
    statOut.x38_commentAddr = file->m_commentAddr;

    if (file->m_iconAddress == -1)
    {
        statOut.x3c_offsetBanner = -1;
        statOut.x40_offsetBannerTlut = -1;
        for (int i=0 ; i<CARD_ICON_MAX ; ++i)
            statOut.x44_offsetIcon[i] = -1;
        statOut.x64_offsetIconTlut = -1;
        statOut.x68_offsetData = file->m_commentAddr + 64;
    }
    else
    {
        uint32_t cur = file->m_iconAddress;
        statOut.x3c_offsetBanner = cur;
        cur += BannerSize(statOut.GetBannerFormat());
        statOut.x40_offsetBannerTlut = cur;
        cur += TlutSize(statOut.GetBannerFormat());
        bool palette = false;
        for (int i=0 ; i<CARD_ICON_MAX ; ++i)
        {
            statOut.x44_offsetIcon[i] = cur;
            EImageFormat fmt = statOut.GetIconFormat(i);
            if (fmt == EImageFormat::C8)
                palette = true;
            cur += IconSize(fmt);
        }
        if (palette)
        {
            statOut.x64_offsetIconTlut = cur;
            cur += TlutSize(EImageFormat::C8);
        }
        else
            statOut.x64_offsetIconTlut = -1;
        statOut.x68_offsetData = cur;
    }

    return ECardResult::READY;
}

ECardResult Card::setStatus(const std::unique_ptr<IFileHandle>& fh, const CardStat& stat)
{
    File* file = _fileFromHandle(fh);
    if (!file || file->m_game[0] == 0xFF)
        return ECardResult::NOFILE;

    file->m_bannerFlags = stat.x2e_bannerFormat;
    file->m_iconAddress = stat.x30_iconAddr;
    file->m_iconFmt = stat.x34_iconFormat;
    file->m_animSpeed = stat.x36_iconSpeed;
    file->m_commentAddr = stat.x38_commentAddr;

    return ECardResult::READY;
}

const char* Card::gameId(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return nullptr;
    return reinterpret_cast<const char*>(file->m_game);
}

const char* Card::maker(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return nullptr;
    return reinterpret_cast<const char*>(file->m_maker);
}

void Card::setBannerFormat(const std::unique_ptr<IFileHandle>& fh, EImageFormat fmt)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_bannerFlags = (file->m_bannerFlags & ~3) | (uint8_t(fmt));
}

EImageFormat Card::bannerFormat(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return EImageFormat::None;
    return EImageFormat(file->m_bannerFlags & 3);
}

void Card::setIconAnimationType(const std::unique_ptr<IFileHandle>& fh, EAnimationType type)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_bannerFlags = (file->m_bannerFlags & ~4) | uint8_t(type);
}

EAnimationType Card::iconAnimationType(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return EAnimationType::Loop;

    return EAnimationType(file->m_bannerFlags & 4);
}

void Card::setIconFormat(const std::unique_ptr<IFileHandle>& fh, uint32_t idx, EImageFormat fmt)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    file->m_iconFmt = (file->m_iconFmt & ~(3 << (2 * idx))) | (uint16_t(fmt) << (2 * idx));
}

EImageFormat Card::iconFormat(const std::unique_ptr<IFileHandle>& fh, uint32_t idx) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return EImageFormat::None;

    return EImageFormat(file->m_iconFmt >> (2 * (idx)) & 3);
}

void Card::setIconSpeed(const std::unique_ptr<IFileHandle>& fh, uint32_t idx, EAnimationSpeed speed)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;

    file->m_animSpeed = (file->m_animSpeed & ~(3 << (2 * idx))) | (uint16_t(speed) << (2 * idx));
}

EAnimationSpeed Card::iconSpeed(const std::unique_ptr<IFileHandle>& fh, uint32_t idx) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return EAnimationSpeed::End;

    return EAnimationSpeed((file->m_animSpeed >> (2 * (idx))) & 3);
}

void Card::setImageAddress(const std::unique_ptr<IFileHandle>& fh, uint32_t addr)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_iconAddress = addr;
}

int32_t Card::imageAddress(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return -1;
    return file->m_iconAddress;
}

void Card::setCommentAddress(const std::unique_ptr<IFileHandle>& fh, uint32_t addr)
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return;
    file->m_commentAddr = addr;
}

int32_t Card::commentAddress(const std::unique_ptr<IFileHandle>& fh) const
{
    File* file = _fileFromHandle(fh);
    if (!file)
        return -1;
    return file->m_commentAddr;
}

bool Card::copyFileTo(const std::unique_ptr<IFileHandle>& fh, Card& dest)
{
    if (!fh)
        return false;

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
    std::unique_ptr<IFileHandle> tmpHandle;
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

bool Card::moveFileTo(const std::unique_ptr<IFileHandle>& fh, Card& dest)
{
    if (copyFileTo(fh, dest) && canMove(fh))
    {
        deleteFile(fh);
        return true;
    }

    return false;
}

void Card::setCurrentGame(const char* game)
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

const uint8_t* Card::getCurrentGame() const
{
    if (strlen(m_game) == 4)
        return reinterpret_cast<const uint8_t*>(m_game);

    return nullptr;
}

void Card::setCurrentMaker(const char* maker)
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

const uint8_t* Card::getCurrentMaker() const
{
    if (strlen(m_maker) == 2)
        return reinterpret_cast<const uint8_t*>(m_maker);
    return nullptr;
}

void Card::getSerial(uint64_t& serial)
{
    _swapEndian();
    uint32_t serialBuf[8];
    for (uint32_t i = 0; i < 8; i++)
        serialBuf[i] = SBig(*reinterpret_cast<uint32_t*>(__raw + (i * 4)));
    serial = uint64_t(serialBuf[0] ^ serialBuf[2] ^ serialBuf[4] ^ serialBuf[6]) << 32 |
             (serialBuf[1] ^ serialBuf[3] ^ serialBuf[5] ^ serialBuf[7]);
    _swapEndian();
}

void Card::getChecksum(uint16_t& checksum, uint16_t& inverse)
{
    checksum = m_checksum;
    inverse = m_checksumInv;
}

void Card::getFreeBlocks(int32_t& bytesNotUsed, int32_t& filesNotUsed)
{
    bytesNotUsed = m_bats[m_currentBat].numFreeBlocks() * 0x2000;
    filesNotUsed = m_dirs[m_currentDir].numFreeFiles();
}

void Card::format(ECardSlot id, ECardSize size, EEncoding encoding)
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

    m_sramBias = int32_t(SBig(g_SRAM.counter_bias));
    m_sramLanguage = uint32_t(SBig(g_SRAM.lang));
    m_unknown = 0; /* 1 works for slot A, 0 both */
    m_deviceId = 0;
    m_sizeMb = uint16_t(size);
    m_maxBlock = m_sizeMb * MbitToBlocks;
    m_encoding = uint16_t(encoding);
    _updateChecksum();
    m_dirs[0] = Directory();
    m_dirs[1] = m_dirs[0];
    m_bats[0] = BlockAllocationTable(uint32_t(size) * MbitToBlocks);
    m_bats[1] = m_bats[0];
    m_currentDir = 1;
    m_currentBat = 1;

    if (m_fileHandle)
        fclose(m_fileHandle);

    m_fileHandle = Fopen(m_filename.c_str(), _S("wb"));

    if (m_fileHandle)
    {
        _swapEndian();
        fwrite(__raw, 1, BlockSize, m_fileHandle);
        _swapEndian();
        Directory tmpDir = m_dirs[0];
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        tmpDir = m_dirs[1];
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        BlockAllocationTable tmpBat = m_bats[0];
        tmpBat.swapEndian();
        fwrite(tmpBat.__raw, 1, BlockSize, m_fileHandle);
        tmpBat = m_bats[1];
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

ProbeResults Card::probeCardFile(const SystemString& filename)
{
    Sstat stat;
    if (Stat(filename.c_str(), &stat) || !S_ISREG(stat.st_mode))
        return { ECardResult::NOCARD, 0, 0 };
    return { ECardResult::READY, uint32_t(stat.st_size / BlockSize) / MbitToBlocks, 0x2000 };
}

void Card::commit()
{
    if (m_fileHandle)
    {
        rewind(m_fileHandle);

        _swapEndian();
        fwrite(__raw, 1, BlockSize, m_fileHandle);
        _swapEndian();
        Directory tmpDir = m_dirs[0];
        tmpDir.updateChecksum();
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        tmpDir = m_dirs[1];
        tmpDir.updateChecksum();
        tmpDir.swapEndian();
        fwrite(tmpDir.__raw, 1, BlockSize, m_fileHandle);
        BlockAllocationTable tmpBat = m_bats[0];
        tmpBat.updateChecksum();
        tmpBat.swapEndian();
        fwrite(tmpBat.__raw, 1, BlockSize, m_fileHandle);
        tmpBat = m_bats[1];
        tmpBat.updateChecksum();
        tmpBat.swapEndian();
        fwrite(tmpBat.__raw, 1, BlockSize, m_fileHandle);
    }
}

ECardResult Card::getError() const
{
    if (m_fileHandle == nullptr)
        return ECardResult::NOCARD;

    uint16_t ckSum, ckSumInv;
    calculateChecksumLE(reinterpret_cast<const uint16_t*>(__raw), 0xFE, &ckSum, &ckSumInv);
    if (ckSum != m_checksum || ckSumInv != m_checksumInv)
        return ECardResult::BROKEN;
    if (!m_dirs[0].valid() && !m_dirs[1].valid())
        return ECardResult::BROKEN;
    if (!m_bats[0].valid() && !m_bats[1].valid())
        return ECardResult::BROKEN;

    return ECardResult::READY;
}
}

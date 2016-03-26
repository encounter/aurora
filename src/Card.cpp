#include "Card.hpp"
#include "SRAM.hpp"
#include <stdio.h>
#include <string.h>
#include <memory>

namespace kabufuda
{
Card::Card()
{
    memset(__raw, 0xFF, BlockSize);
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
        fread(m_dir.__raw, 1, BlockSize, m_fileHandle);
        fread(m_dirBackup.__raw, 1, BlockSize, m_fileHandle);
        fread(m_bat.__raw, 1, BlockSize, m_fileHandle);
        fread(m_batBackup.__raw, 1, BlockSize, m_fileHandle);
        if (m_dir.m_updateCounter < m_dirBackup.m_updateCounter)
            m_dirInUse = &m_dirBackup;
        else
            m_dirInUse = &m_dir;

        if (m_bat.m_updateCounter < m_batBackup.m_updateCounter)
            m_batInUse = &m_batBackup;
        else
            m_batInUse = &m_bat;
    }
}

Card::~Card()
{
}

void Card::openFile(const char* filename)
{

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
        memcpy(&serial[i], ((uint8_t*)m_serial + (i * 4)), 4);
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
    m_formatTime = SBig(rand);
    for (int i = 0; i < 12; i++)
    {
        rand = (((rand * (uint64_t)0x41c64e6d) + (uint64_t)0x3039) >> 16);
        m_serial[i] = (uint8_t)(g_SRAM.flash_id[uint32_t(id)][i] + (uint32_t)rand);
        rand = (((rand * (uint64_t)0x41c64e6d) + (uint64_t)0x3039) >> 16);
        rand &= (uint64_t)0x7fffULL;
    }

    m_sramBias   = g_SRAM.counter_bias;
    m_sramLanguage = SBig(g_SRAM.lang);
    m_unknown    = 0; /* 1 works for slot A, 0 both */
    m_deviceId    = 0;
    m_sizeMb      = SBig(uint16_t(size));
    m_encoding    = SBig(uint16_t(encoding));
    calculateChecksum((uint16_t*)__raw, 0xFE, &m_checksum, &m_checksumInv);
    m_dir         = Directory();
    m_dirBackup   = m_dir;
    m_dirInUse    = &m_dir;
    m_bat         = BlockAllocationTable(uint32_t(size) * MbitToBlocks);
    m_batBackup   = m_bat;
    m_batInUse    = &m_bat;
    m_sizeMb      = SBig(uint16_t(size));
    m_encoding    = SBig(uint16_t(encoding));

    FILE* f = Fopen(m_filename.c_str(), _S("wb"));
    if (f)
    {
        fwrite(__raw, 1, BlockSize, f);
        fwrite(m_dir.__raw, 1, BlockSize, f);
        fwrite(m_dirBackup.__raw, 1, BlockSize, f);
        fwrite(m_bat.__raw, 1, BlockSize, f);
        fwrite(m_batBackup.__raw, 1, BlockSize, f);
        uint32_t dataLen = ((uint32_t(size) * MbitToBlocks) - 5) * BlockSize;
        std::unique_ptr<char[]> data(new char[dataLen]);
        memset(data.get(), 0xFF, dataLen);
        fwrite(data.get(), 1, dataLen, f);
        fclose(f);
    }
}

uint32_t Card::getSizeMbitFromFile(const SystemString& filename)
{
    Sstat stat;
    Stat(filename.c_str(), &stat);
    return (stat.st_size / BlockSize) / MbitToBlocks;
}

File::File(char data[])
{
    memcpy(__raw, data, 0x40);
}

File::File(const char* filename)
{
    memset(m_filename, 0, 0x20);
    memcpy(m_filename, filename, 32 - strlen(filename));
}

BlockAllocationTable::BlockAllocationTable(uint32_t blockCount)
{
    memset(__raw, 0, BlockSize);
    m_freeBlocks = SBig(uint16_t(blockCount - 5));
    m_lastAllocated = SBig(uint16_t(4));
    calculateChecksum((uint16_t*)(__raw + 4), 0xFFE, &m_checksum, &m_checksumInv);
}

Directory::Directory()
{
    memset(__raw, 0xFF, BlockSize);
    m_updateCounter = 0;
    calculateChecksum((uint16_t*)__raw, 0xFFE, &m_checksum, &m_checksumInv);
}

Directory::Directory(uint8_t data[])
{
    memcpy((uint16_t*)__raw, data, BlockSize);
}


void calculateChecksum(uint16_t* data, size_t len, uint16_t* checksum, uint16_t* checksumInv)
{
    *checksum = 0;
    *checksumInv = 0;
    for (size_t i = 0; i < len; i++)
    {
        *checksum += SBig(data[i]);
        *checksumInv += SBig(uint16_t(data[i] ^ 0xFFFF));
    }
    *checksum = SBig(uint16_t(*checksum));
    *checksumInv = SBig(uint16_t(*checksumInv));
    if (*checksum == 0xFFFF)
        *checksum = 0;
    if (*checksumInv == 0xFFFF)
        *checksumInv = 0;
}

}

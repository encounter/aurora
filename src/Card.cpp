#include "Card.hpp"
#include <stdio.h>
#include <string.h>

namespace card
{
Card::Card()
{
    memset(__raw, 0xFF, BlockSize);
}

Card::Card(const std::string &filepath, const char* game, const char* maker)
    : m_filepath(filepath)
{
    memset(__raw, 0xFF, BlockSize);
    if (game && strlen(game) == 4)
        memcpy(m_game, game, 4);
    if (maker && strlen(maker) == 2)
        memcpy(m_maker, maker, 2);
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

void Card::format(ECardSize size)
{

}

void Card::format()
{
    FILE* file = fopen(m_filename.c_str(), "wb");
}

uint16_t calculateChecksum(void* data, size_t len)
{
    uint16_t ret = 0;
    for (size_t i = 0; i < len; ++i)
        ret += *(uint8_t*)(reinterpret_cast<uint8_t*>(data) + i);

    return ret;
}

}

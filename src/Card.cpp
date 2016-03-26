#include "Card.hpp"
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
    if (game)
        memcpy(m_game, game, 4);
    if (maker)
        memcpy(m_maker, maker, 2);
}

void Card::setGame(const char* id)
{
    if (strlen(id) != 4)
        return;

    memcpy(m_game, id, 4);
}

const uint8_t* Card::getGame() const
{
    return reinterpret_cast<const uint8_t*>(m_game);
}

void Card::setMaker(const char* maker)
{
    if (strlen(maker) != 2)
        return;

    memcpy(m_maker, maker, 2);
}

const uint8_t* Card::getMaker() const
{
    return reinterpret_cast<const uint8_t*>(m_maker);
}
}

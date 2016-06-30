#include "kabufuda/Directory.hpp"
#include "kabufuda/Util.hpp"


namespace kabufuda
{
void Directory::swapEndian()
{
    std::for_each(std::begin(m_files), std::end(m_files), [](File& f){
        f.swapEndian();
    });

    m_updateCounter = SBig(m_updateCounter);
    m_checksum = SBig(m_checksum);
    m_checksumInv = SBig(m_checksumInv);
}

void Directory::updateChecksum()
{
    swapEndian();
    calculateChecksumBE(reinterpret_cast<uint16_t*>(__raw), 0xFFE, &m_checksum, &m_checksumInv);
    swapEndian();
}

bool Directory::valid() const
{
    uint16_t ckSum, ckSumInv;
    Directory tmp = *this;
    tmp.swapEndian();
    calculateChecksumBE(reinterpret_cast<const uint16_t*>(tmp.__raw), 0xFFE, &ckSum, &ckSumInv);
    return (SBig(ckSum) == m_checksum && SBig(ckSumInv) == m_checksumInv);
}

Directory::Directory()
{
    memset(__raw, 0xFF, BlockSize);
    m_updateCounter = 0;
    updateChecksum();
}

Directory::Directory(uint8_t data[])
{
    memcpy(__raw, data, BlockSize);
}

Directory::Directory(const Directory& other)
{
    memcpy(__raw, other.__raw, BlockSize);
}

void Directory::operator=(const Directory& other)
{
    memcpy(__raw, other.__raw, BlockSize);
}

Directory::~Directory()
{
}

File* Directory::getFirstFreeFile(const char* game, const char* maker, const char* filename)
{
    for (uint16_t i = 0 ; i < 127 ; i++)
    {
        if (m_files[i].m_id[0] == 0xFF)
        {
            File* ret = &m_files[i];
            *ret = File(filename);
            if (game && strlen(game) == 4)
                memcpy(ret->m_id, game, 4);
            if (maker && strlen(maker) == 2)
                memcpy(ret->m_maker, maker, 2);
            return ret;
        }
    }

    return nullptr;
}

File* Directory::getFile(const char* game, const char* maker, const char* filename)
{
    for (uint16_t i = 0 ; i < 127 ; i++)
    {
        if (game && strlen(game) == 4 && memcmp(m_files[i].m_id, game, 4))
            continue;
        if (maker && strlen(maker) == 2 && memcmp(m_files[i].m_maker, maker, 2))
            continue;
        if (!strcmp(m_files[i].m_filename, filename))
            return &m_files[i];
    }

    return nullptr;
}
}

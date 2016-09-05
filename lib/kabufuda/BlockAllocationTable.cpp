#include "kabufuda/BlockAllocationTable.hpp"
#include "kabufuda/Util.hpp"
#include <vector>

namespace kabufuda
{
void BlockAllocationTable::swapEndian()
{
    m_checksum = SBig(m_checksum);
    m_checksumInv = SBig(m_checksumInv);
    m_updateCounter = SBig(m_updateCounter);
    m_freeBlocks = SBig(m_freeBlocks);
    m_lastAllocated = SBig(m_lastAllocated);
    std::for_each(std::begin(m_map), std::end(m_map), [](uint16_t& val) { val = SBig(val); });
}

void BlockAllocationTable::updateChecksum()
{
    swapEndian();
    calculateChecksumBE(reinterpret_cast<uint16_t*>(__raw + 4), 0xFFE, &m_checksum, &m_checksumInv);
    swapEndian();
}

bool BlockAllocationTable::valid() const
{
    uint16_t ckSum, ckSumInv;
    BlockAllocationTable tmp = *this;
    tmp.swapEndian();
    calculateChecksumBE(reinterpret_cast<const uint16_t*>(tmp.__raw + 4), 0xFFE, &ckSum, &ckSumInv);
    return (SBig(ckSum) == m_checksum && SBig(ckSumInv) == m_checksumInv);
}

BlockAllocationTable::BlockAllocationTable(uint32_t blockCount)
{
    memset(__raw, 0, BlockSize);
    m_freeBlocks = uint16_t(blockCount - FSTBlocks);
    m_lastAllocated = 4;
    updateChecksum();
}

BlockAllocationTable::~BlockAllocationTable() {}

uint16_t BlockAllocationTable::getNextBlock(uint16_t block) const
{
    if ((block < FSTBlocks) || (block > (BATSize - FSTBlocks)))
        return 0;

    return m_map[block - FSTBlocks];
}

uint16_t BlockAllocationTable::nextFreeBlock(uint16_t maxBlock, uint16_t startingBlock) const
{
    if (m_freeBlocks > 0)
    {
        maxBlock = std::min(maxBlock, uint16_t(BATSize));
        for (uint16_t i = startingBlock; i < maxBlock; ++i)
            if (m_map[i - FSTBlocks] == 0)
                return i;

        for (uint16_t i = FSTBlocks; i < startingBlock; ++i)
            if (m_map[i - FSTBlocks] == 0)
                return i;
    }

    return 0xFFFF;
}

bool BlockAllocationTable::clear(uint16_t first, uint16_t count)
{
    std::vector<uint16_t> blocks;
    while (first != 0xFFFF && first != 0)
    {
        blocks.push_back(first);
        first = getNextBlock(first);
    }
    if (first > 0)
    {
        size_t length = blocks.size();
        if (length != count)
            return false;

        for (size_t i = 0; i < length; ++i)
            m_map[blocks.at(i) - FSTBlocks] = 0;
        m_freeBlocks += count;
        return true;
    }
    return false;
}

uint16_t BlockAllocationTable::allocateBlocks(uint16_t count, uint16_t maxBlocks)
{
    uint16_t firstBlock = nextFreeBlock(maxBlocks - FSTBlocks, m_lastAllocated + 1);
    uint16_t freeBlock = firstBlock;
    if (freeBlock != 0xFFFF)
    {
        uint16_t tmpCount = count;
        while ((count--) > 0)
        {
            m_map[(freeBlock - FSTBlocks)] = 0xFFFF;
            if (count != 0)
            {
                m_map[(freeBlock - FSTBlocks)] = nextFreeBlock(maxBlocks - FSTBlocks, m_lastAllocated + 1);
                freeBlock = m_map[(freeBlock - FSTBlocks)];
            }
            m_lastAllocated = freeBlock;
        }

        m_freeBlocks -= tmpCount;
    }
    return firstBlock;
}
}

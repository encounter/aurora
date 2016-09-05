#include "kabufuda/Util.hpp"
#include <time.h>

namespace kabufuda
{
uint64_t getGCTime()
{
    time_t sysTime, tzDiff, tzDST;
    struct tm* gmTime;

    time(&sysTime);

    // Account for DST where needed
    gmTime = localtime(&sysTime);
    if (gmTime->tm_isdst == 1)
        tzDST = 3600;
    else
        tzDST = 0;

    // Lazy way to get local time in sec
    gmTime = gmtime(&sysTime);
    tzDiff = sysTime - mktime(gmTime);

    return (uint64_t)(sysTime + tzDiff + tzDST) - 0x386D4380;
}

void calculateChecksumBE(const uint16_t* data, size_t len, uint16_t* checksum, uint16_t* checksumInv)
{
    *checksum = 0;
    *checksumInv = 0;
    for (size_t i = 0; i < len; ++i)
    {
        *checksum += SBig(data[i]);
        *checksumInv += SBig(uint16_t(data[i] ^ 0xFFFF));
    }

    *checksum = SBig(*checksum);
    *checksumInv = SBig(*checksumInv);
    if (*checksum == 0xFFFF)
        *checksum = 0;
    if (*checksumInv == 0xFFFF)
        *checksumInv = 0;
}
}

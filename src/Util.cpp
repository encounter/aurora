#include "Util.hpp"
#include <time.h>

namespace kabufuda
{
uint64_t getGCTime()
{
    time_t sysTime, tzDiff, tzDST;
    struct tm * gmTime;

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
}

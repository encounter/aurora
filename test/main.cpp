#include "Card.hpp"
#include <iostream>

int main()
{
    kabufuda::Card mc{_S("test.USA.raw")};
    mc.format(kabufuda::EDeviceId::SlotA, kabufuda::ECardSize::Card123Mb);
    printf("File Mbit %x\n", kabufuda::Card::getSizeMbitFromFile(_S("test.USA.raw")));
    return 0;
}

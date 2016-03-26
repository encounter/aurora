#include "Card.hpp"
#include <iostream>

int main()
{
    kabufuda::Card mc{_S("test.USA.raw"), "GM8E", "01"};
    mc.format(kabufuda::EDeviceId::SlotA, kabufuda::ECardSize::Card123Mb);
    mc.createFile("MetroidPrime A", kabufuda::BlockSize * 2);
    //kabufuda::File* f = mc.openFile("MetroidPrime A");
//    if (f)
//    {
//        char test[] = "Metroid Prime is Cool\0";
//        mc.write(f, test, strlen(test));
//    }
    return 0;
}

#include "Card.hpp"
#include <iostream>

int main()
{
    kabufuda::Card mc{_S("test.USA.raw"), "GM8E", "01"};
    mc.format(kabufuda::EDeviceId::SlotA, kabufuda::ECardSize::Card123Mb);
    mc.createFile("MetroidPrime A", kabufuda::BlockSize);
    kabufuda::FileHandle f = mc.openFile("MetroidPrime A");
    if (f)
    {
        const char* test = "Metroid Prime is Cool";
        size_t len = strlen(test);
        mc.write(&f, test, len + 1);
        uint16_t derp = 1234;
        mc.write(&f, &derp, 2);
    }
    return 0;
}

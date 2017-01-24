#include "kabufuda/Card.hpp"
#include <iostream>

int main()
{
    kabufuda::Card mc{_S("test.USA.raw"), "GM8E", "01"};
    mc.format(kabufuda::ECardSlot::SlotA, kabufuda::ECardSize::Card2043Mb);
    uint64_t a = 0;
    mc.getSerial(a);

    kabufuda::FileHandle f;
    mc.openFile("MetroidPrime A", f);
    for (uint32_t i = 0; i < 127; i++)
    {
        char name[32] = {'\0'};
        sprintf(name, "Metroid Prime %i", i);
        kabufuda::ECardResult res = mc.createFile(name, kabufuda::BlockSize, f);
        if (res == kabufuda::ECardResult::INSSPACE || res == kabufuda::ECardResult::NOFILE)
            break;

        mc.setPublic(f, true);
        mc.setCanCopy(f, true);
        mc.setCanMove(f, true);
        mc.setCommentAddress(f, 0);
        mc.write(f, "Test\0", strlen("Test") + 1);
        mc.seek(f, 32, kabufuda::SeekOrigin::Begin);
        mc.write(f, "Test\0", strlen("Test") + 1);
    }
    return 0;
}

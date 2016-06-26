#include "Card.hpp"
#include <iostream>

int main()
{
    kabufuda::Card mc{_S("test.USA.raw"), "GM8E", "01"};
    //if (!mc)
        mc.format(kabufuda::EDeviceId::SlotA, kabufuda::ECardSize::Card2043Mb);
    std::unique_ptr<kabufuda::IFileHandle> f = mc.openFile("MetroidPrime B");
    if (!f)
        f = mc.createFile("MetroidPrime B", kabufuda::BlockSize);

    if (f)
    {
        const char* test = "Metroid Prime A is Cool";
        size_t len = strlen(test);
        mc.write(f, test, len + 1);
        uint16_t derp = 1234;
        mc.write(f, &derp, 2);
        mc.seek(f, -4, kabufuda::SeekOrigin::Current);
        mc.read(f, &derp, 2);
        std::cout << derp << std::endl;
        mc.deleteFile(f);
    }
    return 0;
}

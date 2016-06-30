#include "kabufuda/Card.hpp"
#include <iostream>

int main()
{
    kabufuda::Card mc{_S("test.USA.raw"), "GM8E", "01"};
    if (!mc)
        mc.format(kabufuda::EDeviceId::SlotA, kabufuda::ECardSize::Card2043Mb);
    kabufuda::Card mc2{_S("test2.USA.raw"), "GM8E", "01"};
    if (!mc2)
        mc2.format(kabufuda::EDeviceId::SlotA, kabufuda::ECardSize::Card2043Mb);

    std::unique_ptr<kabufuda::IFileHandle> f = mc.openFile("MetroidPrime B");
    if (!f)
    {
        f = mc.createFile("MetroidPrime B", kabufuda::BlockSize);
        mc.setPublic(f, true);
        mc.setCanCopy(f, true);
        mc.setCanMove(f, true);
        mc.setIconAddress(f, mc.commentAddress(f) + 64);
    }

    if (f)
    {
        mc.setBannerFormat(f, kabufuda::EImageFormat::C8);
        mc.setIconFormat(f, 0, kabufuda::EImageFormat::C8);
        mc.setIconSpeed(f, 0, kabufuda::EAnimationSpeed::Middle);

        const char* test = "Metroid Prime B is Cool";
        size_t len = strlen(test);
        mc.seek(f, 0, kabufuda::SeekOrigin::Begin);
        mc.write(f, test, len + 1);
        uint16_t derp = 1234;
        mc.seek(f, 1, kabufuda::SeekOrigin::End);
        mc.write(f, &derp, 2);
        mc.seek(f, -2, kabufuda::SeekOrigin::Current);
        mc.read(f, &derp, 2);
        std::cout << derp << std::endl;
        if (mc.copyFileTo(f, mc2))
            printf("Copy succeeded!\n");
        else
            printf("Copy failed...\n");
    }
    return 0;
}

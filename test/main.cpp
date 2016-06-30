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
    }

    if (f)
    {
        mc.setBannerFormat(f, kabufuda::EImageFormat::C8);
        mc.setIconFormat(f, 0, kabufuda::EImageFormat::C8);
        mc.setIconSpeed(f, 0, kabufuda::EAnimationSpeed::Middle);

        mc.seek(f, 4, kabufuda::SeekOrigin::Begin);
        mc.setCommentAddress(f, 4);

        std::string comment("Metroid Prime PC Edition");
        mc.write(f, comment.c_str(), comment.length());
        mc.seek(f, 32 - comment.length(), kabufuda::SeekOrigin::Current);
        comment = "Metroid Prime PC Is Cool";
        mc.write(f, comment.c_str(), comment.length());
        mc.seek(f, 32 - comment.length(), kabufuda::SeekOrigin::Current);
        mc.setImageAddress(f, mc.tell(f));

        if (mc.copyFileTo(f, mc2))
            printf("Copy succeeded!\n");
        else
            printf("Copy failed...\n");
    }
    return 0;
}

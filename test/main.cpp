#include "kabufuda/Card.hpp"
#include <iostream>

int main() {
  kabufuda::Card mc{"GM8E", "01"};
  mc.open(_SYS_STR("test.USA.raw"));
  mc.format(kabufuda::ECardSlot::SlotA, kabufuda::ECardSize::Card2043Mb);
  uint64_t a = 0;
  mc.getSerial(a);

  kabufuda::FileHandle f;
  mc.openFile("MetroidPrime A", f);
  for (uint32_t i = 0; i < 127; i++) {
    char name[32] = {'\0'};
    sprintf(name, "Metroid Prime %i", i);
    kabufuda::ECardResult res = mc.createFile(name, kabufuda::BlockSize, f);
    if (res == kabufuda::ECardResult::INSSPACE || res == kabufuda::ECardResult::NOFILE)
      break;

    mc.setPublic(f, true);
    mc.setCanCopy(f, true);
    mc.setCanMove(f, true);
    kabufuda::CardStat stat = {};
    mc.setStatus(f, stat);
    mc.asyncWrite(f, "Test\0", strlen("Test") + 1);
    mc.seek(f, 32, kabufuda::SeekOrigin::Begin);
    mc.asyncWrite(f, "Test\0", strlen("Test") + 1);
  }
  return 0;
}

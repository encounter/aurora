#include <cassert>

#include "internal.hpp"

#include <dolphin/os.h>

#define ROUND64(n, a) (((u64)(n) + (a)-1) & ~(u64)((a)-1))
#define TRUNC64(n, a) (((u64)(n)) & ~(u64)((a)-1))

static void* ArenaLow;
static void* ArenaHigh;

void* OSGetArenaHi() {
  return ArenaHigh;
}

void* OSGetArenaLo() {
  return ArenaLow;
}

void OSSetArenaHi(void* newHi) {
  assert(newHi <= MEM1End && newHi >= MEM1Start);
  ArenaHigh = newHi;
}

void OSSetArenaLo(void* newLo) {
  assert(newLo <= MEM1End && newLo >= MEM1Start);
  ArenaLow = newLo;
}

void* OSAllocFromArenaLo(const u32 size, const u32 align) {
  void* ptr = OSGetArenaLo();
  auto arenaLo = static_cast<u8*>(ptr = reinterpret_cast<void*>(ROUND64(ptr, align)));
  arenaLo += size;
  arenaLo = reinterpret_cast<u8*>(ROUND64(arenaLo, align));
  OSSetArenaLo(arenaLo);
  return ptr;
}

void* OSAllocFromArenaHi(const u32 size, const u32 align) {
  void* ptr;

  auto arenaHi = static_cast<u8*>(OSGetArenaHi());
  arenaHi = reinterpret_cast<u8*>(TRUNC64(arenaHi, align));
  arenaHi -= size;
  arenaHi = static_cast<u8*>(ptr = reinterpret_cast<void*>(TRUNC64(arenaHi, align)));
  OSSetArenaHi(arenaHi);
  return ptr;
}

void AuroraInitArena() {
  if (MEM1Start == nullptr) {
    return;
  }

  OSSetArenaLo(static_cast<u8*>(MEM1Start) + ARENA_START_OFFSET);
  OSSetArenaHi(MEM1End);
}

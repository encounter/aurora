#include <dolphin/os.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

constexpr std::size_t kArenaBytes = 64 * 1024;

std::array<std::uint8_t, kArenaBytes> gArena{};

void resetAllocator(int maxHeaps = 8) {
  std::fill(gArena.begin(), gArena.end(), 0);
  void* start = gArena.data();
  void* end = gArena.data() + gArena.size();
  ASSERT_NE(OSInitAlloc(start, end, maxHeaps), nullptr);
  EXPECT_EQ(OSSetCurrentHeap(-1), -1);
}

} // namespace

TEST(OSAlloc, InitCreateAllocFreeCheck) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 512, gArena.data() + 4096);
  ASSERT_GE(heap, 0);

  EXPECT_EQ(OSSetCurrentHeap(heap), -1);

  void* p = OSAllocFromHeap(heap, 96);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) & 31u, 0u);
  EXPECT_GE(OSReferentSize(p), 96u);

  s32 freeBefore = OSCheckHeap(heap);
  EXPECT_GE(freeBefore, 0);

  OSFreeToHeap(heap, p);
  s32 freeAfter = OSCheckHeap(heap);
  EXPECT_GE(freeAfter, freeBefore);
}

TEST(OSAlloc, AddToHeapMakesSpaceAvailable) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 0x1000, gArena.data() + 0x2000);
  ASSERT_GE(heap, 0);

  void* p1 = OSAllocFromHeap(heap, 0xD00);
  ASSERT_NE(p1, nullptr);

  void* p2 = OSAllocFromHeap(heap, 0x400);
  EXPECT_EQ(p2, nullptr);

  OSAddToHeap(heap, gArena.data() + 0x3000, gArena.data() + 0x3800);

  p2 = OSAllocFromHeap(heap, 0x400);
  EXPECT_NE(p2, nullptr);
}

TEST(OSAlloc, AllocFixedCarvesRangeFromHeap) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 0x2000, gArena.data() + 0x5000);
  ASSERT_GE(heap, 0);

  s32 freeBefore = OSCheckHeap(heap);
  ASSERT_GT(freeBefore, 0);

  void* fixed = OSAllocFixed(gArena.data() + 0x3000, gArena.data() + 0x3400);
  ASSERT_NE(fixed, nullptr);

  s32 freeAfter = OSCheckHeap(heap);
  EXPECT_GE(freeBefore, freeAfter);
}

TEST(OSAlloc, SetCurrentHeapReturnsPrevious) {
  resetAllocator();

  OSHeapHandle heapA = OSCreateHeap(gArena.data() + 0x1000, gArena.data() + 0x2000);
  OSHeapHandle heapB = OSCreateHeap(gArena.data() + 0x3000, gArena.data() + 0x4000);
  ASSERT_GE(heapA, 0);
  ASSERT_GE(heapB, 0);

  EXPECT_EQ(OSSetCurrentHeap(heapA), -1);
  EXPECT_EQ(OSSetCurrentHeap(heapB), heapA);

  void* p = OSAlloc(128);
  ASSERT_NE(p, nullptr);
  OSFree(p);

  EXPECT_GE(OSCheckHeap(heapB), 0);
}

TEST(OSAlloc, DestroyHeapInvalidatesHandle) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 0x1000, gArena.data() + 0x2000);
  ASSERT_GE(heap, 0);

  OSDestroyHeap(heap);
  EXPECT_LT(OSCheckHeap(heap), 0);
  EXPECT_EQ(OSAllocFromHeap(heap, 64), nullptr);
}

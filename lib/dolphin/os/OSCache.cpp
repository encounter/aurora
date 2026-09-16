#include <dolphin/os/OSCache.h>

#include <cstddef>
#include <cstring>

namespace {
alignas(32) u8 s_lcData[16 * 1024];

u32 copy_data(void* dest, const void* src, u32 nBytes) {
  const size_t numBlocks = (static_cast<size_t>(nBytes) + 31) / 32;
  if (numBlocks != 0) {
    std::memcpy(dest, src, numBlocks * 32);
  }
  return static_cast<u32>((numBlocks + 127) / 128);
}
} // namespace

extern "C" {
void DCInvalidateRange(void*, u32) {}
void DCFlushRange(void*, u32) {}
void DCStoreRange(void*, u32) {}
void DCFlushRangeNoSync(void*, u32) {}
void DCStoreRangeNoSync(void*, u32) {}
void DCZeroRange(void* addr, u32 nBytes) {
  if (nBytes != 0) {
    std::memset(addr, 0, nBytes);
  }
}
void DCTouchRange(void*, u32) {}
void ICInvalidateRange(void*, u32) {}

void* LCGetBase() { return s_lcData; }
void LCEnable() {}
void LCDisable() {}

void LCLoadBlocks(void* destTag, void* srcAddr, u32 numBlocks) {
  std::memcpy(destTag, srcAddr, (numBlocks != 0 ? static_cast<size_t>(numBlocks) : 128) * 32);
}
void LCStoreBlocks(void* destAddr, void* srcTag, u32 numBlocks) {
  std::memcpy(destAddr, srcTag, (numBlocks != 0 ? static_cast<size_t>(numBlocks) : 128) * 32);
}
u32 LCLoadData(void* destAddr, void* srcAddr, u32 nBytes) { return copy_data(destAddr, srcAddr, nBytes); }
u32 LCStoreData(void* destAddr, void* srcAddr, u32 nBytes) { return copy_data(destAddr, srcAddr, nBytes); }

u32 LCQueueLength(void) { return 0; }
void LCQueueWait(u32) {}
void LCFlushQueue(void) {}
void __OSCacheInit(void) {}
}

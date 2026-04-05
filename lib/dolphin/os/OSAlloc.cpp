#include <dolphin/os.h>

#include <cstddef>
#include <cstdint>

#include "../../logging.hpp"

extern "C" volatile OSHeapHandle __OSCurrHeap = -1;

namespace {

constexpr u32 kAlignment = 32;
constexpr u32 kHeaderSize = 32;
constexpr u32 kMinObjectSize = 64;

struct HeapDesc;

struct alignas(32) Cell {
  Cell* prev;
  Cell* next;
  s32 size;
  HeapDesc* owner;
};

static_assert(sizeof(Cell) == kHeaderSize, "Cell header must stay 32 bytes");

struct HeapDesc {
  s32 size;
  Cell* freeList;
  Cell* allocated;
};

static aurora::Module AllocLog("aurora::os::alloc");

static HeapDesc* sHeapArray = nullptr;
static int sNumHeaps = 0;
static u8* sArenaStart = nullptr;
static u8* sArenaEnd = nullptr;

static uintptr_t roundUp32(const uintptr_t value) {
  return (value + (kAlignment - 1)) & ~(static_cast<uintptr_t>(kAlignment - 1));
}

static uintptr_t roundDown32(const uintptr_t value) {
  return value & ~(static_cast<uintptr_t>(kAlignment - 1));
}

static bool inArena(const void* ptr) {
  if (sArenaStart == nullptr || sArenaEnd == nullptr) {
    return false;
  }
  const auto p = reinterpret_cast<uintptr_t>(ptr);
  return p >= reinterpret_cast<uintptr_t>(sArenaStart) && p < reinterpret_cast<uintptr_t>(sArenaEnd);
}

static bool validHeapHandle(const OSHeapHandle heap) {
  return sHeapArray != nullptr && heap >= 0 && heap < sNumHeaps && sHeapArray[heap].size >= 0;
}

static Cell* addFront(Cell* list, Cell* cell) {
  cell->prev = nullptr;
  cell->next = list;
  if (list != nullptr) {
    list->prev = cell;
  }
  return cell;
}

static Cell* extract(Cell* list, Cell* cell) {
  if (cell->next != nullptr) {
    cell->next->prev = cell->prev;
  }
  if (cell->prev != nullptr) {
    cell->prev->next = cell->next;
    return list;
  }
  return cell->next;
}

static bool containsCell(Cell* list, Cell* cell) {
  for (Cell* it = list; it != nullptr; it = it->next) {
    if (it == cell) {
      return true;
    }
  }
  return false;
}

static Cell* insertAndCoalesce(Cell* list, Cell* cell) {
  Cell* prev = nullptr;
  Cell* next = list;
  while (next != nullptr && next < cell) {
    prev = next;
    next = next->next;
  }

  cell->prev = prev;
  cell->next = next;
  if (prev != nullptr) {
    prev->next = cell;
  } else {
    list = cell;
  }
  if (next != nullptr) {
    next->prev = cell;
  }

  if (cell->next != nullptr) {
    auto* right = cell->next;
    if (reinterpret_cast<u8*>(cell) + cell->size == reinterpret_cast<u8*>(right)) {
      cell->size += right->size;
      cell->next = right->next;
      if (right->next != nullptr) {
        right->next->prev = cell;
      }
    }
  }

  if (cell->prev != nullptr) {
    auto* left = cell->prev;
    if (reinterpret_cast<u8*>(left) + left->size == reinterpret_cast<u8*>(cell)) {
      left->size += cell->size;
      left->next = cell->next;
      if (cell->next != nullptr) {
        cell->next->prev = left;
      }
      return list;
    }
  }

  return list;
}

static bool validateBlockRange(const uintptr_t start, const uintptr_t end) {
  if (start >= end) {
    return false;
  }
  if (sArenaStart == nullptr || sArenaEnd == nullptr) {
    return false;
  }
  return start >= reinterpret_cast<uintptr_t>(sArenaStart)
      && end <= reinterpret_cast<uintptr_t>(sArenaEnd)
      && (end - start) >= kMinObjectSize;
}

static void dropTinyCell(HeapDesc& hd, Cell* cell) {
  hd.freeList = extract(hd.freeList, cell);
  hd.size -= cell->size;
}

static void carveRangeFromHeap(HeapDesc& hd, uintptr_t carveStart, uintptr_t carveEnd) {
  Cell* cell = hd.freeList;
  while (cell != nullptr) {
    Cell* nextCell = cell->next;
    const auto cellStart = reinterpret_cast<uintptr_t>(cell);
    const auto cellEnd = cellStart + static_cast<uintptr_t>(cell->size);

    const auto overlapStart = carveStart > cellStart ? carveStart : cellStart;
    const auto overlapEnd = carveEnd < cellEnd ? carveEnd : cellEnd;
    if (overlapStart >= overlapEnd) {
      cell = nextCell;
      continue;
    }

    const auto removed = static_cast<s32>(overlapEnd - overlapStart);
    hd.size -= removed;

    const bool cutHead = overlapStart == cellStart;
    const bool cutTail = overlapEnd == cellEnd;

    if (cutHead && cutTail) {
      hd.freeList = extract(hd.freeList, cell);
    } else if (cutHead) {
      auto* newCell = reinterpret_cast<Cell*>(overlapEnd);
      newCell->size = static_cast<s32>(cellEnd - overlapEnd);
      newCell->owner = nullptr;
      newCell->prev = cell->prev;
      newCell->next = cell->next;
      if (newCell->prev != nullptr) {
        newCell->prev->next = newCell;
      } else {
        hd.freeList = newCell;
      }
      if (newCell->next != nullptr) {
        newCell->next->prev = newCell;
      }
      if (newCell->size < static_cast<s32>(kMinObjectSize)) {
        dropTinyCell(hd, newCell);
      }
    } else if (cutTail) {
      cell->size = static_cast<s32>(overlapStart - cellStart);
      if (cell->size < static_cast<s32>(kMinObjectSize)) {
        dropTinyCell(hd, cell);
      }
    } else {
      const auto leftSize = static_cast<s32>(overlapStart - cellStart);
      const auto rightSize = static_cast<s32>(cellEnd - overlapEnd);
      if (leftSize >= static_cast<s32>(kMinObjectSize) && rightSize >= static_cast<s32>(kMinObjectSize)) {
        auto* right = reinterpret_cast<Cell*>(overlapEnd);
        right->size = rightSize;
        right->owner = nullptr;
        right->prev = cell;
        right->next = cell->next;
        if (right->next != nullptr) {
          right->next->prev = right;
        }
        cell->next = right;
        cell->size = leftSize;
      } else if (leftSize >= static_cast<s32>(kMinObjectSize)) {
        cell->size = leftSize;
      } else if (rightSize >= static_cast<s32>(kMinObjectSize)) {
        auto* right = reinterpret_cast<Cell*>(overlapEnd);
        right->size = rightSize;
        right->owner = nullptr;
        right->prev = cell->prev;
        right->next = cell->next;
        if (right->prev != nullptr) {
          right->prev->next = right;
        } else {
          hd.freeList = right;
        }
        if (right->next != nullptr) {
          right->next->prev = right;
        }
      } else {
        hd.freeList = extract(hd.freeList, cell);
      }
    }

    cell = nextCell;
  }
}

} // namespace

extern "C" {

void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps) {
  if (arenaStart == nullptr || arenaEnd == nullptr || maxHeaps <= 0) {
    return nullptr;
  }

  auto start = reinterpret_cast<uintptr_t>(arenaStart);
  auto end = reinterpret_cast<uintptr_t>(arenaEnd);
  if (start >= end) {
    return nullptr;
  }

  const auto arrayBytes = static_cast<uintptr_t>(maxHeaps) * sizeof(HeapDesc);
  if ((end - start) < arrayBytes + kMinObjectSize) {
    return nullptr;
  }

  sHeapArray = reinterpret_cast<HeapDesc*>(arenaStart);
  sNumHeaps = maxHeaps;
  for (int i = 0; i < sNumHeaps; ++i) {
    sHeapArray[i].size = -1;
    sHeapArray[i].freeList = nullptr;
    sHeapArray[i].allocated = nullptr;
  }

  __OSCurrHeap = -1;
  sArenaStart = reinterpret_cast<u8*>(roundUp32(start + arrayBytes));
  sArenaEnd = reinterpret_cast<u8*>(roundDown32(end));
  if (sArenaEnd <= sArenaStart || static_cast<uintptr_t>(sArenaEnd - sArenaStart) < kMinObjectSize) {
    sHeapArray = nullptr;
    sNumHeaps = 0;
    sArenaStart = nullptr;
    sArenaEnd = nullptr;
    return nullptr;
  }

  return sArenaStart;
}

OSHeapHandle OSCreateHeap(void* start, void* end) {
  if (sHeapArray == nullptr) {
    return -1;
  }

  const auto blockStart = roundUp32(reinterpret_cast<uintptr_t>(start));
  const auto blockEnd = roundDown32(reinterpret_cast<uintptr_t>(end));
  if (!validateBlockRange(blockStart, blockEnd)) {
    return -1;
  }

  for (OSHeapHandle heap = 0; heap < sNumHeaps; ++heap) {
    auto& hd = sHeapArray[heap];
    if (hd.size >= 0) {
      continue;
    }

    hd.size = static_cast<s32>(blockEnd - blockStart);
    hd.allocated = nullptr;
    hd.freeList = reinterpret_cast<Cell*>(blockStart);
    hd.freeList->prev = nullptr;
    hd.freeList->next = nullptr;
    hd.freeList->size = hd.size;
    hd.freeList->owner = nullptr;
    return heap;
  }

  return -1;
}

void OSDestroyHeap(OSHeapHandle heap) {
  if (!validHeapHandle(heap)) {
    return;
  }

  auto& hd = sHeapArray[heap];
  hd.size = -1;
  hd.freeList = nullptr;
  hd.allocated = nullptr;
  if (__OSCurrHeap == heap) {
    __OSCurrHeap = -1;
  }
}

void OSAddToHeap(OSHeapHandle heap, void* start, void* end) {
  if (!validHeapHandle(heap)) {
    return;
  }

  const auto blockStart = roundUp32(reinterpret_cast<uintptr_t>(start));
  const auto blockEnd = roundDown32(reinterpret_cast<uintptr_t>(end));
  if (!validateBlockRange(blockStart, blockEnd)) {
    return;
  }

  auto& hd = sHeapArray[heap];
  auto* cell = reinterpret_cast<Cell*>(blockStart);
  cell->prev = nullptr;
  cell->next = nullptr;
  cell->size = static_cast<s32>(blockEnd - blockStart);
  cell->owner = nullptr;
  hd.freeList = insertAndCoalesce(hd.freeList, cell);
  hd.size += cell->size;
}

void* OSAllocFromHeap(OSHeapHandle heap, u32 size) {
  if (!validHeapHandle(heap) || size == 0) {
    return nullptr;
  }

  auto& hd = sHeapArray[heap];
  const auto requested = static_cast<s32>(roundUp32(static_cast<uintptr_t>(size) + kHeaderSize));

  Cell* cell = hd.freeList;
  while (cell != nullptr && cell->size < requested) {
    cell = cell->next;
  }
  if (cell == nullptr) {
    return nullptr;
  }

  const auto leftover = cell->size - requested;
  if (leftover < static_cast<s32>(kMinObjectSize)) {
    hd.freeList = extract(hd.freeList, cell);
  } else {
    auto* split = reinterpret_cast<Cell*>(reinterpret_cast<u8*>(cell) + requested);
    split->size = leftover;
    split->owner = nullptr;
    split->prev = cell->prev;
    split->next = cell->next;
    if (split->prev != nullptr) {
      split->prev->next = split;
    } else {
      hd.freeList = split;
    }
    if (split->next != nullptr) {
      split->next->prev = split;
    }
    cell->size = requested;
  }

  cell->owner = &hd;
  hd.allocated = addFront(hd.allocated, cell);
  return reinterpret_cast<u8*>(cell) + kHeaderSize;
}

void OSFreeToHeap(OSHeapHandle heap, void* ptr) {
  if (!validHeapHandle(heap) || ptr == nullptr) {
    return;
  }
  if (!inArena(ptr) || (reinterpret_cast<uintptr_t>(ptr) & (kAlignment - 1)) != 0) {
    return;
  }

  auto& hd = sHeapArray[heap];
  auto* cell = reinterpret_cast<Cell*>(reinterpret_cast<u8*>(ptr) - kHeaderSize);
  if (cell->owner != &hd || !containsCell(hd.allocated, cell)) {
    return;
  }

  hd.allocated = extract(hd.allocated, cell);
  cell->owner = nullptr;
  hd.freeList = insertAndCoalesce(hd.freeList, cell);
}

OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap) {
  const auto prev = __OSCurrHeap;
  if (heap == -1 || validHeapHandle(heap)) {
    __OSCurrHeap = heap;
  }
  return prev;
}

void* OSAllocFixed(void* rstart, void* rend) {
  if (sHeapArray == nullptr || rstart == nullptr || rend == nullptr) {
    return nullptr;
  }

  for (int i = 0; i < sNumHeaps; ++i) {
    if (sHeapArray[i].size >= 0 && sHeapArray[i].allocated != nullptr) {
      return nullptr;
    }
  }

  const auto fixedStart = roundDown32(reinterpret_cast<uintptr_t>(rstart));
  const auto fixedEnd = roundUp32(reinterpret_cast<uintptr_t>(rend));
  if (fixedStart >= fixedEnd) {
    return nullptr;
  }
  if (fixedStart < reinterpret_cast<uintptr_t>(sArenaStart)
      || fixedEnd > reinterpret_cast<uintptr_t>(sArenaEnd)) {
    return nullptr;
  }

  for (int i = 0; i < sNumHeaps; ++i) {
    auto& hd = sHeapArray[i];
    if (hd.size >= 0) {
      carveRangeFromHeap(hd, fixedStart, fixedEnd);
    }
  }

  return reinterpret_cast<void*>(fixedStart);
}

s32 OSCheckHeap(OSHeapHandle heap) {
  if (!validHeapHandle(heap)) {
    return -1;
  }

  auto& hd = sHeapArray[heap];
  s32 total = 0;
  s32 freeBytes = 0;

  if (hd.allocated != nullptr && hd.allocated->prev != nullptr) {
    return -1;
  }

  for (Cell* cell = hd.allocated; cell != nullptr; cell = cell->next) {
    if (!inArena(cell)
        || (reinterpret_cast<uintptr_t>(cell) & (kAlignment - 1)) != 0
        || cell->size < static_cast<s32>(kMinObjectSize)
        || (cell->size & (kAlignment - 1)) != 0
        || cell->owner != &hd
        || (cell->next != nullptr && cell->next->prev != cell)) {
      return -1;
    }
    total += cell->size;
    if (total <= 0 || total > hd.size) {
      return -1;
    }
  }

  if (hd.freeList != nullptr && hd.freeList->prev != nullptr) {
    return -1;
  }

  for (Cell* cell = hd.freeList; cell != nullptr; cell = cell->next) {
    if (!inArena(cell)
        || (reinterpret_cast<uintptr_t>(cell) & (kAlignment - 1)) != 0
        || cell->size < static_cast<s32>(kMinObjectSize)
        || (cell->size & (kAlignment - 1)) != 0
        || cell->owner != nullptr
        || (cell->next != nullptr && cell->next->prev != cell)) {
      return -1;
    }
    if (cell->next != nullptr) {
      if (reinterpret_cast<uintptr_t>(cell) + static_cast<uintptr_t>(cell->size)
          > reinterpret_cast<uintptr_t>(cell->next)) {
        return -1;
      }
    }

    total += cell->size;
    freeBytes += cell->size - static_cast<s32>(kHeaderSize);
    if (total <= 0 || total > hd.size) {
      return -1;
    }
  }

  if (total != hd.size) {
    return -1;
  }
  return freeBytes;
}

u32 OSReferentSize(void* ptr) {
  if (ptr == nullptr || !inArena(ptr) || (reinterpret_cast<uintptr_t>(ptr) & (kAlignment - 1)) != 0) {
    return 0;
  }
  auto* cell = reinterpret_cast<Cell*>(reinterpret_cast<u8*>(ptr) - kHeaderSize);
  if (cell->owner == nullptr) {
    return 0;
  }
  return static_cast<u32>(cell->size - static_cast<s32>(kHeaderSize));
}

void OSDumpHeap(OSHeapHandle heap) {
  AllocLog.info("OSDumpHeap({})", heap);
  if (!validHeapHandle(heap)) {
    AllocLog.info("--------Invalid");
    return;
  }

  auto& hd = sHeapArray[heap];
  if (OSCheckHeap(heap) < 0) {
    AllocLog.info("--------Broken");
    return;
  }

  AllocLog.info("addr\tsize\t\tend\t\tprev\t\tnext");
  AllocLog.info("--------Allocated");
  for (Cell* cell = hd.allocated; cell != nullptr; cell = cell->next) {
    AllocLog.info("{}\t{}\t{}\t{}\t{}",
                  reinterpret_cast<void*>(cell),
                  cell->size,
                  reinterpret_cast<void*>(reinterpret_cast<u8*>(cell) + cell->size),
                  reinterpret_cast<void*>(cell->prev),
                  reinterpret_cast<void*>(cell->next));
  }

  AllocLog.info("--------Free");
  for (Cell* cell = hd.freeList; cell != nullptr; cell = cell->next) {
    AllocLog.info("{}\t{}\t{}\t{}\t{}",
                  reinterpret_cast<void*>(cell),
                  cell->size,
                  reinterpret_cast<void*>(reinterpret_cast<u8*>(cell) + cell->size),
                  reinterpret_cast<void*>(cell->prev),
                  reinterpret_cast<void*>(cell->next));
  }
}

void OSVisitAllocated(void (*visitor)(void*, u32)) {
  if (visitor == nullptr || sHeapArray == nullptr) {
    return;
  }

  for (int heap = 0; heap < sNumHeaps; ++heap) {
    auto& hd = sHeapArray[heap];
    if (hd.size < 0) {
      continue;
    }
    for (Cell* cell = hd.allocated; cell != nullptr; cell = cell->next) {
      visitor(reinterpret_cast<u8*>(cell) + kHeaderSize,
              static_cast<u32>(cell->size - static_cast<s32>(kHeaderSize)));
    }
  }
}

} // extern "C"

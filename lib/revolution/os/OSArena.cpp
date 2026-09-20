#include "internal.hpp"
#include <revolution/os.h>

struct Arena {
	uintptr_t low;
	uintptr_t high;
};

static Arena MEM1Arena;
static Arena MEM2Arena;

static void SetBound(uintptr_t& bound, void* value, void* start, void* end) {
	const auto address = reinterpret_cast<uintptr_t>(value);

	if (address < reinterpret_cast<uintptr_t>(start) || address > reinterpret_cast<uintptr_t>(end)) {
		Log.fatal("Arena boundary is outside its memory region");
	}

	bound = address;
}

static bool ValidAlignment(u32 alignment) {
	return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

static void* AllocateLow(Arena& arena, u32 size, u32 alignment) {
	if (!ValidAlignment(alignment) || arena.low == 0 || arena.low > arena.high) {
		return nullptr;
	}

	const uintptr_t mask = alignment - 1;
	const uintptr_t padding = (alignment - (arena.low & mask)) & mask;

	if (padding > arena.high - arena.low || size > arena.high - arena.low - padding) {
		return nullptr;
	}

	const uintptr_t result = arena.low + padding;
	const uintptr_t end = result + size;
	const uintptr_t endPadding = (alignment - (end & mask)) & mask;

	if (endPadding > arena.high - end) {
		return nullptr;
	}

	arena.low = end + endPadding;
	return reinterpret_cast<void*>(result);
}

static void* AllocateHigh(Arena& arena, u32 size, u32 alignment) {
	if (!ValidAlignment(alignment) || arena.low == 0 || arena.low > arena.high) {
		return nullptr;
	}

	const uintptr_t mask = alignment - 1;
	const uintptr_t end = arena.high & ~mask;

	if (end < arena.low || size > end - arena.low) {
		return nullptr;
	}

	const uintptr_t result = (end - size) & ~mask;

	if (result < arena.low) {
		return nullptr;
	}

	arena.high = result;
	return reinterpret_cast<void*>(result);
}

void* OSGetMEM1ArenaLo() {
	return reinterpret_cast<void*>(MEM1Arena.low);
}

void* OSGetMEM1ArenaHi() {
	return reinterpret_cast<void*>(MEM1Arena.high);
}

void OSSetMEM1ArenaLo(void* value) {
	SetBound(MEM1Arena.low, value, MEM1Start, MEM1End);
}

void OSSetMEM1ArenaHi(void* value) {
	SetBound(MEM1Arena.high, value, MEM1Start, MEM1End);
}

void* OSAllocFromMEM1ArenaLo(u32 size, u32 alignment) {
	return AllocateLow(MEM1Arena, size, alignment);
}

void* OSAllocFromMEM1ArenaHi(u32 size, u32 alignment) {
	return AllocateHigh(MEM1Arena, size, alignment);
}

void* OSGetMEM2ArenaLo() {
	return reinterpret_cast<void*>(MEM2Arena.low);
}

void* OSGetMEM2ArenaHi() {
	return reinterpret_cast<void*>(MEM2Arena.high);
}

void OSSetMEM2ArenaLo(void* value) {
	SetBound(MEM2Arena.low, value, MEM2Start, MEM2End);
}

void OSSetMEM2ArenaHi(void* value) {
	SetBound(MEM2Arena.high, value, MEM2Start, MEM2End);
}

void* OSAllocFromMEM2ArenaLo(u32 size, u32 alignment) {
	return AllocateLow(MEM2Arena, size, alignment);
}

void* OSAllocFromMEM2ArenaHi(u32 size, u32 alignment) {
	return AllocateHigh(MEM2Arena, size, alignment);
}

void* OSGetArenaLo() {
	return OSGetMEM1ArenaLo();
}

void* OSGetArenaHi() {
	return OSGetMEM1ArenaHi();
}

void OSSetArenaLo(void* value) {
	OSSetMEM1ArenaLo(value);
}

void OSSetArenaHi(void* value) {
	OSSetMEM1ArenaHi(value);
}

void* OSAllocFromArenaLo(u32 size, u32 alignment) {
	return OSAllocFromMEM1ArenaLo(size, alignment);
}

void* OSAllocFromArenaHi(u32 size, u32 alignment) {
	return OSAllocFromMEM1ArenaHi(size, alignment);
}

void AuroraInitArena() {
	if (MEM1Start != nullptr) {
		if (aurora::g_config.mem1Size < ARENA_START_OFFSET) {
			Log.fatal("MEM1 is too small for the OS arena");
		}

		OSSetMEM1ArenaLo(static_cast<u8*>(MEM1Start) + ARENA_START_OFFSET);
		OSSetMEM1ArenaHi(MEM1End);
	}

	if (MEM2Start != nullptr) {
		OSSetMEM2ArenaLo(MEM2Start);
		OSSetMEM2ArenaHi(MEM2End);
	}
}

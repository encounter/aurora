#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "fmt/base.h"

#include <cassert>

#include "internal.hpp"
#include <revolution/os.h>
#include "dolphin/types.h"

#if !NDEBUG && (INTPTR_MAX > INT32_MAX)
#define GUARD_MEMORY 1
#endif

uintptr_t OSBaseAddress = 0;

void* MEM1Start;
void* MEM1End;
void* MEM2Start;
void* MEM2End;

static void GuardGCMemory();
static void* AllocMemory(u32 size, uintptr_t consoleAddress);

void AuroraOSInitMemory() {
	if (aurora::g_config.mem1Size > 0x10000000 || aurora::g_config.mem2Size > 0x10000000) {
		Log.fatal("Memory regions must not exceed 256 MiB");
	}

	GuardGCMemory();

	if (aurora::g_config.mem2Size > 0) {
		MEM2Start = AllocMemory(aurora::g_config.mem2Size, 0x90000000);
		MEM2End = static_cast<u8*>(MEM2Start) + aurora::g_config.mem2Size;
	}

	if (aurora::g_config.mem1Size > 0) {
		MEM1Start = AllocMemory(aurora::g_config.mem1Size, 0x80000000);
		MEM1End = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(MEM1Start) + aurora::g_config.mem1Size);
		OSBaseAddress = reinterpret_cast<uintptr_t>(MEM1Start);
	}
}

#if GUARD_MEMORY
static uintptr_t GetAllocationGranularity() {
#if _WIN32
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);

	return sysInfo.dwAllocationGranularity;
#else
	// TODO: posix impl
	return 0;
#endif
}

static void TryGuardRegion(const uintptr_t start, const uintptr_t end, char const* const name) {
#if _WIN32
	assert(start != 0);
	const auto addr = VirtualAlloc(reinterpret_cast<LPVOID>(start), end - start, MEM_RESERVE, PAGE_NOACCESS);

	if (addr == nullptr) {
		Log.debug("Unable to guard memory region: {}", name);
	} else {
		assert(addr == reinterpret_cast<LPVOID>(start));
		Log.debug("Successfully guarded memory range: {:08X}-{:08X} ({})", start, end, name);
	}
#else
	// TODO: posix impl
#endif
}

static void GuardGCMemory() {
	// Reserve the normal GC/Wii memory map so accesses are 100% guaranteed to fail.
	// https://www.gc-forever.com/yagcd/chap5.html#sec5.11
	// https://wiibrew.org/wiki/Memory_map

	// We can't quite map at address 0 (for good reasons) but we *can* map at the next granularity over!
	TryGuardRegion(0x00000000 + GetAllocationGranularity(), 0x017fffff, "MEM1 Physical");
	TryGuardRegion(0x80000000, 0x817fffff, "MEM1 Logical (cached)");
	TryGuardRegion(0xC0000000, 0xC17fffff, "MEM1 Logical (uncached)");
	TryGuardRegion(0x10000000, 0x13FFFFFF, "MEM2 Physical");
	TryGuardRegion(0x90000000, 0x93FFFFFF, "MEM2 Logical (cached)");
	TryGuardRegion(0xD0000000, 0xD3FFFFFF, "MEM2 Logical (uncached)");
	TryGuardRegion(0x08000000, 0x08300000, "EFB Physical");
	TryGuardRegion(0xC8000000, 0xC8300000, "EFB Logical");
	TryGuardRegion(0x0D000000, 0x0D008000, "Hollywood HW registers Physical");
	TryGuardRegion(0xCD000000, 0xCD008000, "Hollywood HW registers Logical");
	TryGuardRegion(0x0C000000, 0x0C008020, "Broadway/GC HW registers Physical");
	TryGuardRegion(0xCC000000, 0xCC008020, "Broadway/GC HW registers Logical");
	TryGuardRegion(0xe0000000, 0xe0003fff, "GC L2 cache");
	TryGuardRegion(0xfff00000, 0xffffffff, "GC IPL");
}
#else
static void GuardGCMemory() {
}
#endif

#if _WIN64 && !NDEBUG
static void* AllocMemory(u32 size, uintptr_t consoleAddress) {
	// Allocate an entire 32-bit's worth of memory and allocate the memory region in that.
	// This way, if a 64-bit pointer gets truncated to 32-bit, it will still fall in our guard pages.

	void* bulkChunk = VirtualAlloc(nullptr, 8ll * 1024 * 1024 * 1024, MEM_RESERVE, PAGE_NOACCESS);

	if (bulkChunk == nullptr) {
		DWORD err = GetLastError();
		fmt::memory_buffer msg;
		fmt::format_system_error(msg, static_cast<int>(err), "Failed to allocate bulk chunk for memory region");
		Log.fatal("{}", fmt::to_string(msg));
	}

	uintptr_t memSpace = (reinterpret_cast<uintptr_t>(bulkChunk) | 0xFFFFFFFF) + 1;
	void* memoryAddress = reinterpret_cast<void*>(memSpace + consoleAddress);

	Log.debug("Reserved memory map at {:016X}-{:016X}", memSpace, memSpace + 0xFFFFFFFF);
	Log.debug("Memory at {:016X}-{:016X}", reinterpret_cast<uintptr_t>(memoryAddress),
			  reinterpret_cast<uintptr_t>(memoryAddress) + size);

	void* result = VirtualAlloc(memoryAddress, size, MEM_COMMIT, PAGE_READWRITE);
	if (result == nullptr) {
		DWORD err = GetLastError();
		fmt::memory_buffer msg;
		fmt::format_system_error(msg, static_cast<int>(err), "Failed to commit memory for memory region");
		Log.fatal("{}", fmt::to_string(msg));
	}

	assert(result == memoryAddress);
	return result;
}
#else
static void* AllocMemory(u32 size, uintptr_t consoleAddress) {
	void* allocation = calloc(1, static_cast<size_t>(size) + 31);

	if (allocation == nullptr) {
		Log.fatal("Failed to allocate memory region");
	}

	return reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(allocation) + 31) & ~uintptr_t(31));
}
#endif

u32 OSGetPhysicalMemSize() {
	const auto info = static_cast<OSBootInfo*>(OSPhysicalToCached(0));
	return info->memorySize;
}

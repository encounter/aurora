#include "internal.hpp"
#include <revolution/os.h>

static bool InRegion(const void* address, const void* start, const void* end) {
	const auto value = reinterpret_cast<uintptr_t>(address);
	return start != nullptr && value >= reinterpret_cast<uintptr_t>(start) && value < reinterpret_cast<uintptr_t>(end);
}

BOOL OSIsMEM1Region(const void* address) {
	return InRegion(address, MEM1Start, MEM1End);
}

BOOL OSIsMEM2Region(const void* address) {
	return InRegion(address, MEM2Start, MEM2End);
}

void* OSPhysicalToCached(u32 address) {
	if (MEM1Start != nullptr && address < aurora::g_config.mem1Size) {
		return static_cast<u8*>(MEM1Start) + address;
	}

	if (MEM2Start != nullptr && address >= 0x10000000 && address - 0x10000000 < aurora::g_config.mem2Size) {
		return static_cast<u8*>(MEM2Start) + (address - 0x10000000);
	}

	Log.fatal("Invalid physical memory address: {:08X}", address);
	return nullptr;
}

void* OSPhysicalToUncached(u32 address) {
	return OSPhysicalToCached(address);
}

u32 OSCachedToPhysical(void* address) {
	if (OSIsMEM1Region(address)) {
		return static_cast<u32>(static_cast<u8*>(address) - static_cast<u8*>(MEM1Start));
	}

	if (OSIsMEM2Region(address)) {
		return 0x10000000 + static_cast<u32>(static_cast<u8*>(address) - static_cast<u8*>(MEM2Start));
	}

	Log.fatal("Address is outside MEM1 and MEM2: {}", address);
	return 0;
}

u32 OSUncachedToPhysical(void* address) {
	return OSCachedToPhysical(address);
}

void* OSCachedToUncached(void* address) {
	return address;
}

void* OSUncachedToCached(void* address) {
	return address;
}

#include <dolphin/ar.h>
#include "../internal.hpp"
#include <revolution/os.h>

static aurora::Module Log("aurora::ar");

static u32 AR_StackPointer;
static BOOL AR_init_flag;
static u8* sAramBuffer;
static u32 sAramSize;

static constexpr u32 ARAM_STACK_START = 0x4000;

static u8* aramToHost(uintptr_t address, u32 length) {
	if (sAramBuffer == nullptr || address > sAramSize || length > sAramSize - address) {
		Log.fatal("ARAM transfer is outside the allocated region");
	}

	return sAramBuffer + address;
}

u32 ARAlloc(u32 length) {
	if (!AR_init_flag || (length & 31) != 0 || length > sAramSize - AR_StackPointer) {
		Log.fatal("Invalid ARAM allocation of {} bytes", length);
	}

	const u32 address = AR_StackPointer;
	AR_StackPointer += length;
	return address;
}

BOOL ARCheckInit() {
	return AR_init_flag;
}

u32 ARInit(u32* stackIndex, u32 entries) {
	if (AR_init_flag) {
		return ARAM_STACK_START;
	}

	if (aurora::g_config.mem2Size == 0) {
		Log.warn("ARInit called with mem2Size set to zero");
		return 0;
	}

	OSInit();
	const auto low = reinterpret_cast<uintptr_t>(OSGetMEM2ArenaLo());
	const auto high = reinterpret_cast<uintptr_t>(OSGetMEM2ArenaHi());

	if (low == 0 || high < low || high - low < ARAM_STACK_START) {
		Log.fatal("MEM2 arena is too small for ARAM");
	}

	sAramBuffer = reinterpret_cast<u8*>(low);
	sAramSize = static_cast<u32>(high - low);
	AR_StackPointer = ARAM_STACK_START;
	AR_init_flag = TRUE;
	return ARAM_STACK_START;
}

u32 ARGetBaseAddress() {
	return ARAM_STACK_START;
}

u32 ARGetSize() {
	return sAramSize;
}

u32 ARGetInternalSize() {
	return ARGetSize();
}

void ARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, uintptr_t source, uintptr_t dest,
					u32 length, ARQCallback callback) {
	if (type == ARAM_DIR_MRAM_TO_ARAM) {
		u8* target = aramToHost(dest, length);

		if (source != 0 && length != 0) {
			memcpy(target, reinterpret_cast<const void*>(source), length);
		}
	} else if (type == ARAM_DIR_ARAM_TO_MRAM) {
		const u8* data = aramToHost(source, length);

		if (dest != 0 && length != 0) {
			memcpy(reinterpret_cast<void*>(dest), data, length);
		}
	} else {
		Log.fatal("Invalid ARAM transfer direction: {}", type);
	}

	if (callback != nullptr) {
		callback(reinterpret_cast<uintptr_t>(request));
	}
}

void ARQInit() {
}

void* ARGetStorageAddress() {
	return sAramBuffer;
}

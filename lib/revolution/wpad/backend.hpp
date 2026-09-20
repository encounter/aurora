#pragma once

#include <array>
#include <aurora/wpad.h>
#include <dolphin/pad.h>

namespace aurora::wpad {
struct HostState {
	PADStatus pad{};
	Vec coreAcc{0, -1, 0};
	Vec nunchukAcc{0, -1, 0};
	AuroraWpadPointer pointer{};
	bool blocked = false;
};

std::array<HostState, WPAD_MAX_CONTROLLERS> read_host();
void install_hooks(void (*update)(), void (*shutdown)());
double now();
} // namespace aurora::wpad

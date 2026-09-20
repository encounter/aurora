#pragma once

#include "backend.hpp"

namespace aurora::wpad {
struct Sample {
	WPADStatus core{};
	WPADFSStatus fs{};
	WPADCLStatus cl{};
	AuroraWpadPointer pointer{};
	double time = 0;
	u32 format = WPAD_FMT_CORE;
};

using Observer = void (*)(s32, const Sample&);
void set_observer(Observer observer, void (*reset)());
} // namespace aurora::wpad

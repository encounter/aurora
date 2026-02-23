#pragma once

#include "../../internal.hpp"
#include <dolphin/types.h>

static aurora::Module Log("aurora::os");

constexpr s32 OS_REAL_TIMER_CLOCK = 50'000'000;

extern void* MEM1Start;
extern void* MEM1End;

void AuroraOSInitMemory();
void AuroraFillBootInfo();
void AuroraInitClock();

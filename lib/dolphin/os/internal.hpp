#pragma once

#include "../../internal.hpp"
#include <dolphin/types.h>

static aurora::Module Log("aurora::os");

constexpr s32 OS_REAL_TIMER_CLOCK = 50'000'000;

static void* MEM1Start = nullptr;
static void* MEM1End = nullptr;

void AuroraOSInitMemory();
void AuroraFillBootInfo();
void AuroraInitClock();

#pragma once

#include "../../internal.hpp"
#include <dolphin/types.h>

static aurora::Module Log("aurora::os");

constexpr uintptr_t ARENA_START_OFFSET = 0x4000;

extern void* MEM1Start;
extern void* MEM1End;
extern void* MEM2Start;
extern void* MEM2End;

void AuroraOSInitMemory();
void AuroraFillBootInfo();
void AuroraInitClock();
void AuroraInitArena();

#pragma once

#include "../../internal.hpp"

static aurora::Module Log("aurora::os");

static void* MEM1Start = nullptr;
static void* MEM1End = nullptr;

void AuroraOSInitMemory();

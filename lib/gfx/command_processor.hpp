#pragma once

#include <dolphin/gx.h>
#include <cstdint>

namespace aurora::gfx::command_processor {

// Process a buffer of GX FIFO commands
// data: pointer to command data
// size: size of command data in bytes
// bigEndian: true for big-endian data (normal), false for little-endian (GXCallDisplayListLE)
void process(const u8* data, u32 size, bool bigEndian);

} // namespace aurora::gfx::command_processor

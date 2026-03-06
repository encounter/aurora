#pragma once

#include <dolphin/gx.h>
#include <cstdint>

namespace aurora::gx::fifo {

// Process a buffer of GX FIFO commands
void process(const u8* data, u32 size, bool bigEndian);

} // namespace aurora::gx::fifo

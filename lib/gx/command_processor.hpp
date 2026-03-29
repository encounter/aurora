#pragma once

#include "../internal.hpp"

namespace aurora::gx::fifo {

// Process a buffer of GX FIFO commands
void process(const uint8_t* data, uint32_t size, bool bigEndian);

} // namespace aurora::gx::fifo

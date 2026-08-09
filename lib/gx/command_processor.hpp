#pragma once

#include "../internal.hpp"

#include <cstdint>

namespace aurora::gx::fifo {

// Process a buffer of GX FIFO commands
void process(const uint8_t* data, uint32_t size) noexcept;
void end_frame() noexcept;

} // namespace aurora::gx::fifo

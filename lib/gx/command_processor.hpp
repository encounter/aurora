#pragma once

#include "../internal.hpp"

#include <cstdint>

namespace aurora::gx::fifo {

// Process a buffer of GX FIFO commands
void process(const uint8_t* data, uint32_t size) noexcept;
void clear_draw_cache() noexcept;

} // namespace aurora::gx::fifo

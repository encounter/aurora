#pragma once

#include "gx.hpp"

#include <bit>
#include <cstring>
#include <span>

namespace aurora::gx::fifo {

constexpr u32 reg_get(const u32 reg, const u32 size, const u32 shift) noexcept {
  return reg >> shift & (1u << size) - 1;
}

void handle_bp(u32 value) noexcept;
void handle_cp(u8 addr, u32 value) noexcept;
void handle_xf(u16 addr, std::span<const u8> data) noexcept;

bool copy_xf_data(u32 addr, const u8* data, u32 len, std::endian e) noexcept;

} // namespace aurora::gx::fifo

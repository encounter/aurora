#pragma once

#include "gx.hpp"

namespace aurora::gfx::gx {
struct DisplayListResult {
  Range vertRange;
  Range idxRange;
  u32 numIndices;
  GXVtxFmt fmt;
};

auto process_display_list(const u8* dlStart, u32 dlSize, bool bigEndian) -> DisplayListResult;
}; // namespace aurora::gfx::gx

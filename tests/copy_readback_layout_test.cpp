#include "gfx/copy_readback_layout.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace aurora::gfx::copy_readback;

namespace {
// The GX 8-bit tile layout written out longhand, as game code indexes it
// (e.g. Super Mario Sunshine's TPollutionPos::index).
uint32_t reference_offset(uint32_t x, uint32_t y, uint32_t log2Width) {
  return (y & 3u) * 8u + ((x >> 3u) + ((y >> 2u) << (log2Width - 3u))) * 32u + (x & 7u);
}
} // namespace

TEST(CopyReadbackLayout, MatchesGameIndexingForPowerOfTwoSizes) {
  for (uint32_t log2Width = 3; log2Width <= 9; ++log2Width) {
    const uint32_t width = 1u << log2Width;
    for (uint32_t y = 0; y < 16; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        EXPECT_EQ(texel_to_offset_8bit(x, y, width), reference_offset(x, y, log2Width))
            << "x=" << x << " y=" << y << " width=" << width;
      }
    }
  }
}

TEST(CopyReadbackLayout, OffsetToTexelInvertsTexelToOffset) {
  for (const uint32_t width : {8u, 20u, 64u, 512u}) {
    const uint32_t height = 12;
    const uint32_t size = byte_size_8bit(width, height);
    std::vector<bool> seen(size, false);
    for (uint32_t offset = 0; offset < size; ++offset) {
      const auto texel = offset_to_texel_8bit(offset, width);
      EXPECT_LT(texel.x, padded_width_8bit(width));
      EXPECT_LT(texel.y, padded_height_8bit(height));
      const uint32_t back = texel_to_offset_8bit(texel.x, texel.y, width);
      EXPECT_EQ(back, offset);
      ASSERT_LT(back, size);
      EXPECT_FALSE(seen[back]);
      seen[back] = true;
    }
  }
}

TEST(CopyReadbackLayout, PadsToWholeTiles) {
  EXPECT_EQ(byte_size_8bit(512, 512), 512u * 512u);
  EXPECT_EQ(byte_size_8bit(20, 6), 24u * 8u);
  EXPECT_EQ(padded_width_8bit(1), 8u);
  EXPECT_EQ(padded_height_8bit(5), 8u);
}

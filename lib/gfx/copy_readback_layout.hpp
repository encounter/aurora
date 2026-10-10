#pragma once

#include <cstdint>

namespace aurora::gfx::copy_readback {

// GX stores 8-bit texture formats (I8, A8, R8, G8, B8) as 8x4 texel tiles of 32 bytes,
// in row-major tile order. A copy writes the full padded tile grid.
inline constexpr uint32_t Tile8Width = 8;
inline constexpr uint32_t Tile8Height = 4;
inline constexpr uint32_t Tile8Bytes = Tile8Width * Tile8Height;

constexpr uint32_t padded_width_8bit(uint32_t width) noexcept {
  return (width + Tile8Width - 1) / Tile8Width * Tile8Width;
}

constexpr uint32_t padded_height_8bit(uint32_t height) noexcept {
  return (height + Tile8Height - 1) / Tile8Height * Tile8Height;
}

constexpr uint32_t byte_size_8bit(uint32_t width, uint32_t height) noexcept {
  return padded_width_8bit(width) * padded_height_8bit(height);
}

// Byte offset of texel (x, y) in GX tiled 8-bit layout.
constexpr uint32_t texel_to_offset_8bit(uint32_t x, uint32_t y, uint32_t width) noexcept {
  const uint32_t tilesPerRow = padded_width_8bit(width) / Tile8Width;
  const uint32_t tile = (y / Tile8Height) * tilesPerRow + x / Tile8Width;
  return tile * Tile8Bytes + (y % Tile8Height) * Tile8Width + x % Tile8Width;
}

struct Texel {
  uint32_t x;
  uint32_t y;
};

// Inverse of texel_to_offset_8bit. The readback shader uses the same mapping.
constexpr Texel offset_to_texel_8bit(uint32_t offset, uint32_t width) noexcept {
  const uint32_t tilesPerRow = padded_width_8bit(width) / Tile8Width;
  const uint32_t tile = offset / Tile8Bytes;
  const uint32_t within = offset % Tile8Bytes;
  return Texel{
      .x = (tile % tilesPerRow) * Tile8Width + within % Tile8Width,
      .y = (tile / tilesPerRow) * Tile8Height + within / Tile8Width,
  };
}

} // namespace aurora::gfx::copy_readback

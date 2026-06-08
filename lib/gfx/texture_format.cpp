#include "texture.hpp"

#include "../internal.hpp"

#include <algorithm>
#include <cstdint>

#include <magic_enum.hpp>

namespace aurora::gfx {
namespace {
Module Log("aurora::gfx");

constexpr u32 div_ceil(u32 value, u32 divisor) noexcept { return (value + divisor - 1) / divisor; }
} // namespace

TextureFormatInfo format_info(wgpu::TextureFormat format) noexcept {
  switch (format) {
    DEFAULT_FATAL("unimplemented texture format {}", magic_enum::enum_name(format));
  case wgpu::TextureFormat::R8Unorm:
    return {1, 1, 1, false};
  case wgpu::TextureFormat::RG8Unorm:
  case wgpu::TextureFormat::R16Sint:
    return {1, 1, 2, false};
  case wgpu::TextureFormat::RGBA8Unorm:
  case wgpu::TextureFormat::BGRA8Unorm:
  case wgpu::TextureFormat::R32Float:
    return {1, 1, 4, false};
  case wgpu::TextureFormat::BC1RGBAUnorm:
    return {4, 4, 8, true};
  case wgpu::TextureFormat::BC3RGBAUnorm:
  case wgpu::TextureFormat::BC5RGUnorm:
  case wgpu::TextureFormat::BC7RGBAUnorm:
    return {4, 4, 16, true};
  case wgpu::TextureFormat::ASTC4x4Unorm:
  case wgpu::TextureFormat::ASTC4x4UnormSrgb:
    return {4, 4, 16, true};
  case wgpu::TextureFormat::ASTC5x4Unorm:
  case wgpu::TextureFormat::ASTC5x4UnormSrgb:
    return {5, 4, 16, true};
  case wgpu::TextureFormat::ASTC5x5Unorm:
  case wgpu::TextureFormat::ASTC5x5UnormSrgb:
    return {5, 5, 16, true};
  case wgpu::TextureFormat::ASTC6x5Unorm:
  case wgpu::TextureFormat::ASTC6x5UnormSrgb:
    return {6, 5, 16, true};
  case wgpu::TextureFormat::ASTC6x6Unorm:
  case wgpu::TextureFormat::ASTC6x6UnormSrgb:
    return {6, 6, 16, true};
  case wgpu::TextureFormat::ASTC8x5Unorm:
  case wgpu::TextureFormat::ASTC8x5UnormSrgb:
    return {8, 5, 16, true};
  case wgpu::TextureFormat::ASTC8x6Unorm:
  case wgpu::TextureFormat::ASTC8x6UnormSrgb:
    return {8, 6, 16, true};
  case wgpu::TextureFormat::ASTC8x8Unorm:
  case wgpu::TextureFormat::ASTC8x8UnormSrgb:
    return {8, 8, 16, true};
  case wgpu::TextureFormat::ASTC10x5Unorm:
  case wgpu::TextureFormat::ASTC10x5UnormSrgb:
    return {10, 5, 16, true};
  case wgpu::TextureFormat::ASTC10x6Unorm:
  case wgpu::TextureFormat::ASTC10x6UnormSrgb:
    return {10, 6, 16, true};
  case wgpu::TextureFormat::ASTC10x8Unorm:
  case wgpu::TextureFormat::ASTC10x8UnormSrgb:
    return {10, 8, 16, true};
  case wgpu::TextureFormat::ASTC10x10Unorm:
  case wgpu::TextureFormat::ASTC10x10UnormSrgb:
    return {10, 10, 16, true};
  case wgpu::TextureFormat::ASTC12x10Unorm:
  case wgpu::TextureFormat::ASTC12x10UnormSrgb:
    return {12, 10, 16, true};
  case wgpu::TextureFormat::ASTC12x12Unorm:
  case wgpu::TextureFormat::ASTC12x12UnormSrgb:
    return {12, 12, 16, true};
  }
}

uint64_t calc_texture_size(wgpu::TextureFormat format, u32 width, u32 height, u32 mips) noexcept {
  const auto info = format_info(format);
  uint64_t total = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t mipWidth = std::max(width >> mip, 1u);
    const uint32_t mipHeight = std::max(height >> mip, 1u);
    const uint64_t widthBlocks = div_ceil(mipWidth, info.blockWidth);
    const uint64_t heightBlocks = div_ceil(mipHeight, info.blockHeight);
    const uint64_t mipBytes = widthBlocks * heightBlocks * info.blockSize;
    total += mipBytes;
  }
  return total;
}

bool is_block_aligned(wgpu::TextureFormat format, uint32_t width, uint32_t height) noexcept {
  if (width == 0 || height == 0) {
    return false;
  }

  const auto info = format_info(format);
  return !info.compressed || (width % info.blockWidth == 0 && height % info.blockHeight == 0);
}
} // namespace aurora::gfx

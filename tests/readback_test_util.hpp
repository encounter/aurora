#pragma once

#include "gfx/readback/copy_layout.hpp"
#include "gfx/readback/readback.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <vector>

// CPU models of the readback shaders
namespace aurora::gfx::readback::test {

struct Texel {
  uint32_t x;
  uint32_t y;
};

struct ByteSource {
  Texel texel;
  uint32_t component;
};

constexpr ByteSource byte_source(const Format& f, uint32_t offset, uint32_t width) noexcept {
  const uint32_t tile = offset / f.tileBytes;
  const uint32_t within = offset % f.tileBytes;
  const uint32_t tx = tile % tiles_x(f, width);
  const uint32_t ty = tile / tiles_x(f, width);
  const uint32_t baseX = tx * f.tileWidth;
  const uint32_t baseY = ty * f.tileHeight;
  switch (f.family) {
  case Family::Bpp4:
    return {{baseX + (within % 4) * 2, baseY + within / 4}, 0};
  case Family::Bpp8:
    return {{baseX + within % 8, baseY + within / 8}, 0};
  case Family::Bpp16: {
    const uint32_t t = within / 2;
    return {{baseX + t % 4, baseY + t / 4}, within % 2};
  }
  case Family::Bpp32: {
    const uint32_t block = within / 32;
    const uint32_t t = (within % 32) / 2;
    return {{baseX + t % 4, baseY + t / 4}, block * 2 + within % 2};
  }
  }
  return {};
}

constexpr uint32_t texel_offset_8bit(uint32_t x, uint32_t y, uint32_t width) noexcept {
  const uint32_t tilesPerRow = (width + 7) / 8;
  return ((y / 4) * tilesPerRow + x / 8) * 32 + (y % 4) * 8 + x % 8;
}

constexpr uint32_t source_coord(uint32_t x, uint32_t logical, uint32_t source) noexcept {
  const uint64_t s = (static_cast<uint64_t>(2 * x + 1) * source) / (2ull * logical);
  return static_cast<uint32_t>(s < source ? s : source - 1);
}

constexpr uint32_t quantize_rgba6(uint32_t v) noexcept { return (v & 0xFCu) | (v >> 6); }

constexpr uint32_t nibble(uint32_t v) noexcept { return (v * 15 + 127) / 255; }

struct Rgba8 {
  uint32_t r, g, b, a;
};

constexpr uint32_t encode_byte(const Format& f, Rgba8 c, Rgba8 next, uint32_t component, bool rgba6) noexcept {
  if (rgba6 && f.rawColor) {
    c = {quantize_rgba6(c.r), quantize_rgba6(c.g), quantize_rgba6(c.b), quantize_rgba6(c.a)};
  }
  switch (f.encoding) {
  case Encoding::Red:
    return c.r;
  case Encoding::Nibble:
    return (nibble(c.r) << 4) | nibble(next.r);
  case Encoding::NibblePairAR:
    return (nibble(c.a) << 4) | nibble(c.r);
  case Encoding::PairAR:
    return component == 0 ? c.a : c.r;
  case Encoding::RGB565: {
    const uint32_t v = ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
    return component == 0 ? v >> 8 : v & 0xFF;
  }
  case Encoding::RGB5A3: {
    const uint32_t v = c.a >= 224 ? 0x8000u | ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3)
                                  : ((c.a >> 5) << 12) | ((c.r >> 4) << 8) | ((c.g >> 4) << 4) | (c.b >> 4);
    return component == 0 ? v >> 8 : v & 0xFF;
  }
  case Encoding::RGBA8: {
    const uint32_t channels[4] = {c.a, c.r, c.g, c.b};
    return channels[component];
  }
  }
  return 0;
}

// CPU reference for the packing shader
inline std::vector<uint8_t> pack_reference(uint32_t fmt, uint32_t width, uint32_t height, uint32_t srcWidth,
                                           uint32_t srcHeight, std::span<const uint8_t> texels, bool rgba6) {
  const auto f = format_info(fmt);
  const uint32_t size = byte_size(fmt, width, height);
  std::vector<uint8_t> out(size);
  if (!f || size == 0 || srcWidth == 0 || srcHeight == 0 || texels.size() < size_t{srcWidth} * srcHeight * 4) {
    return out;
  }
  const auto load = [&](uint32_t x, uint32_t y) -> Rgba8 {
    if (x >= width || y >= height) {
      return {};
    }
    const uint32_t sx = source_coord(x, width, srcWidth);
    const uint32_t sy = source_coord(y, height, srcHeight);
    const uint8_t* p = &texels[(size_t{sy} * srcWidth + sx) * 4];
    return {p[0], p[1], p[2], p[3]};
  };
  for (uint32_t offset = 0; offset < size; ++offset) {
    const auto src = byte_source(*f, offset, width);
    const Rgba8 c = load(src.texel.x, src.texel.y);
    const Rgba8 next = f->family == Family::Bpp4 ? load(src.texel.x + 1, src.texel.y) : Rgba8{};
    out[offset] = static_cast<uint8_t>(encode_byte(*f, c, next, src.component, rgba6));
  }
  return out;
}

// --- EFB gather (readback/efb_gather.cpp) ---

struct EfbModel {
  // Values at render-target pixels: forward GX Z24, and RGBA8 color.
  std::function<uint32_t(uint32_t x, uint32_t y)> depth;
  std::function<std::array<uint8_t, 4>(uint32_t x, uint32_t y)> color;
};

inline uint32_t target_coord(uint32_t v, uint32_t num, uint32_t den, uint32_t size) {
  return std::min<uint32_t>(((2 * v + 1) * num) / (2 * den), size - 1);
}

inline std::array<uint32_t, 4> efb_quantize(std::array<uint32_t, 4> v, uint32_t pixelFormat) {
  switch (pixelFormat) {
  case GX_PF_RGBA6_Z24:
    for (auto& c : v) {
      c = (c & 0xFC) | (c >> 6);
    }
    return v;
  case GX_PF_RGB565_Z16:
    return {(v[0] & 0xF8) | (v[0] >> 5), (v[1] & 0xFC) | (v[1] >> 6), (v[2] & 0xF8) | (v[2] >> 5), 255};
  default:
    return {v[0], v[1], v[2], 255};
  }
}

inline std::vector<uint8_t> gather_reference(const EfbGatherParams& params, std::span<const EfbGatherJob> jobs,
                                             const EfbModel& model) {
  std::vector<uint8_t> out(size_t{params.totalWords} * 4);
  for (const auto& job : jobs) {
    for (uint32_t i = 0; i < job.width * job.height; ++i) {
      const uint32_t x = target_coord(job.x + i % job.width, params.scaleNumX, params.scaleDenX, params.targetWidth);
      const uint32_t y = target_coord(job.y + i / job.width, params.scaleNumY, params.scaleDenY, params.targetHeight);
      uint32_t value;
      if (job.color == 0) {
        value = model.depth(x, y) & 0x00FFFFFFu;
      } else {
        const auto c8 = model.color(x, y);
        auto c = efb_quantize({c8[0], c8[1], c8[2], c8[3]}, job.pixelFormat);
        if (job.alphaMode == GX_READ_00) {
          c[3] = 0;
        } else if (job.alphaMode == GX_READ_FF) {
          c[3] = 255;
        }
        value = (c[3] << 24) | (c[0] << 16) | (c[1] << 8) | c[2];
      }
      std::memcpy(&out[size_t{job.outWord + i} * 4], &value, 4);
    }
  }
  return out;
}

} // namespace aurora::gfx::readback::test

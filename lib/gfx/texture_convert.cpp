#include "texture_convert.hpp"

#include "../internal.hpp"
#include "../gx/gx_fmt.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace aurora::gfx {
static Module Log("aurora::gfx");

struct RGBA8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

namespace {
constexpr float kArbMipThreshold = 15.0f;

size_t calc_size_rgba8(uint32_t width, uint32_t height) {
  return static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(RGBA8);
}

size_t calc_offset_rgba8(uint32_t x, uint32_t y, uint32_t width) {
  return (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * sizeof(RGBA8);
}

/**
 * Downscales RGBA8 data using a simple box filter.
 */
ByteBuffer downscale(const uint8_t* src, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight) {
  ByteBuffer dst{calc_size_rgba8(dstWidth, dstHeight)};
  auto* dstPixels = dst.data();
  for (uint32_t y = 0; y < dstHeight; ++y) {
    const uint32_t srcY0 = std::min(y * 2, srcHeight - 1);
    const uint32_t srcY1 = std::min(srcY0 + 1, srcHeight - 1);
    for (uint32_t x = 0; x < dstWidth; ++x) {
      const uint32_t srcX0 = std::min(x * 2, srcWidth - 1);
      const uint32_t srcX1 = std::min(srcX0 + 1, srcWidth - 1);
      const size_t sampleOffsets[4] = {
          calc_offset_rgba8(srcX0, srcY0, srcWidth),
          calc_offset_rgba8(srcX1, srcY0, srcWidth),
          calc_offset_rgba8(srcX0, srcY1, srcWidth),
          calc_offset_rgba8(srcX1, srcY1, srcWidth),
      };
      uint8_t* out = dstPixels + calc_offset_rgba8(x, y, dstWidth);
      for (size_t channel = 0; channel < 4; ++channel) {
        uint32_t sum = 0;
        for (const size_t offset : sampleOffsets) {
          sum += src[offset + channel];
        }
        out[channel] = static_cast<uint8_t>((sum + 2) / 4);
      }
    }
  }
  return dst;
}

float avg_diff(const uint8_t* lhs, const uint8_t* rhs, uint32_t width, uint32_t height) {
  double diffSum = 0.0;
  const size_t byteCount = calc_size_rgba8(width, height);
  for (size_t i = 0; i < byteCount; ++i) {
    const int diff = static_cast<int>(lhs[i]) - static_cast<int>(rhs[i]);
    diffSum += static_cast<double>(diff * diff);
  }
  const double sampleCount = static_cast<double>(width) * static_cast<double>(height) * 4.0;
  return static_cast<float>(std::sqrt(diffSum / sampleCount) / 2.56);
}

/**
 * Attempts to detect whether the mipmaps of a texture are manually-authored, which is sometimes used for mipmap-based
 * distance effects. This is achieved by downscaling the mipmaps using a box filter and comparing the average pixel
 * difference between the original and downscaled mipmaps.
 */
bool arb_mip_check(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  if (mips < 2) {
    return false;
  }

  std::array<const uint8_t*, 10> levels{};
  std::array<uint32_t, 10> widths{};
  std::array<uint32_t, 10> heights{};
  CHECK(mips <= levels.size(), "arb_mip_check: unsupported mip count {}", mips);

  size_t offset = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const size_t mipSize = calc_size_rgba8(width, height);
    CHECK(offset + mipSize <= data.size(), "arb_mip_check: expected {} bytes, got {}", offset + mipSize, data.size());
    levels[mip] = data.data() + offset;
    widths[mip] = width;
    heights[mip] = height;
    offset += mipSize;
    width = std::max(width >> 1, 1u);
    height = std::max(height >> 1, 1u);
  }

  ByteBuffer downscaled;
  float totalDiff = 0.0f;
  for (uint32_t mip = 0; mip + 1 < mips; ++mip) {
    const uint8_t* src = mip == 0 ? levels[mip] : downscaled.data();
    downscaled = downscale(src, widths[mip], heights[mip], widths[mip + 1], heights[mip + 1]);
    totalDiff += avg_diff(levels[mip + 1], downscaled.data(), widths[mip + 1], heights[mip + 1]);
  }
  return (totalDiff / static_cast<float>(mips - 1)) > kArbMipThreshold;
}
} // namespace

// http://www.mindcontrol.org/~hplus/graphics/expand-bits.html
template <uint8_t v>
constexpr uint8_t ExpandTo8(uint8_t n) {
  if constexpr (v == 3) {
    return (n << (8 - 3)) | (n << (8 - 6)) | (n >> (9 - 8));
  } else {
    return (n << (8 - v)) | (n >> ((v * 2) - 8));
  }
}

constexpr uint8_t S3TCBlend(uint32_t a, uint32_t b) {
  return static_cast<uint8_t>((((a << 1) + a) + ((b << 2) + b)) >> 3);
}

constexpr uint8_t HalfBlend(uint8_t a, uint8_t b) {
  return static_cast<uint8_t>((static_cast<uint32_t>(a) + static_cast<uint32_t>(b)) >> 1);
}

static size_t ComputeMippedTexelCount(uint32_t w, uint32_t h, uint32_t mips) {
  size_t ret = w * h;
  for (uint32_t i = mips; i > 1; --i) {
    if (w > 1) {
      w /= 2;
    }
    if (h > 1) {
      h /= 2;
    }
    ret += w * h;
  }
  return ret;
}

template <typename T>
concept TextureDecoder = requires(T) {
  typename T::Source;
  typename T::Target;
  { T::Frac } -> std::convertible_to<uint32_t>;
  { T::BlockWidth } -> std::convertible_to<uint32_t>;
  { T::BlockHeight } -> std::convertible_to<uint32_t>;
  { T::decode_texel(std::declval<typename T::Target*>(), std::declval<const typename T::Source*>(), 0u) };
};

template <TextureDecoder T>
static ByteBuffer DecodeTiled(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{texelCount * sizeof(typename T::Target)};

  uint32_t w = width;
  uint32_t h = height;
  auto* targetMip = reinterpret_cast<typename T::Target*>(buf.data());
  const auto* in = reinterpret_cast<const typename T::Source*>(data.data());
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + (T::BlockWidth - 1)) / T::BlockWidth;
    const uint32_t bheight = (h + (T::BlockHeight - 1)) / T::BlockHeight;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * T::BlockHeight;
      const uint32_t numRows = std::min(h - baseY, T::BlockHeight);
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * T::BlockWidth;
        for (uint32_t y = 0; y < numRows; ++y) {
          auto* target = targetMip + (baseY + y) * w + baseX;
          const auto n = std::min(w - baseX, T::BlockWidth);
          for (uint32_t x = 0; x < n; ++x) {
            T::decode_texel(target, in, x);
          }
          in += T::BlockWidth / T::Frac;
        }
        const uint32_t extraY = T::BlockHeight - numRows;
        in += T::BlockWidth * extraY / T::Frac;
      }
    }
    targetMip += w * h;
    if (w > 1) {
      w /= 2;
    }
    if (h > 1) {
      h /= 2;
    }
  }

  return buf;
}

template <TextureDecoder T>
static ByteBuffer DecodeLinear(uint32_t width, ArrayRef<uint8_t> data) {
  ByteBuffer buf{width * sizeof(typename T::Target)};
  auto* target = reinterpret_cast<typename T::Target*>(buf.data());
  const auto* in = reinterpret_cast<const typename T::Source*>(data.data());
  for (uint32_t x = 0; x < width; ++x) {
    T::decode_texel(target, in, x);
  }
  return buf;
}

struct TextureDecoderI4 {
  using Source = uint8_t;
  using Target = RGBA8;

  static constexpr uint32_t Frac = 2;
  static constexpr uint32_t BlockWidth = 8;
  static constexpr uint32_t BlockHeight = 8;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) {
    const uint8_t intensity = ExpandTo8<4>(in[x / 2] >> (x & 1 ? 0 : 4) & 0xf);
    target[x].r = intensity;
    target[x].g = intensity;
    target[x].b = intensity;
    target[x].a = intensity;
  }
};

struct TextureDecoderI8 {
  using Source = uint8_t;
  using Target = RGBA8;

  static constexpr uint32_t Frac = 1;
  static constexpr uint32_t BlockWidth = 8;
  static constexpr uint32_t BlockHeight = 4;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) {
    const uint8_t intensity = in[x];
    target[x].r = intensity;
    target[x].g = intensity;
    target[x].b = intensity;
    target[x].a = intensity;
  }
};

struct TextureDecoderIA4 {
  using Source = uint8_t;
  using Target = RGBA8;

  static constexpr uint32_t Frac = 1;
  static constexpr uint32_t BlockWidth = 8;
  static constexpr uint32_t BlockHeight = 4;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) {
    const uint8_t intensity = ExpandTo8<4>(in[x] & 0xf);
    target[x].r = intensity;
    target[x].g = intensity;
    target[x].b = intensity;
    target[x].a = ExpandTo8<4>(in[x] >> 4);
  }
};

struct TextureDecoderIA8 {
  using Source = uint16_t;
  using Target = RGBA8;

  static constexpr uint32_t Frac = 1;
  static constexpr uint32_t BlockWidth = 4;
  static constexpr uint32_t BlockHeight = 4;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) {
    const uint8_t intensity = in[x] >> 8;
    target[x].r = intensity;
    target[x].g = intensity;
    target[x].b = intensity;
    target[x].a = in[x] & 0xff;
  }
};

struct TextureDecoderC4 {
  using Source = uint8_t;
  using Target = uint16_t;

  static constexpr uint32_t Frac = 2;
  static constexpr uint32_t BlockWidth = 8;
  static constexpr uint32_t BlockHeight = 8;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) {
    target[x] = in[x / 2] >> (x & 1 ? 0 : 4) & 0xf;
  }
};

struct TextureDecoderC8 {
  using Source = uint8_t;
  using Target = uint16_t;

  static constexpr uint32_t Frac = 1;
  static constexpr uint32_t BlockWidth = 8;
  static constexpr uint32_t BlockHeight = 4;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) { target[x] = in[x]; }
};

struct TextureDecoderRGB565 {
  using Source = uint16_t;
  using Target = RGBA8;

  static constexpr uint32_t Frac = 1;
  static constexpr uint32_t BlockWidth = 4;
  static constexpr uint32_t BlockHeight = 4;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) {
    const auto texel = bswap(in[x]);
    target[x].r = ExpandTo8<5>(texel >> 11 & 0x1f);
    target[x].g = ExpandTo8<6>(texel >> 5 & 0x3f);
    target[x].b = ExpandTo8<5>(texel & 0x1f);
    target[x].a = 0xff;
  }
};

struct TextureDecoderRGB5A3 {
  using Source = uint16_t;
  using Target = RGBA8;

  static constexpr uint32_t Frac = 1;
  static constexpr uint32_t BlockWidth = 4;
  static constexpr uint32_t BlockHeight = 4;

  static void decode_texel(Target* target, const Source* in, const uint32_t x) {
    const auto texel = bswap(in[x]);
    if ((texel & 0x8000) != 0) {
      target[x].r = ExpandTo8<5>(texel >> 10 & 0x1f);
      target[x].g = ExpandTo8<5>(texel >> 5 & 0x1f);
      target[x].b = ExpandTo8<5>(texel & 0x1f);
      target[x].a = 0xff;
    } else {
      target[x].r = ExpandTo8<4>(texel >> 8 & 0xf);
      target[x].g = ExpandTo8<4>(texel >> 4 & 0xf);
      target[x].b = ExpandTo8<4>(texel & 0xf);
      target[x].a = ExpandTo8<3>(texel >> 12 & 0x7);
    }
  }
};

static ByteBuffer BuildRGBA8FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{sizeof(RGBA8) * texelCount};

  uint32_t w = width;
  uint32_t h = height;
  auto* targetMip = reinterpret_cast<RGBA8*>(buf.data());
  const uint8_t* in = data.data();
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 3) / 4;
    const uint32_t bheight = (h + 3) / 4;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 4;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 4;
        for (uint32_t c = 0; c < 2; ++c) {
          for (uint32_t y = 0; y < 4; ++y) {
            RGBA8* target = targetMip + (baseY + y) * w + baseX;
            for (size_t x = 0; x < 4; ++x) {
              if (c != 0) {
                target[x].g = in[x * 2];
                target[x].b = in[x * 2 + 1];
              } else {
                target[x].a = in[x * 2];
                target[x].r = in[x * 2 + 1];
              }
            }
            in += 8;
          }
        }
      }
    }
    targetMip += w * h;
    if (w > 1) {
      w /= 2;
    }
    if (h > 1) {
      h /= 2;
    }
  }

  return buf;
}

static ByteBuffer BuildRGBA8FromCMPR(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{sizeof(RGBA8) * texelCount};

  uint32_t h = height;
  uint32_t w = width;
  uint8_t* dst = buf.data();
  const uint8_t* src = data.data();
  for (uint32_t mip = 0; mip < mips; ++mip) {
    for (uint32_t yy = 0; yy < h; yy += 8) {
      for (uint32_t xx = 0; xx < w; xx += 8) {
        for (uint32_t yb = 0; yb < 8; yb += 4) {
          for (uint32_t xb = 0; xb < 8; xb += 4) {
            // CMPR difference: Big-endian color1/2
            const uint16_t color1 = bswap(*reinterpret_cast<const uint16_t*>(src));
            const uint16_t color2 = bswap(*reinterpret_cast<const uint16_t*>(src + 2));
            src += 4;

            // Fill in first two colors in color table.
            std::array<uint8_t, 16> color_table{};

            color_table[0] = ExpandTo8<5>(static_cast<uint8_t>((color1 >> 11) & 0x1F));
            color_table[1] = ExpandTo8<6>(static_cast<uint8_t>((color1 >> 5) & 0x3F));
            color_table[2] = ExpandTo8<5>(static_cast<uint8_t>(color1 & 0x1F));
            color_table[3] = 0xFF;

            color_table[4] = ExpandTo8<5>(static_cast<uint8_t>((color2 >> 11) & 0x1F));
            color_table[5] = ExpandTo8<6>(static_cast<uint8_t>((color2 >> 5) & 0x3F));
            color_table[6] = ExpandTo8<5>(static_cast<uint8_t>(color2 & 0x1F));
            color_table[7] = 0xFF;
            if (color1 > color2) {
              // Predict gradients.
              color_table[8] = S3TCBlend(color_table[4], color_table[0]);
              color_table[9] = S3TCBlend(color_table[5], color_table[1]);
              color_table[10] = S3TCBlend(color_table[6], color_table[2]);
              color_table[11] = 0xFF;

              color_table[12] = S3TCBlend(color_table[0], color_table[4]);
              color_table[13] = S3TCBlend(color_table[1], color_table[5]);
              color_table[14] = S3TCBlend(color_table[2], color_table[6]);
              color_table[15] = 0xFF;
            } else {
              color_table[8] = HalfBlend(color_table[0], color_table[4]);
              color_table[9] = HalfBlend(color_table[1], color_table[5]);
              color_table[10] = HalfBlend(color_table[2], color_table[6]);
              color_table[11] = 0xFF;

              // CMPR difference: GX fills with an alpha 0 midway point here.
              color_table[12] = color_table[8];
              color_table[13] = color_table[9];
              color_table[14] = color_table[10];
              color_table[15] = 0;
            }

            for (uint32_t y = 0; y < 4; ++y) {
              uint8_t bits = src[y];
              for (uint32_t x = 0; x < 4; ++x) {
                if (xx + xb + x >= w || yy + yb + y >= h) {
                  continue;
                }
                uint8_t* dstOffs = dst + ((yy + yb + y) * w + (xx + xb + x)) * 4;
                const uint8_t* colorTableOffs = &color_table[static_cast<size_t>((bits >> 6) & 3) * 4];
                memcpy(dstOffs, colorTableOffs, 4);
                bits <<= 2;
              }
            }
            src += 4;
          }
        }
      }
    }
    dst += w * h * 4;
    if (w > 1) {
      w /= 2;
    }
    if (h > 1) {
      h /= 2;
    }
  }

  return buf;
}

ConvertedTexture convert_texture(u32 format, uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  ByteBuffer converted;
  switch (format) {
    DEFAULT_FATAL("convert_texture: unknown texture format {}", format);
  case GX_TF_R8_PC:
    converted = DecodeLinear<TextureDecoderI8>(width * height, data);
    break;
  case GX_TF_RGBA8_PC:
    return {}; // No conversion
  case GX_TF_I4:
    converted = DecodeTiled<TextureDecoderI4>(width, height, mips, data);
    break;
  case GX_TF_I8:
    converted = DecodeTiled<TextureDecoderI8>(width, height, mips, data);
    break;
  case GX_TF_IA4:
    converted = DecodeTiled<TextureDecoderIA4>(width, height, mips, data);
    break;
  case GX_TF_IA8:
    converted = DecodeTiled<TextureDecoderIA8>(width, height, mips, data);
    break;
  case GX_TF_C4:
    converted = DecodeTiled<TextureDecoderC4>(width, height, mips, data);
    break;
  case GX_TF_C8:
    converted = DecodeTiled<TextureDecoderC8>(width, height, mips, data);
    break;
  case GX_TF_C14X2:
    FATAL("convert_texture: C14X2 unimplemented");
  case GX_TF_RGB565:
    converted = DecodeTiled<TextureDecoderRGB565>(width, height, mips, data);
    break;
  case GX_TF_RGB5A3:
    converted = DecodeTiled<TextureDecoderRGB5A3>(width, height, mips, data);
    break;
  case GX_TF_RGBA8:
    converted = BuildRGBA8FromGCN(width, height, mips, data);
    break;
  case GX_TF_CMPR:
    converted = BuildRGBA8FromCMPR(width, height, mips, data);
    break;
  }
  const auto wgpuFormat = to_wgpu(format);
  bool hasArbitraryMips = false;
  if (wgpuFormat == wgpu::TextureFormat::RGBA8Unorm && mips > 1) {
    hasArbitraryMips = arb_mip_check(width, height, mips, converted);
  }
  return {
      .format = wgpuFormat,
      .width = width,
      .height = height,
      .mips = mips,
      .data = std::move(converted),
      .hasArbitraryMips = hasArbitraryMips,
  };
}

ConvertedTexture convert_tlut(u32 format, uint32_t width, ArrayRef<uint8_t> data) {
  ByteBuffer converted;
  switch (format) {
    DEFAULT_FATAL("convert_tlut: unsupported tlut format {}", format);
  case GX_TF_IA8: // GX_TL_IA8
    converted = DecodeLinear<TextureDecoderIA8>(width, data);
    break;
  case GX_TF_RGB565: // GX_TL_RGB565
    converted = DecodeLinear<TextureDecoderRGB565>(width, data);
    break;
  case GX_TF_RGB5A3: // GX_TL_RGB5A3
    converted = DecodeLinear<TextureDecoderRGB5A3>(width, data);
    break;
  }
  return {
      .format = wgpu::TextureFormat::R16Sint,
      .width = width,
      .height = 1,
      .mips = 1,
      .data = std::move(converted),
  };
}

GXTexFmt tlut_texture_format(GXTlutFmt format) noexcept {
  switch (format) {
    DEFAULT_FATAL("tlut_texture_format: unsupported tlut format {}", format);
  case GX_TL_IA8:
    return GX_TF_IA8;
  case GX_TL_RGB565:
    return GX_TF_RGB565;
  case GX_TL_RGB5A3:
    return GX_TF_RGB5A3;
  }
}

ConvertedTexture convert_texture_palette(u32 textureFormat, uint32_t width, uint32_t height, uint32_t mips,
                                         ArrayRef<uint8_t> textureData, GXTlutFmt tlutFormat, uint16_t tlutEntries,
                                         ArrayRef<uint8_t> tlutData) {
  const auto indices = convert_texture(textureFormat, width, height, mips, textureData);
  if (indices.data.empty()) {
    return {};
  }
  const auto palette = convert_tlut(tlut_texture_format(tlutFormat), tlutEntries, tlutData);
  if (palette.data.empty()) {
    return {};
  }

  ByteBuffer pixels;
  pixels.reserve_extra(indices.data.size() / sizeof(u16) * 4);

  const auto* indexData = reinterpret_cast<const u16*>(indices.data.data());
  size_t offset = 0;
  for (u32 mip = 0; mip < mips; ++mip) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixelCount; ++i) {
      const u32 index = indexData[offset + i];
      if (index >= tlutEntries) {
        constexpr uint8_t transparent[4] = {0, 0, 0, 0};
        pixels.append(transparent, sizeof(transparent));
        continue;
      }
      if (tlutFormat == GX_TL_IA8) {
        const size_t src = static_cast<size_t>(index) * 2;
        const u8 intensity = palette.data.data()[src];
        const uint8_t rgba[4] = {intensity, intensity, intensity, palette.data.data()[src + 1]};
        pixels.append(rgba, sizeof(rgba));
      } else {
        const size_t src = static_cast<size_t>(index) * 4;
        pixels.append(palette.data.data() + src, 4);
      }
    }
    offset += pixelCount;
    width = std::max(width >> 1, 1u);
    height = std::max(height >> 1, 1u);
  }

  bool hasArbitraryMips = arb_mip_check(width, height, mips, pixels);
  return {
      .format = wgpu::TextureFormat::RGBA8Unorm,
      .width = width,
      .height = height,
      .mips = mips,
      .data = std::move(pixels),
      .hasArbitraryMips = hasArbitraryMips,
  };
}
} // namespace aurora::gfx

#include "texture_convert.hpp"

#include "../internal.hpp"

namespace aurora::gfx {
static Module Log("aurora::gfx");

struct RGBA8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};
struct DXT1Block {
  uint16_t color1;
  uint16_t color2;
  std::array<uint8_t, 4> lines;
};

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

static size_t ComputeMippedBlockCountDXT1(uint32_t w, uint32_t h, uint32_t mips) {
  w /= 4;
  h /= 4;
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
constexpr T bswap16(T val) noexcept {
#if __GNUC__
  return __builtin_bswap16(val);
#elif _WIN32
  return _byteswap_ushort(val);
#else
  return (val = (val << 8) | ((val >> 8) & 0xFF));
#endif
}

static ByteBuffer BuildI4FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{texelCount};

  uint32_t w = width;
  uint32_t h = height;
  uint8_t* targetMip = buf.data();
  const uint8_t* in = data.data();
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 7) / 8;
    const uint32_t bheight = (h + 7) / 8;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 8;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 8;
        for (uint32_t y = 0; y < std::min(h, 8u); ++y) {
          uint8_t* target = targetMip + (baseY + y) * w + baseX;
          for (uint32_t x = 0; x < std::min(w, 8u); ++x) {
            target[x] = ExpandTo8<4>(in[x / 2] >> ((x & 1) ? 0 : 4) & 0xf);
          }
          in += std::min<size_t>(w / 4, 4);
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

static ByteBuffer BuildI8FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{texelCount};

  uint32_t w = width;
  uint32_t h = height;
  auto* targetMip = buf.data();
  const uint8_t* in = data.data();
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 7) / 8;
    const uint32_t bheight = (h + 3) / 4;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 4;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 8;
        for (uint32_t y = 0; y < 4; ++y) {
          uint8_t* target = targetMip + (baseY + y) * w + baseX;
          const auto n = std::min(w, 8u);
          for (size_t x = 0; x < n; ++x) {
            target[x] = in[x];
          }
          in += n;
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

ByteBuffer BuildIA4FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{sizeof(RGBA8) * texelCount};

  uint32_t w = width;
  uint32_t h = height;
  RGBA8* targetMip = reinterpret_cast<RGBA8*>(buf.data());
  const uint8_t* in = data.data();
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 7) / 8;
    const uint32_t bheight = (h + 3) / 4;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 4;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 8;
        for (uint32_t y = 0; y < 4; ++y) {
          RGBA8* target = targetMip + (baseY + y) * w + baseX;
          const auto n = std::min(w, 8u);
          for (size_t x = 0; x < n; ++x) {
            const uint8_t intensity = ExpandTo8<4>(in[x] & 0xf);
            target[x].r = intensity;
            target[x].g = intensity;
            target[x].b = intensity;
            target[x].a = ExpandTo8<4>(in[x] >> 4);
          }
          in += n;
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

ByteBuffer BuildIA8FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{sizeof(RGBA8) * texelCount};

  uint32_t w = width;
  uint32_t h = height;
  auto* targetMip = reinterpret_cast<RGBA8*>(buf.data());
  const auto* in = reinterpret_cast<const uint16_t*>(data.data());
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 3) / 4;
    const uint32_t bheight = (h + 3) / 4;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 4;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 4;
        for (uint32_t y = 0; y < 4; ++y) {
          RGBA8* target = targetMip + (baseY + y) * w + baseX;
          for (size_t x = 0; x < 4; ++x) {
            const auto texel = bswap16(in[x]);
            const uint8_t intensity = texel >> 8;
            target[x].r = intensity;
            target[x].g = intensity;
            target[x].b = intensity;
            target[x].a = texel & 0xff;
          }
          in += 4;
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

ByteBuffer BuildC4FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{texelCount * 2};

  uint32_t w = width;
  uint32_t h = height;
  uint16_t* targetMip = reinterpret_cast<uint16_t*>(buf.data());
  const uint8_t* in = data.data();
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 7) / 8;
    const uint32_t bheight = (h + 7) / 8;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 8;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 8;
        for (uint32_t y = 0; y < std::min(8u, h); ++y) {
          uint16_t* target = targetMip + (baseY + y) * w + baseX;
          const auto n = std::min(w, 8u);
          for (size_t x = 0; x < n; ++x) {
            target[x] = in[x / 2] >> ((x & 1) ? 0 : 4) & 0xf;
          }
          in += n / 2;
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

ByteBuffer BuildC8FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{texelCount * 2};

  uint32_t w = width;
  uint32_t h = height;
  uint16_t* targetMip = reinterpret_cast<uint16_t*>(buf.data());
  const uint8_t* in = data.data();
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 7) / 8;
    const uint32_t bheight = (h + 3) / 4;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 4;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 8;
        for (uint32_t y = 0; y < 4; ++y) {
          uint16_t* target = targetMip + (baseY + y) * w + baseX;
          const auto n = std::min(w, 8u);
          for (size_t x = 0; x < n; ++x) {
            target[x] = in[x];
          }
          in += n;
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

ByteBuffer BuildRGB565FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{sizeof(RGBA8) * texelCount};

  uint32_t w = width;
  uint32_t h = height;
  auto* targetMip = reinterpret_cast<RGBA8*>(buf.data());
  const auto* in = reinterpret_cast<const uint16_t*>(data.data());
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 3) / 4;
    const uint32_t bheight = (h + 3) / 4;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 4;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 4;
        for (uint32_t y = 0; y < std::min(4u, h); ++y) {
          RGBA8* target = targetMip + (baseY + y) * w + baseX;
          for (size_t x = 0; x < std::min(4u, w); ++x) {
            const auto texel = bswap16(in[x]);
            target[x].r = ExpandTo8<5>(texel >> 11 & 0x1f);
            target[x].g = ExpandTo8<6>(texel >> 5 & 0x3f);
            target[x].b = ExpandTo8<5>(texel & 0x1f);
            target[x].a = 0xff;
          }
          in += 4;
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

ByteBuffer BuildRGB5A3FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  ByteBuffer buf{sizeof(RGBA8) * texelCount};

  uint32_t w = width;
  uint32_t h = height;
  auto* targetMip = reinterpret_cast<RGBA8*>(buf.data());
  const auto* in = reinterpret_cast<const uint16_t*>(data.data());
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 3) / 4;
    const uint32_t bheight = (h + 3) / 4;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 4;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 4;
        for (uint32_t y = 0; y < std::min(4u, h); ++y) {
          RGBA8* target = targetMip + (baseY + y) * w + baseX;
          for (size_t x = 0; x < std::min(4u, w); ++x) {
            const auto texel = bswap16(in[x]);
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
          in += 4;
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

ByteBuffer BuildRGBA8FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
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

ByteBuffer BuildDXT1FromGCN(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t blockCount = ComputeMippedBlockCountDXT1(width, height, mips);
  ByteBuffer buf{sizeof(DXT1Block) * blockCount};

  uint32_t w = width / 4;
  uint32_t h = height / 4;
  auto* targetMip = reinterpret_cast<DXT1Block*>(buf.data());
  const auto* in = reinterpret_cast<const DXT1Block*>(data.data());
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t bwidth = (w + 1) / 2;
    const uint32_t bheight = (h + 1) / 2;
    for (uint32_t by = 0; by < bheight; ++by) {
      const uint32_t baseY = by * 2;
      for (uint32_t bx = 0; bx < bwidth; ++bx) {
        const uint32_t baseX = bx * 2;
        for (uint32_t y = 0; y < 2; ++y) {
          DXT1Block* target = targetMip + (baseY + y) * w + baseX;
          for (size_t x = 0; x < 2; ++x) {
            target[x].color1 = bswap16(in[x].color1);
            target[x].color2 = bswap16(in[x].color2);
            for (size_t i = 0; i < 4; ++i) {
              std::array<uint8_t, 4> ind;
              const uint8_t packed = in[x].lines[i];
              ind[3] = packed & 0x3;
              ind[2] = (packed >> 2) & 0x3;
              ind[1] = (packed >> 4) & 0x3;
              ind[0] = (packed >> 6) & 0x3;
              target[x].lines[i] = ind[0] | (ind[1] << 2) | (ind[2] << 4) | (ind[3] << 6);
            }
          }
          in += 2;
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

ByteBuffer BuildRGBA8FromCMPR(uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  const size_t texelCount = ComputeMippedTexelCount(width, height, mips);
  const size_t blockCount = ComputeMippedBlockCountDXT1(width, height, mips);
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
            const uint16_t color1 = bswap16(*reinterpret_cast<const uint16_t*>(src));
            const uint16_t color2 = bswap16(*reinterpret_cast<const uint16_t*>(src + 2));
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

ByteBuffer convert_texture(u32 format, uint32_t width, uint32_t height, uint32_t mips, ArrayRef<uint8_t> data) {
  switch (format) {
    DEFAULT_FATAL("convert_texture: unknown texture format {}", format);
  case GX_TF_R8_PC:
  case GX_TF_RGBA8_PC:
    return {}; // No conversion
  case GX_TF_I4:
    return BuildI4FromGCN(width, height, mips, data);
  case GX_TF_I8:
    return BuildI8FromGCN(width, height, mips, data);
  case GX_TF_IA4:
    return BuildIA4FromGCN(width, height, mips, data);
  case GX_TF_IA8:
    return BuildIA8FromGCN(width, height, mips, data);
  case GX_TF_C4:
    return BuildC4FromGCN(width, height, mips, data);
  case GX_TF_C8:
    return BuildC8FromGCN(width, height, mips, data);
  case GX_TF_C14X2:
    FATAL("convert_texture: C14X2 unimplemented");
  case GX_TF_RGB565:
    return BuildRGB565FromGCN(width, height, mips, data);
  case GX_TF_RGB5A3:
    return BuildRGB5A3FromGCN(width, height, mips, data);
  case GX_TF_RGBA8:
    return BuildRGBA8FromGCN(width, height, mips, data);
  case GX_TF_CMPR:
    if (webgpu::g_device.HasFeature(wgpu::FeatureName::TextureCompressionBC)) {
      return BuildDXT1FromGCN(width, height, mips, data);
    } else {
      return BuildRGBA8FromCMPR(width, height, mips, data);
    }
  }
}
} // namespace aurora::gfx

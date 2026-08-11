#include "dds_io.hpp"

#include "../io.hpp"
#include "texture.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace aurora::gfx::dds {
struct ParsedDDSLayout {
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  size_t dataOffset = 0;
};

struct ParsedDDS {
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  size_t dataOffset = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mips = 0;
};

struct DDSPixelFormat {
  uint32_t size;
  uint32_t flags;
  uint32_t fourCC;
  uint32_t rgbBitCount;
  uint32_t rBitMask;
  uint32_t gBitMask;
  uint32_t bBitMask;
  uint32_t aBitMask;
};
static_assert(sizeof(DDSPixelFormat) == 32);

struct DDSHeader {
  uint32_t size;
  uint32_t flags;
  uint32_t height;
  uint32_t width;
  uint32_t pitchOrLinearSize;
  uint32_t depth;
  uint32_t mipMapCount;
  uint32_t reserved1[11];
  DDSPixelFormat ddspf;
  uint32_t caps;
  uint32_t caps2;
  uint32_t caps3;
  uint32_t caps4;
  uint32_t reserved2;
};
static_assert(sizeof(DDSHeader) == 124);

struct DDSHeaderDX10 {
  uint32_t dxgiFormat;
  uint32_t resourceDimension;
  uint32_t miscFlag;
  uint32_t arraySize;
  uint32_t miscFlags2;
};
static_assert(sizeof(DDSHeaderDX10) == 20);

constexpr uint32_t kDDSMagic = 0x20534444; // "DDS"
constexpr uint32_t kDDSDMipmapCount = 0x00020000;
constexpr uint32_t kDDSCapsComplex = 0x00000008;
constexpr uint32_t kDDSCapsMipmap = 0x00400000;
constexpr uint32_t kDDSCaps2Cubemap = 0x00000200;
constexpr uint32_t kDDSCaps2Volume = 0x00200000;

bool validate_dds_header(const DDSHeader& header) noexcept {
  if (header.size != sizeof(DDSHeader) || header.ddspf.size != sizeof(DDSPixelFormat)) {
    return false;
  }
  if (header.width == 0 || header.height == 0) {
    return false;
  }
  if ((header.caps2 & (kDDSCaps2Cubemap | kDDSCaps2Volume)) != 0) { // Unsupported
    return false;
  }
  return true;
}

uint32_t max_mip_count(uint32_t width, uint32_t height) noexcept {
  uint32_t count = 1;
  while (width > 1 || height > 1) {
    width = std::max(width >> 1, 1u);
    height = std::max(height >> 1, 1u);
    ++count;
  }
  return count;
}

std::optional<uint32_t> resolve_mip_count(const DDSHeader& header) noexcept {
  if (header.mipMapCount <= 1) {
    return 1;
  }

  const bool hasMipFlags = (header.flags & kDDSDMipmapCount) != 0 && (header.caps & kDDSCapsComplex) != 0 &&
                           (header.caps & kDDSCapsMipmap) != 0;
  if (!hasMipFlags || header.mipMapCount > max_mip_count(header.width, header.height)) {
    return std::nullopt;
  }

  return header.mipMapCount;
}

std::optional<wgpu::TextureFormat> resolve_dx10_format(uint32_t dxgiFormat) noexcept {
  switch (dxgiFormat) {
  case 28:
    return wgpu::TextureFormat::RGBA8Unorm;
  case 87:
    return wgpu::TextureFormat::BGRA8Unorm;
  case 71:
    return wgpu::TextureFormat::BC1RGBAUnorm;
  case 77:
    return wgpu::TextureFormat::BC3RGBAUnorm;
  case 83:
    return wgpu::TextureFormat::BC5RGUnorm;
  case 98:
    return wgpu::TextureFormat::BC7RGBAUnorm;
  case 134:
    return wgpu::TextureFormat::ASTC4x4Unorm;
  case 135:
    return wgpu::TextureFormat::ASTC4x4UnormSrgb;
  case 138:
    return wgpu::TextureFormat::ASTC5x4Unorm;
  case 139:
    return wgpu::TextureFormat::ASTC5x4UnormSrgb;
  case 142:
    return wgpu::TextureFormat::ASTC5x5Unorm;
  case 143:
    return wgpu::TextureFormat::ASTC5x5UnormSrgb;
  case 146:
    return wgpu::TextureFormat::ASTC6x5Unorm;
  case 147:
    return wgpu::TextureFormat::ASTC6x5UnormSrgb;
  case 150:
    return wgpu::TextureFormat::ASTC6x6Unorm;
  case 151:
    return wgpu::TextureFormat::ASTC6x6UnormSrgb;
  case 154:
    return wgpu::TextureFormat::ASTC8x5Unorm;
  case 155:
    return wgpu::TextureFormat::ASTC8x5UnormSrgb;
  case 158:
    return wgpu::TextureFormat::ASTC8x6Unorm;
  case 159:
    return wgpu::TextureFormat::ASTC8x6UnormSrgb;
  case 162:
    return wgpu::TextureFormat::ASTC8x8Unorm;
  case 163:
    return wgpu::TextureFormat::ASTC8x8UnormSrgb;
  case 166:
    return wgpu::TextureFormat::ASTC10x5Unorm;
  case 167:
    return wgpu::TextureFormat::ASTC10x5UnormSrgb;
  case 170:
    return wgpu::TextureFormat::ASTC10x6Unorm;
  case 171:
    return wgpu::TextureFormat::ASTC10x6UnormSrgb;
  case 174:
    return wgpu::TextureFormat::ASTC10x8Unorm;
  case 175:
    return wgpu::TextureFormat::ASTC10x8UnormSrgb;
  case 178:
    return wgpu::TextureFormat::ASTC10x10Unorm;
  case 179:
    return wgpu::TextureFormat::ASTC10x10UnormSrgb;
  case 182:
    return wgpu::TextureFormat::ASTC12x10Unorm;
  case 183:
    return wgpu::TextureFormat::ASTC12x10UnormSrgb;
  case 186:
    return wgpu::TextureFormat::ASTC12x12Unorm;
  case 187:
    return wgpu::TextureFormat::ASTC12x12UnormSrgb;
  default:
    return std::nullopt;
  }
}

std::optional<ParsedDDSLayout> resolve_dds_layout(ArrayRef<uint8_t> bytes, const DDSHeader& header) noexcept {
  ParsedDDSLayout out{.dataOffset = sizeof(uint32_t) + sizeof(DDSHeader)};

  if ((header.ddspf.flags & 0x00000004) != 0) { // DDS has FourCC
    switch (header.ddspf.fourCC) {
    case 0x31545844:
      out.format = wgpu::TextureFormat::BC1RGBAUnorm;
      return out;
    case 0x35545844:
      out.format = wgpu::TextureFormat::BC3RGBAUnorm;
      return out;
    case 0x32495441:
      out.format = wgpu::TextureFormat::BC5RGUnorm;
      return out;
    case 0x30315844: {
      if (bytes.size() < out.dataOffset + sizeof(DDSHeaderDX10)) {
        return std::nullopt;
      }
      DDSHeaderDX10 dx10{};
      std::memcpy(&dx10, bytes.data() + out.dataOffset, sizeof(dx10));
      out.dataOffset += sizeof(DDSHeaderDX10);
      if (dx10.resourceDimension != 3 || dx10.arraySize != 1) {
        return std::nullopt;
      }
      const auto format = resolve_dx10_format(dx10.dxgiFormat);
      if (!format.has_value()) {
        return std::nullopt;
      }
      out.format = *format;
      return out;
    }
    default:
      return std::nullopt;
    }
  }

  if ((header.ddspf.flags & 0x00000040) != 0 && header.ddspf.rgbBitCount == 32) {
    if (header.ddspf.rBitMask == 0x000000FF && header.ddspf.gBitMask == 0x0000FF00 &&
        header.ddspf.bBitMask == 0x00FF0000 && header.ddspf.aBitMask == 0xFF000000) {
      out.format = wgpu::TextureFormat::RGBA8Unorm;
      return out;
    }
    if (header.ddspf.rBitMask == 0x00FF0000 && header.ddspf.gBitMask == 0x0000FF00 &&
        header.ddspf.bBitMask == 0x000000FF && header.ddspf.aBitMask == 0xFF000000) {
      out.format = wgpu::TextureFormat::BGRA8Unorm;
      return out;
    }
  }

  return std::nullopt;
}

std::optional<ParsedDDS> parse_dds_header(ArrayRef<uint8_t> bytes) noexcept {
  if (bytes.size() < sizeof(uint32_t) + sizeof(DDSHeader)) {
    return std::nullopt;
  }

  uint32_t magic = 0;
  std::memcpy(&magic, bytes.data(), sizeof(magic));
  if (magic != kDDSMagic) {
    return std::nullopt;
  }

  DDSHeader header{};
  std::memcpy(&header, bytes.data() + sizeof(uint32_t), sizeof(header));
  if (!validate_dds_header(header)) {
    return std::nullopt;
  }

  const auto layout = resolve_dds_layout(bytes, header);
  const auto mipCount = resolve_mip_count(header);
  if (!layout.has_value() || !mipCount.has_value()) {
    return std::nullopt;
  }
  return ParsedDDS{
      .format = layout->format,
      .dataOffset = layout->dataOffset,
      .width = header.width,
      .height = header.height,
      .mips = *mipCount,
  };
}

std::optional<uint32_t> mip_tail_start(const ParsedDDS& dds, uint32_t maxDimension) noexcept {
  if (maxDimension == 0) {
    return std::nullopt;
  }
  for (uint32_t mip = 0; mip < dds.mips; ++mip) {
    const uint32_t width = std::max(dds.width >> mip, 1u);
    const uint32_t height = std::max(dds.height >> mip, 1u);
    if (width <= maxDimension && height <= maxDimension) {
      return mip;
    }
  }
  return std::nullopt;
}

struct MipTailLayout {
  size_t dataOffset = 0;
  size_t dataSize = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mips = 0;
  bool includesBase = false;
};

std::optional<MipTailLayout> mip_tail_layout(const ParsedDDS& dds, uint32_t maxDimension) noexcept {
  const auto startMip = mip_tail_start(dds, maxDimension);
  if (!startMip.has_value()) {
    return std::nullopt;
  }

  const uint64_t prefixSize = *startMip == 0 ? 0 : calc_texture_size(dds.format, dds.width, dds.height, *startMip);
  const uint32_t width = std::max(dds.width >> *startMip, 1u);
  const uint32_t height = std::max(dds.height >> *startMip, 1u);
  const uint32_t mips = dds.mips - *startMip;
  const uint64_t tailSize = calc_texture_size(dds.format, width, height, mips);
  if (tailSize == 0 || prefixSize > SIZE_MAX || tailSize > SIZE_MAX || prefixSize > SIZE_MAX - dds.dataOffset) {
    return std::nullopt;
  }

  return MipTailLayout{
      .dataOffset = dds.dataOffset + static_cast<size_t>(prefixSize),
      .dataSize = static_cast<size_t>(tailSize),
      .width = width,
      .height = height,
      .mips = mips,
      .includesBase = *startMip == 0,
  };
}

std::optional<MipTail> slice_mip_tail(ArrayRef<uint8_t> bytes, const ParsedDDS& dds, uint32_t maxDimension) noexcept {
  const auto layout = mip_tail_layout(dds, maxDimension);
  if (!layout.has_value() || layout->dataOffset > bytes.size() ||
      layout->dataSize > bytes.size() - layout->dataOffset) {
    return std::nullopt;
  }

  ByteBuffer data{layout->dataSize};
  std::memcpy(data.data(), bytes.data() + layout->dataOffset, data.size());
  return MipTail{
      .texture =
          ConvertedTexture{
              .format = dds.format,
              .width = layout->width,
              .height = layout->height,
              .mips = layout->mips,
              .data = std::move(data),
          },
      .includesBase = layout->includesBase,
  };
}

std::optional<ConvertedTexture> parse_dds_bytes(ArrayRef<uint8_t> bytes) noexcept {
  const auto dds = parse_dds_header(bytes);
  if (!dds.has_value()) {
    return std::nullopt;
  }

  const auto expectedSize = calc_texture_size(dds->format, dds->width, dds->height, dds->mips);
  if (expectedSize == 0 || expectedSize > SIZE_MAX || dds->dataOffset > bytes.size() ||
      static_cast<size_t>(expectedSize) > bytes.size() - dds->dataOffset) {
    return std::nullopt;
  }

  ByteBuffer data{static_cast<size_t>(expectedSize)};
  std::memcpy(data.data(), bytes.data() + dds->dataOffset, data.size());
  return ConvertedTexture{
      .format = dds->format,
      .width = dds->width,
      .height = dds->height,
      .mips = dds->mips,
      .data = std::move(data),
  };
}

std::optional<ConvertedTexture> load_dds_file(const std::filesystem::path& path) noexcept {
  const auto bytes = io::read_file(path);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return parse_dds_bytes(*bytes);
}

std::optional<MipTail> parse_dds_mip_tail(ArrayRef<uint8_t> bytes, uint32_t maxDimension) noexcept {
  const auto dds = parse_dds_header(bytes);
  return dds.has_value() ? slice_mip_tail(bytes, *dds, maxDimension) : std::nullopt;
}

std::optional<MipTail> load_dds_mip_tail(const std::filesystem::path& path, uint32_t maxDimension) noexcept {
  constexpr size_t MaxHeaderSize = sizeof(uint32_t) + sizeof(DDSHeader) + sizeof(DDSHeaderDX10);
  auto file = io::open_file(path, "rb");
  if (!file) {
    return std::nullopt;
  }
  const Sint64 end = SDL_GetIOSize(file.get());
  if (end <= 0 || static_cast<uint64_t>(end) > SIZE_MAX) {
    return std::nullopt;
  }
  const size_t fileSize = static_cast<size_t>(end);
  std::array<uint8_t, MaxHeaderSize> headerBytes{};
  const size_t headerSize = std::min(fileSize, headerBytes.size());
  if (!io::read_at(file.get(), 0, headerBytes.data(), headerSize)) {
    return std::nullopt;
  }

  const auto dds = parse_dds_header({headerBytes.data(), headerSize});
  if (!dds.has_value()) {
    return std::nullopt;
  }
  const auto layout = mip_tail_layout(*dds, maxDimension);
  if (!layout.has_value() || layout->dataOffset > fileSize || layout->dataSize > fileSize - layout->dataOffset) {
    return std::nullopt;
  }

  ByteBuffer data{layout->dataSize};
  if (!io::read_at(file.get(), layout->dataOffset, data.data(), data.size())) {
    return std::nullopt;
  }
  return MipTail{
      .texture =
          ConvertedTexture{
              .format = dds->format,
              .width = layout->width,
              .height = layout->height,
              .mips = layout->mips,
              .data = std::move(data),
          },
      .includesBase = layout->includesBase,
  };
}

ByteBuffer encode_rgba8_dds(uint32_t width, uint32_t height, ArrayRef<uint8_t> pixels) {
  DDSHeader header{};
  header.size = sizeof(DDSHeader);
  header.flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8;
  header.height = height;
  header.width = width;
  header.pitchOrLinearSize = width * 4;
  header.mipMapCount = 1;
  header.ddspf = {
      .size = sizeof(DDSPixelFormat),
      .flags = 0x00000040 | 0x00000001,
      .fourCC = 0,
      .rgbBitCount = 32,
      .rBitMask = 0x000000FF,
      .gBitMask = 0x0000FF00,
      .bBitMask = 0x00FF0000,
      .aBitMask = 0xFF000000,
  };
  header.caps = 0x00001000;

  ByteBuffer bytes{sizeof(uint32_t) + sizeof(DDSHeader) + pixels.size()};
  std::memcpy(bytes.data(), &kDDSMagic, sizeof(kDDSMagic));
  std::memcpy(bytes.data() + sizeof(uint32_t), &header, sizeof(header));
  std::memcpy(bytes.data() + sizeof(uint32_t) + sizeof(DDSHeader), pixels.data(), pixels.size());
  return bytes;
}

bool write_rgba8_dds(const std::filesystem::path& path, uint32_t width, uint32_t height,
                     ArrayRef<uint8_t> pixels) noexcept {
  const auto encoded = encode_rgba8_dds(width, height, pixels);
  return io::write_file(path, {encoded.data(), encoded.size()});
}
} // namespace aurora::gfx::dds

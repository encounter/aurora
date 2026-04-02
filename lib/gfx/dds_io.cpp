#include "dds_io.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace aurora::gfx::dds {
struct ParsedDDSLayout {
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  size_t dataOffset = 0;
};

struct DDSPixelFormat {
  u32 size;
  u32 flags;
  u32 fourCC;
  u32 rgbBitCount;
  u32 rBitMask;
  u32 gBitMask;
  u32 bBitMask;
  u32 aBitMask;
};
static_assert(sizeof(DDSPixelFormat) == 32);

struct DDSHeader {
  u32 size;
  u32 flags;
  u32 height;
  u32 width;
  u32 pitchOrLinearSize;
  u32 depth;
  u32 mipMapCount;
  u32 reserved1[11];
  DDSPixelFormat ddspf;
  u32 caps;
  u32 caps2;
  u32 caps3;
  u32 caps4;
  u32 reserved2;
};
static_assert(sizeof(DDSHeader) == 124);

struct DDSHeaderDX10 {
  u32 dxgiFormat;
  u32 resourceDimension;
  u32 miscFlag;
  u32 arraySize;
  u32 miscFlags2;
};
static_assert(sizeof(DDSHeaderDX10) == 20);

constexpr u32 kDDSMagic = 0x20534444; // "DDS"

struct RawFormatInfo {
  u32 blockWidth = 1;
  u32 blockHeight = 1;
  u32 bytesPerBlock = 0;
};

constexpr u32 ceil_div(u32 value, u32 divisor) noexcept {
  return (value + divisor - 1) / divisor;
}

constexpr u32 align_to(u32 value, u32 alignment) noexcept {
  return (value + alignment - 1) & ~(alignment - 1);
}

std::optional<RawFormatInfo> raw_format_info(wgpu::TextureFormat format) noexcept {
  switch (format) {
  case wgpu::TextureFormat::RGBA8Unorm:
  case wgpu::TextureFormat::BGRA8Unorm:
    return RawFormatInfo{.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4};
  case wgpu::TextureFormat::BC1RGBAUnorm:
    return RawFormatInfo{.blockWidth = 4, .blockHeight = 4, .bytesPerBlock = 8};
  case wgpu::TextureFormat::BC3RGBAUnorm:
  case wgpu::TextureFormat::BC5RGUnorm:
  case wgpu::TextureFormat::BC7RGBAUnorm:
    return RawFormatInfo{.blockWidth = 4, .blockHeight = 4, .bytesPerBlock = 16};
  default:
    return std::nullopt;
  }
}

std::optional<uint64_t> raw_mipmap_chain_byte_size(wgpu::TextureFormat format, u32 width, u32 height, u32 mips) noexcept {
  const auto info = raw_format_info(format);
  if (!info.has_value()) {
    return std::nullopt;
  }

  uint64_t total = 0;
  for (u32 mip = 0; mip < mips; ++mip) {
    const u32 mipWidth = std::max(width >> mip, 1u);
    const u32 mipHeight = std::max(height >> mip, 1u);
    const uint64_t widthBlocks = ceil_div(mipWidth, info->blockWidth);
    const uint64_t heightBlocks = ceil_div(mipHeight, info->blockHeight);
    const uint64_t mipBytes = widthBlocks * heightBlocks * info->bytesPerBlock;
    if (mipBytes > std::numeric_limits<uint64_t>::max() - total) {
      return std::nullopt;
    }
    total += mipBytes;
  }
  return total;
}

bool ensure_directory(const std::filesystem::path& dir) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

bool write_binary_file(const std::filesystem::path& path, const void* data, size_t size) noexcept {
  if (!ensure_directory(path.parent_path())) {
    return false;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  if (size != 0) {
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  }
  return static_cast<bool>(out);
}

std::optional<std::vector<u8>> read_binary_file(const std::filesystem::path& path) noexcept {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return std::nullopt;
  }

  const auto size = static_cast<size_t>(file.tellg());
  if (size == 0) {
    return std::nullopt;
  }

  std::vector<u8> bytes(size);
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    return std::nullopt;
  }

  return bytes;
}

bool validate_dds_header(const DDSHeader& header) noexcept {
  if (header.size != sizeof(DDSHeader) || header.ddspf.size != sizeof(DDSPixelFormat)) {
    return false;
  }
  if (header.width == 0 || header.height == 0) {
    return false;
  }
  if ((header.caps2 & (0x00000200 | 0x00200000)) != 0) { // Unsupported
    return false;
  }
  return true;
}

std::optional<wgpu::TextureFormat> resolve_dx10_format(u32 dxgiFormat) noexcept {
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
  default:
    return std::nullopt;
  }
}

std::optional<size_t> upload_byte_size(wgpu::TextureFormat format, u32 width, u32 height, u32 mips) noexcept {
  const auto info = raw_format_info(format);
  if (!info.has_value()) {
    return std::nullopt;
  }

  size_t total = 0;
  for (u32 mip = 0; mip < mips; ++mip) {
    const u32 mipWidth = std::max(width >> mip, 1u);
    const u32 mipHeight = std::max(height >> mip, 1u);
    const u32 widthBlocks = ceil_div(mipWidth, info->blockWidth);
    const u32 heightBlocks = ceil_div(mipHeight, info->blockHeight);
    const u32 bytesPerRow = widthBlocks * info->bytesPerBlock;
    const u32 uploadBytes = align_to(bytesPerRow, 256u) * heightBlocks;
    if (uploadBytes > std::numeric_limits<size_t>::max() - total) {
      return std::nullopt;
    }
    total += uploadBytes;
  }
  return total;
}

std::optional<uint64_t> storage_byte_size(wgpu::TextureFormat format, u32 width, u32 height, u32 mips) noexcept {
  return raw_mipmap_chain_byte_size(format, width, height, mips);
}

std::optional<ParsedDDSLayout> resolve_dds_layout(const std::vector<u8>& bytes, const DDSHeader& header) noexcept {
  ParsedDDSLayout out{.dataOffset = sizeof(u32) + sizeof(DDSHeader)};

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
      const auto* dx10 = reinterpret_cast<const DDSHeaderDX10*>(bytes.data() + out.dataOffset);
      out.dataOffset += sizeof(DDSHeaderDX10);
      if (dx10->resourceDimension != 3 || dx10->arraySize != 1) {
        return std::nullopt;
      }
      const auto format = resolve_dx10_format(dx10->dxgiFormat);
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
    if (header.ddspf.rBitMask == 0x000000FF && header.ddspf.gBitMask == 0x0000FF00 && header.ddspf.bBitMask == 0x00FF0000 && header.ddspf.aBitMask == 0xFF000000) {
      out.format = wgpu::TextureFormat::RGBA8Unorm;
      return out;
    }
    if (header.ddspf.rBitMask == 0x00FF0000 && header.ddspf.gBitMask == 0x0000FF00 && header.ddspf.bBitMask == 0x000000FF && header.ddspf.aBitMask == 0xFF000000) {
      out.format = wgpu::TextureFormat::BGRA8Unorm;
      return out;
    }
  }

  return std::nullopt;
}


std::optional<DecodedTexture> parse_dds_bytes(const std::vector<u8>& bytes) noexcept {
  if (bytes.size() < sizeof(u32) + sizeof(DDSHeader)) {
    return std::nullopt;
  }

  const u32 magic = *reinterpret_cast<const u32*>(bytes.data());
  if (magic != kDDSMagic) {
    return std::nullopt;
  }

  const auto* header = reinterpret_cast<const DDSHeader*>(bytes.data() + sizeof(u32));
  if (!validate_dds_header(*header)) {
    return std::nullopt;
  }

  const auto parsedLayout = resolve_dds_layout(bytes, *header);
  if (!parsedLayout.has_value()) {
    return std::nullopt;
  }

  const u32 mipCount = std::max(header->mipMapCount, 1u);
  const auto expectedSize = raw_mipmap_chain_byte_size(parsedLayout->format, header->width, header->height, mipCount);
  if (!expectedSize.has_value() || *expectedSize == 0) {
    return std::nullopt;
  }
  if (parsedLayout->dataOffset + *expectedSize > bytes.size()) {
    return std::nullopt;
  }

  DecodedTexture out;
  out.format = parsedLayout->format;
  out.width = header->width;
  out.height = header->height;
  out.mipCount = mipCount;
  out.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(parsedLayout->dataOffset),
                  bytes.begin() + static_cast<std::ptrdiff_t>(parsedLayout->dataOffset + *expectedSize));
  return out;
}

std::optional<DecodedTexture> load_dds_file(const std::filesystem::path& path) noexcept {
  const auto bytes = read_binary_file(path);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return parse_dds_bytes(*bytes);
}

std::vector<u8> encode_rgba8_dds(u32 width, u32 height, const std::vector<u8>& pixels) {
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

  std::vector<u8> bytes(sizeof(u32) + sizeof(DDSHeader) + pixels.size());
  std::memcpy(bytes.data(), &kDDSMagic, sizeof(kDDSMagic));
  std::memcpy(bytes.data() + sizeof(u32), &header, sizeof(header));
  if (!pixels.empty()) {
    std::memcpy(bytes.data() + sizeof(u32) + sizeof(DDSHeader), pixels.data(), pixels.size());
  }
  return bytes;
}

bool write_rgba8_dds(const std::filesystem::path& path, u32 width, u32 height, ArrayRef<u8> pixels) noexcept {
  const std::vector<u8> bytes = encode_rgba8_dds(width, height, std::vector<u8>(pixels.data(), pixels.data() + pixels.size()));
  return write_binary_file(path, bytes.data(), bytes.size());
}
} // namespace aurora::gfx::dds

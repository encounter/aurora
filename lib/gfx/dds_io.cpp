#include "dds_io.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "texture.hpp"

namespace aurora::gfx::dds {
struct ParsedDDSLayout {
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  size_t dataOffset = 0;
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

bool ensure_directory(const std::filesystem::path& dir) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

bool write_binary_file(const std::filesystem::path& path, ArrayRef<uint8_t> data) noexcept {
  if (!ensure_directory(path.parent_path())) {
    return false;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  out.write(reinterpret_cast<const char*>(data.data()), data.size());
  return static_cast<bool>(out);
}

std::optional<ByteBuffer> read_binary_file(const std::filesystem::path& path) noexcept {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return std::nullopt;
  }

  const auto size = static_cast<size_t>(file.tellg());
  if (size == 0) {
    return std::nullopt;
  }

  ByteBuffer bytes{size};
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

std::optional<ConvertedTexture> parse_dds_bytes(ArrayRef<uint8_t> bytes) noexcept {
  if (bytes.size() < sizeof(uint32_t) + sizeof(DDSHeader)) {
    return std::nullopt;
  }

  const uint32_t magic = *reinterpret_cast<const uint32_t*>(bytes.data());
  if (magic != kDDSMagic) {
    return std::nullopt;
  }

  const auto* header = reinterpret_cast<const DDSHeader*>(bytes.data() + sizeof(uint32_t));
  if (!validate_dds_header(*header)) {
    return std::nullopt;
  }

  const auto parsedLayout = resolve_dds_layout(bytes, *header);
  if (!parsedLayout.has_value()) {
    return std::nullopt;
  }

  const uint32_t mipCount = 1u;
  const auto expectedSize = calc_texture_size(parsedLayout->format, header->width, header->height, mipCount);
  if (expectedSize == 0 || parsedLayout->dataOffset + expectedSize > bytes.size()) {
    return std::nullopt;
  }

  ByteBuffer data{expectedSize};
  std::memcpy(data.data(), bytes.data() + parsedLayout->dataOffset, expectedSize);
  return ConvertedTexture{
      .format = parsedLayout->format,
      .width = header->width,
      .height = header->height,
      .mips = mipCount,
      .data = std::move(data),
  };
}

std::optional<ConvertedTexture> load_dds_file(const std::filesystem::path& path) noexcept {
  const auto bytes = read_binary_file(path);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return parse_dds_bytes(*bytes);
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
  return write_binary_file(path, encode_rgba8_dds(width, height, pixels));
}
} // namespace aurora::gfx::dds

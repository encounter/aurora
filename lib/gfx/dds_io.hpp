#pragma once

#include "common.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace aurora::gfx::dds {
struct DecodedTexture {
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  u32 width = 0;
  u32 height = 0;
  u32 mipCount = 0;
  std::vector<u8> data;
};

std::optional<size_t> upload_byte_size(wgpu::TextureFormat format, u32 width, u32 height, u32 mips) noexcept;
std::optional<uint64_t> storage_byte_size(wgpu::TextureFormat format, u32 width, u32 height, u32 mips) noexcept;
std::optional<DecodedTexture> parse_dds_bytes(const std::vector<u8>& bytes) noexcept;
std::optional<DecodedTexture> load_dds_file(const std::filesystem::path& path) noexcept;
std::vector<u8> encode_rgba8_dds(u32 width, u32 height, const std::vector<u8>& pixels);
bool write_rgba8_dds(const std::filesystem::path& path, u32 width, u32 height, ArrayRef<u8> pixels) noexcept;
} // namespace aurora::gfx::dds

#pragma once

#include "common.hpp"
#include "texture_convert.hpp"

#include <filesystem>
#include <optional>

namespace aurora::gfx::dds {
std::optional<ConvertedTexture> parse_dds_bytes(ArrayRef<uint8_t> bytes) noexcept;
std::optional<ConvertedTexture> load_dds_file(const std::filesystem::path& path) noexcept;
ByteBuffer encode_rgba8_dds(uint32_t width, uint32_t height, ArrayRef<uint8_t> pixels);
bool write_rgba8_dds(const std::filesystem::path& path, uint32_t width, uint32_t height, ArrayRef<u8> pixels) noexcept;

std::optional<ByteBuffer> read_binary_file(const std::filesystem::path& path) noexcept;

} // namespace aurora::gfx::dds

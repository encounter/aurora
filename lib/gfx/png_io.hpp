#pragma once

#include <filesystem>
#include <optional>

#include "texture_convert.hpp"

namespace aurora::gfx::png {
std::optional<ConvertedTexture> parse_png_bytes(ArrayRef<uint8_t> bytes) noexcept;
std::optional<ConvertedTexture> load_png_file(const std::filesystem::path& path) noexcept;
} // namespace aurora::gfx::png

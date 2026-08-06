#pragma once

#include "texture.hpp"
#include <aurora/texture.hpp>
#include <optional>

namespace aurora::gfx::texture_replacement {
void initialize() noexcept;
void shutdown() noexcept;
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj) noexcept;
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept;
std::optional<TextureHandle> find_pointer_replacement(const GXTexObj_& obj) noexcept;
std::optional<TextureHandle> find_source_replacement(const GXTexObj_& obj,
                                                     const texture::TextureSourceKey& sourceKey) noexcept;
bool should_build_source_key() noexcept;
bool has_replacement(const GXTexObj_& obj) noexcept;
bool has_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept;
std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept;
std::string build_texture_replacement_name(const texture::TextureSourceKey& sourceKey) noexcept;
} // namespace aurora::gfx::texture_replacement

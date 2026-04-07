#pragma once

#include "texture.hpp"

namespace aurora::gfx::texture_replacement {
u32 compute_texture_upload_size(const GXTexObj_& obj) noexcept;
void initialize() noexcept;
void shutdown() noexcept;
void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, u16 entries) noexcept;
void load_tlut(const GXTlutObj* obj, u32 idx) noexcept;
bool try_bind_replacement(GXTexObj_& obj, GXTexMapID id) noexcept;
} // namespace aurora::gfx::texture_replacement

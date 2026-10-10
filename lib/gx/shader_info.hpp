#pragma once

#include "gx.hpp"

namespace aurora::gx {
ShaderInfo build_shader_info(const ShaderConfig& config) noexcept;
void validate_shader_config(const ShaderConfig& config, const ShaderInfo& info, u32 numTexGens) noexcept;
gfx::Range build_uniform(const ShaderInfo& info) noexcept;
u8 color_channel(GXChannelID id) noexcept;
}; // namespace aurora::gx

#pragma once

#include "gx.hpp"

namespace aurora::gfx::gx {
ShaderInfo build_shader_info(const ShaderConfig& config) noexcept;
Range build_uniform(const ShaderInfo& info) noexcept;
u8 color_channel(GXChannelID id) noexcept;
}; // namespace aurora::gfx::gx

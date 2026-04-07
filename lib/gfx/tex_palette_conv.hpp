#pragma once

#include "common.hpp"

namespace aurora::gfx::tex_palette_conv {

enum class Variant : uint8_t {
  Direct,     // R16Sint index texture -> TLUT -> RGBA8
  FromFloat8, // f32 texture, R channel * 255 -> 8-bit index -> TLUT -> RGBA8
  FromFloat4, // f32 texture, R channel * 15 -> 4-bit index -> TLUT -> RGBA8
};

struct ConvRequest {
  Variant variant;
  TextureHandle src;
  TextureHandle dst;
  TextureHandle tlut;
};

void initialize();
void shutdown();
void run(const wgpu::CommandEncoder& cmd, const ConvRequest& req);

} // namespace aurora::gfx::tex_palette_conv

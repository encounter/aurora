#pragma once

#include "common.hpp"

#include <dolphin/gx/GXEnum.h>

namespace aurora::gfx::tex_copy_conv {

struct ConvRequest {
  GXTexFmt fmt;
  TextureHandle src;
  TextureHandle dst;
};

/// Returns true if the given copy format requires a conversion draw
bool needs_conversion(GXTexFmt fmt);

void initialize();
void shutdown();
void queue(ConvRequest req);
void execute(const wgpu::CommandEncoder& cmd);

} // namespace aurora::gfx::tex_copy_conv

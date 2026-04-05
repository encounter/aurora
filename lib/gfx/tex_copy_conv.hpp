#pragma once

#include "common.hpp"

#include <dolphin/gx/GXEnum.h>

namespace aurora::gfx::tex_copy_conv {

struct ConvRequest {
  GXTexFmt fmt;
  wgpu::TextureView srcView; // View of resolved EFB / offscreen color
  Range uniformRange;        // UV transform uniform (offset + scale)
  TextureHandle dst;         // Destination texture
};

bool needs_conversion(GXTexFmt fmt);

void initialize();
void shutdown();
void run(const wgpu::CommandEncoder& cmd, const ConvRequest& req);
void blit(const wgpu::CommandEncoder& cmd, const ConvRequest& req);

} // namespace aurora::gfx::tex_copy_conv

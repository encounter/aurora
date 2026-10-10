#pragma once

#include <dolphin/gx/GXEnum.h>

#include <cstdint>

namespace aurora::gfx::copy_readback {

void initialize();
void shutdown();

bool is_supported_format(GXTexFmt fmt) noexcept;

// See GXAuroraReadback in GXAurora.h.
bool request(const void* src, void* dst, uint16_t width, uint16_t height, GXTexFmt fmt);

} // namespace aurora::gfx::copy_readback

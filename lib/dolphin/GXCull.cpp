#include "gx.hpp"

void GXSetScissor(u32 left, u32 top, u32 width, u32 height) { aurora::gfx::set_scissor(left, top, width, height); }

void GXSetCullMode(GXCullMode mode) { update_gx_state(g_gxState.cullMode, mode); }

// TODO GXSetCoPlanar

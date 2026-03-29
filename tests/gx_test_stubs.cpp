// Stub implementations for renderer symbols that the GX/FIFO/command_processor code
// references but that live in the full renderer (common.cpp, gx.cpp, model/shader.cpp, etc.).
// These allow the test binary to link without pulling in WebGPU runtime.

#include "gx/gx.hpp"
#include "gx/pipeline.hpp"
#include "gx/shader_info.hpp"
#include "gfx/texture.hpp"
#include "internal.hpp"

#include <cstdio>
#include <fmt/format.h>

// --- aurora::g_config ---
namespace aurora {
AuroraConfig g_config{};
} // namespace aurora

// --- aurora::log_internal ---
namespace aurora {
void log_internal(AuroraLogLevel level, const char* module, const char* message, unsigned int len) noexcept {
  fprintf(stderr, "[%d] %s: %.*s\n", static_cast<int>(level), module, len, message);
}
} // namespace aurora

// --- fmt::formatter<AuroraLogLevel> ---
auto fmt::formatter<AuroraLogLevel>::format(AuroraLogLevel level, format_context& ctx) const
    -> format_context::iterator {
  return fmt::format_to(ctx.out(), "{}", static_cast<int>(level));
}

// --- GPU buffers (default-constructed, not used in tests) ---
namespace aurora::gfx {
AuroraStats g_stats;
wgpu::Buffer g_vertexBuffer;
wgpu::Buffer g_uniformBuffer;
wgpu::Buffer g_indexBuffer;
wgpu::Buffer g_storageBuffer;
} // namespace aurora::gfx

// --- GXState (the real instance -- tests validate this) ---
namespace aurora::gx {
GXState g_gxState{};
} // namespace aurora::gx

// --- Texture uploads ---
namespace aurora::gfx {
std::vector<TextureUpload> g_textureUploads;
} // namespace aurora::gfx

// --- get_texture ---
namespace aurora::gx {
const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[id]; }
void shutdown() noexcept {}
} // namespace aurora::gx

// --- Shader/pipeline stubs ---
namespace aurora::gx {
void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept {
  // No-op for tests
}
GXBindGroups build_bind_groups(const ShaderInfo& info, const ShaderConfig& config,
                               const BindGroupRanges& ranges) noexcept {
  return {};
}
ShaderInfo build_shader_info(const ShaderConfig& config) noexcept { return {}; }
gfx::Range build_uniform(const ShaderInfo& info, u32 vtxStart) noexcept { return {}; }
u8 color_channel(GXChannelID id) noexcept { return 0; }
u8 comp_type_size(GXAttr attr, GXCompType type) noexcept { return 0; }
u8 comp_cnt_count(GXAttr attr, GXCompCnt cnt) noexcept { return 0; }
} // namespace aurora::gx

// --- Buffer push stubs ---
namespace aurora::gfx {
Range push_verts(const uint8_t* data, size_t length) { return {}; }
Range push_indices(const uint8_t* data, size_t length) { return {}; }
Range push_uniform(const uint8_t* data, size_t length) { return {}; }
Range push_storage(const uint8_t* data, size_t length) { return {}; }

const Viewport& get_viewport() noexcept {
  static Viewport vp{0.f, 0.f, 640.f, 480.f, 0.f, 1.f};
  return vp;
}
void set_viewport(float left, float top, float width, float height, float znear, float zfar) noexcept {}
void set_scissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) noexcept {}
} // namespace aurora::gfx

// --- Pipeline/draw command stubs ---
// These are template instantiations that command_processor.cpp needs
namespace aurora::gfx {
template <>
PipelineRef pipeline_ref<gx::PipelineConfig>(gx::PipelineConfig config) {
  return 0;
}
template <>
void push_draw_command<gx::DrawData>(gx::DrawData data) {
  // No-op
}
template <>
gx::DrawData* get_last_draw_command() {
  return nullptr;
}
} // namespace aurora::gfx

// --- TextureBind::get_descriptor ---
namespace aurora::gfx {
wgpu::SamplerDescriptor TextureBind::get_descriptor() const noexcept { return wgpu::SamplerDescriptor{}; }
} // namespace aurora::gfx

// --- Texture creation/write stubs ---
namespace aurora::gfx {
TextureHandle new_static_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 format, ArrayRef<uint8_t> data,
                                    bool tlut, const char* label) noexcept {
  return {};
}
TextureHandle new_dynamic_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 format,
                                     const char* label) noexcept {
  return {};
}
TextureHandle new_render_texture(uint32_t width, uint32_t height, u32 fmt, const char* label) noexcept { return {}; }
void write_texture(const TextureRef& ref, ArrayRef<uint8_t> data) noexcept {}
void resolve_pass(TextureHandle texture, ClipRect rect, bool clear, Vec4<float> clearColor) {}
} // namespace aurora::gfx

// --- Window stub ---
#include "../lib/window.hpp"
namespace aurora::window {
AuroraWindowSize get_window_size() { return {640, 480, 640, 480, 1.0f}; }
} // namespace aurora::window

// --- WebGPU C API stubs (prevent linker errors from wgpu:: destructors) ---
extern "C" {
void wgpuBufferRelease(WGPUBuffer) {}
void wgpuTextureRelease(WGPUTexture) {}
void wgpuTextureViewRelease(WGPUTextureView) {}
void wgpuSamplerRelease(WGPUSampler) {}
void wgpuShaderModuleRelease(WGPUShaderModule) {}
void wgpuRenderPipelineRelease(WGPURenderPipeline) {}
void wgpuBindGroupRelease(WGPUBindGroup) {}
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout) {}
void wgpuPipelineLayoutRelease(WGPUPipelineLayout) {}
void wgpuBufferAddRef(WGPUBuffer) {}
void wgpuTextureAddRef(WGPUTexture) {}
void wgpuTextureViewAddRef(WGPUTextureView) {}
}

void aurora::gfx::push_debug_group(std::string) {
}
void push_debug_group(const char*) {
}
void pop_debug_group() {
}
void aurora::gfx::insert_debug_marker(std::string) {
}
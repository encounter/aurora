// Stub implementations for renderer symbols that the GX/FIFO/command_processor code
// references but that live in the full renderer (common.cpp, gx.cpp, model/shader.cpp, etc.).
// These allow the test binary to link without pulling in WebGPU runtime.

#include "gx/gx.hpp"
#include "gfx/clear.hpp"
#include "gfx/common.hpp"
#include "gfx/depth_peek.hpp"
#include "gfx/tex_copy_conv.hpp"
#include "gfx/tex_palette_conv.hpp"
#include "gfx/texture.hpp"
#include "gx/pipeline.hpp"
#include "gx/shader_info.hpp"
#include "internal.hpp"
#include "webgpu/gpu.hpp"

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

namespace aurora::webgpu {
GraphicsConfig g_graphicsConfig{};
} // namespace aurora::webgpu

// --- GXState (the real instance -- tests validate this) ---
namespace aurora::gx {
GXState g_gxState{};
} // namespace aurora::gx

namespace aurora::vi {
Vec2<uint32_t> configured_fb_size() noexcept { return {640, 480}; }
void configure(const GXRenderModeObj*) noexcept {}
} // namespace aurora::vi

// --- Texture uploads ---
namespace aurora::gfx {
std::vector<TextureUpload> g_textureUploads;
} // namespace aurora::gfx

// --- get_texture ---
namespace aurora::gx {
const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[id]; }
void evict_texture_object(u32 texObjId) noexcept {
  for (auto& obj : g_gxState.loadedTextures) {
    if (obj.texObjId == texObjId) {
      obj.set_no_cache(true);
    }
  }
}
void evict_tlut_object(u32 tlutObjId) noexcept {
  for (auto& obj : g_gxState.loadedTluts) {
    if (obj.tlutObjId == tlutObjId) {
      obj.set_no_cache(true);
    }
  }
}
void evict_copy_texture(const void* dest) noexcept {
  g_gxState.copyTextures.erase(dest);
  for (auto it = g_gxState.copyTextureCache.begin(); it != g_gxState.copyTextureCache.end();) {
    if (it->first.dest == dest) {
      g_gxState.copyTextureCache.erase(it++);
    } else {
      ++it;
    }
  }
}
void shutdown() noexcept {}
Vec2<uint32_t> logical_fb_size() noexcept { return {640, 480}; }
gfx::Viewport map_logical_viewport(const gfx::Viewport& logicalViewport) noexcept { return logicalViewport; }
gfx::ClipRect map_logical_scissor(const gfx::ClipRect& logicalScissor) noexcept { return logicalScissor; }
void set_logical_viewport(const gfx::Viewport& viewport) noexcept {
  g_gxState.logicalViewport = viewport;
  set_render_viewport(map_logical_viewport(viewport));
}
void set_render_viewport(const gfx::Viewport& viewport) noexcept { g_gxState.renderViewport = viewport; }
void set_logical_scissor(const gfx::ClipRect& scissor) noexcept {
  g_gxState.logicalScissor = scissor;
  set_render_scissor(map_logical_scissor(scissor));
}
void set_render_scissor(const gfx::ClipRect& scissor) noexcept { g_gxState.renderScissor = scissor; }
} // namespace aurora::gx

// --- Shader/pipeline stubs ---
namespace aurora::gx {
void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept {
  // No-op for tests
}
GXBindGroups build_bind_groups(const ShaderInfo& info) noexcept { return {}; }
ShaderInfo build_shader_info(const ShaderConfig& config) noexcept { return {}; }
gfx::Range build_uniform(const ShaderInfo& info, uint32_t vtxStart, const BindGroupRanges& ranges) noexcept {
  return {};
}
void resolve_sampled_textures(const ShaderInfo& info) noexcept {}
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

Vec2<uint32_t> get_render_target_size() noexcept { return {640, 480}; }
void set_viewport(const Viewport& viewport) noexcept {}
void set_scissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) noexcept {}
} // namespace aurora::gfx

// --- Pipeline/draw command stubs ---
namespace aurora::gfx {
template <>
PipelineRef pipeline_ref<clear::PipelineConfig>(const clear::PipelineConfig& config) {
  return 0;
}
template <>
void push_draw_command<clear::DrawData>(clear::DrawData data) {
  // No-op
}
template <>
PipelineRef pipeline_ref<gx::PipelineConfig>(const gx::PipelineConfig& config) {
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

// --- Texture creation/write/replacement stubs ---
namespace aurora::gfx {
TextureHandle new_static_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                    ArrayRef<uint8_t> data, bool tlut, const char* label) noexcept {
  return {};
}
TextureHandle new_dynamic_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                     const char* label) noexcept {
  return {};
}
TextureHandle new_render_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept {
  return {};
}
TextureHandle new_conv_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept { return {}; }
void write_texture(const TextureRef& ref, ArrayRef<uint8_t> data) noexcept {}
void resolve_pass(TextureHandle texture, ClipRect rect, bool clearColor, bool clearAlpha, bool clearDepth,
                  Vec4<float> clearColorValue, float clearDepthValue, GXTexFmt resolveFormat) {}
void queue_palette_conv(tex_palette_conv::ConvRequest req) {}
void begin_offscreen(uint32_t width, uint32_t height) {}
void end_offscreen() {}
bool is_offscreen() noexcept { return false; }
} // namespace aurora::gfx

namespace aurora::gfx::depth_peek {
namespace {
bool s_snapshotRequested = false;
uint32_t s_width = 0;
uint32_t s_height = 0;
std::vector<uint32_t> s_data;
} // namespace

void initialize() {}
void shutdown() {}
void request_snapshot() noexcept { s_snapshotRequested = true; }
void poll() noexcept {}
void encode_frame_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                           wgpu::Extent3D sourceSize, uint32_t msaaSamples) noexcept {}
void after_submit() noexcept {}

bool read_latest(uint16_t x, uint16_t y, uint32_t& z) noexcept {
  if (x >= s_width || y >= s_height || s_data.empty()) {
    return false;
  }
  z = s_data[static_cast<size_t>(y) * s_width + x] & 0x00ffffffu;
  return true;
}

namespace testing {
void reset() noexcept {
  s_snapshotRequested = false;
  s_width = 0;
  s_height = 0;
  s_data.clear();
}

bool snapshot_requested() noexcept { return s_snapshotRequested; }

void set_latest(uint32_t width, uint32_t height, const std::vector<uint32_t>& data) {
  s_width = width;
  s_height = height;
  s_data = data;
}
} // namespace testing
} // namespace aurora::gfx::depth_peek

namespace aurora::gfx::tex_copy_conv {
bool needs_conversion(GXTexFmt fmt) { return false; }
} // namespace aurora::gfx::tex_copy_conv

namespace aurora::gfx::tex_palette_conv {
void queue(ConvRequest req) {}
} // namespace aurora::gfx::tex_palette_conv

namespace aurora::gfx::texture_replacement {
u32 compute_texture_upload_size(const GXTexObj_& obj) noexcept { return 0; }
void register_tlut(const GXTlutObj*, const void*, GXTlutFmt, u16) noexcept {}
void load_tlut(const GXTlutObj*, u32) noexcept {}
std::optional<TextureHandle> find_replacement(const GXTexObj_&) noexcept { return std::nullopt; }
bool try_bind_replacement(GXTexObj_&, GXTexMapID) noexcept { return false; }
} // namespace aurora::gfx::texture_replacement

// --- Window stub ---
#include "../lib/window.hpp"
namespace aurora::window {
AuroraWindowSize get_window_size() { return {640, 480, 640, 480, 640, 480, 1.0f}; }
void set_frame_buffer_aspect_fit(bool) {}
} // namespace aurora::window

// --- WebGPU C API stubs (prevent linker errors from wgpu:: destructors) ---
extern "C" {
void wgpuDeviceRelease(WGPUDevice) {}
void wgpuQueueRelease(WGPUQueue) {}
void wgpuSurfaceRelease(WGPUSurface) {}
void wgpuBufferRelease(WGPUBuffer) {}
void wgpuTextureRelease(WGPUTexture) {}
void wgpuTextureViewRelease(WGPUTextureView) {}
void wgpuSamplerRelease(WGPUSampler) {}
void wgpuShaderModuleRelease(WGPUShaderModule) {}
void wgpuRenderPipelineRelease(WGPURenderPipeline) {}
void wgpuBindGroupRelease(WGPUBindGroup) {}
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout) {}
void wgpuPipelineLayoutRelease(WGPUPipelineLayout) {}
void wgpuInstanceRelease(WGPUInstance) {}
void wgpuDeviceAddRef(WGPUDevice) {}
void wgpuQueueAddRef(WGPUQueue) {}
void wgpuSurfaceAddRef(WGPUSurface) {}
void wgpuBufferAddRef(WGPUBuffer) {}
void wgpuTextureAddRef(WGPUTexture) {}
void wgpuTextureViewAddRef(WGPUTextureView) {}
void wgpuInstanceAddRef(WGPUInstance) {}
}

void aurora::gfx::push_debug_group(std::string) {}
void push_debug_group(const char*) {}
void pop_debug_group() {}
void aurora::gfx::insert_debug_marker(std::string) {}

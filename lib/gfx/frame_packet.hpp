#pragma once

#include "pipeline_cache.hpp"
#include "types.hpp"
#include "tex_palette_conv.hpp"
#include "texture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace aurora::gfx::detail {

struct StagingHighWater {
  uint32_t verts = 0;
  uint32_t uniforms = 0;
  uint32_t indices = 0;
  uint32_t storage = 0;
  uint32_t textureUpload = 0;
  size_t textureUploadCount = 0;
};

struct CustomDrawCommand {
  DrawTypeId type = 0;
  uint32_t payloadSize = 0;
  alignas(std::max_align_t) std::array<std::byte, InlineDrawPayloadSize> payload{};
};

struct RenderPass;
using DrawEncoder = void (*)(void* payload, const wgpu::RenderPassEncoder& pass, const RenderPass& passInfo);

struct DrawCommand {
  DrawEncoder encoder = nullptr;
  alignas(std::max_align_t) std::array<std::byte, InlineDrawPayloadSize> payload{};
};

enum class CommandType {
  SetViewport,
  SetScissor,
  Draw,
  CustomDraw,
  DebugMarker,
};

struct Command {
  CommandType type;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> debugGroupStack;
#endif
  union Data {
    Viewport setViewport;
    ClipRect setScissor;
    DrawCommand draw;
    CustomDrawCommand customDraw;
    size_t debugMarkerIndex;
  } data;
};

using CommandList = std::vector<Command>;

struct RenderPass {
  std::string label;
  wgpu::TextureView colorView;
  wgpu::TextureView resolveView;
  wgpu::TextureView depthStencilView;
  wgpu::Texture copySourceTexture;
  wgpu::TextureView copySourceView;
  wgpu::TextureView copySourceDepthView;
  wgpu::Extent3D targetSize;
  uint32_t msaaSamples = 1;

  TextureHandle resolveTarget;
  GXTexFmt resolveFormat = GX_TF_RGBA8;
  ClipRect resolveRect;
  Range resolveUniformRange;
  wgpu::Texture snapshotColorDst;
  wgpu::TextureView snapshotDepthDst;
  Vec4<float> clearColorValue{0.f, 0.f, 0.f, 0.f};
  float clearDepthValue = 1.f;
  wgpu::LoadOp colorLoadOp = wgpu::LoadOp::Undefined;
  wgpu::StoreOp colorStoreOp = wgpu::StoreOp::Store;
  wgpu::LoadOp depthLoadOp = wgpu::LoadOp::Undefined;
  wgpu::StoreOp depthStoreOp = wgpu::StoreOp::Store;
  wgpu::LoadOp stencilLoadOp = wgpu::LoadOp::Undefined;
  wgpu::StoreOp stencilStoreOp = wgpu::StoreOp::Undefined;
  uint32_t stencilClearValue = 0;
  CommandList commands;
  bool clearColor = true;
  bool clearDepth = true;
  bool hasDepth = true;
  bool hasStencil = false;
  bool hasDraws = false;
  bool discardable = false;
  bool captureDepthSnapshot = false;
  bool sealed = false;
  std::vector<tex_palette_conv::ConvRequest> paletteConvs;

  bool has_consumer() const { return resolveTarget || snapshotColorDst || snapshotDepthDst; }
  bool has_content() const { return hasDraws || clearColor || clearDepth; }
};

struct TextureCopy {
  wgpu::TexelCopyTextureInfo src;
  wgpu::TexelCopyTextureInfo dst;
  wgpu::Extent3D size;
};

struct EncoderTask {
  EncoderTaskId type = InvalidEncoderTask;
  std::array<uint8_t, InlineDrawPayloadSize> payload{};
  uint32_t payloadSize = 0;
};

enum class FrameOpType : uint8_t {
  RenderPass,
  TextureCopy,
  EncoderTask,
};

struct FrameOp {
  FrameOpType type = FrameOpType::RenderPass;
  uint32_t index = 0;
  RenderPass* renderPass = nullptr;
  TextureCopy* textureCopy = nullptr;
  EncoderTask* encoderTask = nullptr;
  StagingHighWater highWater;
  std::vector<const TextureUpload*> textureUploads;
};

using RenderPassList = std::deque<RenderPass>;

struct FramePacket {
  RenderPassList renderPasses;
  std::deque<TextureCopy> textureCopies;
  std::deque<EncoderTask> encoderTasks;
  std::deque<FrameOp> ops;
  std::deque<TextureUpload> textureUploads;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> debugMarkers;
#endif
  ByteBuffer verts;
  ByteBuffer uniforms;
  ByteBuffer indices;
  ByteBuffer storage;
  ByteBuffer textureUpload;
  wgpu::CommandEncoder encoder;
  std::vector<AfterSubmitCallback> afterSubmitCallbacks;
  uint64_t frameId = 0;
  uint32_t frameIndex = 0;
  size_t stagingBuffer = 0;
  StagingHighWater copied;
  AuroraStats stats{};
};

} // namespace aurora::gfx::detail

#pragma once
#include <dolphin/gx.h>

#include "common.hpp"

namespace aurora::gfx {
struct TextureUpload {
  WGPUTextureDataLayout layout;
  WGPUImageCopyTexture tex;
  WGPUExtent3D size;

  TextureUpload(WGPUTextureDataLayout layout, WGPUImageCopyTexture tex, WGPUExtent3D size) noexcept
  : layout(layout), tex(tex), size(size) {}
};
extern std::vector<TextureUpload> g_textureUploads;

constexpr u32 InvalidTextureFormat = -1;
struct TextureRef {
  WGPUTexture texture;
  WGPUTextureView view;
  WGPUExtent3D size;
  WGPUTextureFormat format;
  uint32_t mipCount;
  u32 gxFormat;
  bool isRenderTexture; // :shrug: for now

  TextureRef(WGPUTexture texture, WGPUTextureView view, WGPUExtent3D size, WGPUTextureFormat format, uint32_t mipCount,
             u32 gxFormat, bool isRenderTexture)
  : texture(texture)
  , view(view)
  , size(size)
  , format(format)
  , mipCount(mipCount)
  , gxFormat(gxFormat)
  , isRenderTexture(isRenderTexture) {}

  ~TextureRef() {
    wgpuTextureViewRelease(view);
    wgpuTextureDestroy(texture);
  }
};

using TextureHandle = std::shared_ptr<TextureRef>;

TextureHandle new_static_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 format,
                                    ArrayRef<uint8_t> data, const char* label) noexcept;
TextureHandle new_dynamic_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 format,
                                     const char* label) noexcept;
TextureHandle new_render_texture(uint32_t width, uint32_t height, u32 fmt, const char* label) noexcept;
void write_texture(const TextureRef& ref, ArrayRef<uint8_t> data) noexcept;
}; // namespace aurora::gfx

struct GXTexObj_ {
  aurora::gfx::TextureHandle ref;
  const void* data;
  u32 dataSize;
  u16 width;
  u16 height;
  u32 fmt;
  GXTexWrapMode wrapS;
  GXTexWrapMode wrapT;
  GXBool hasMips;
  GXTexFilter minFilter;
  GXTexFilter magFilter;
  float minLod;
  float maxLod;
  float lodBias;
  GXBool biasClamp;
  GXBool doEdgeLod;
  GXAnisotropy maxAniso;
  GXTlut tlut;
  bool dataInvalidated;
};
static_assert(sizeof(GXTexObj_) <= sizeof(GXTexObj), "GXTexObj too small!");
struct GXTlutObj_ {
  aurora::gfx::TextureHandle ref;
};
static_assert(sizeof(GXTlutObj_) <= sizeof(GXTlutObj), "GXTlutObj too small!");

namespace aurora::gfx {
struct TextureBind {
  GXTexObj_ texObj;

  TextureBind() noexcept = default;
  TextureBind(GXTexObj_ obj) noexcept : texObj(std::move(obj)) {}
  void reset() noexcept { texObj.ref.reset(); };
  [[nodiscard]] WGPUSamplerDescriptor get_descriptor() const noexcept;
  operator bool() const noexcept { return texObj.ref.operator bool(); }
};
} // namespace aurora::gfx

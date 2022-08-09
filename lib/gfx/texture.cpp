#include "common.hpp"

#include "../webgpu/gpu.hpp"
#include "../internal.hpp"
#include "texture.hpp"
#include "texture_convert.hpp"

#include <magic_enum.hpp>

namespace aurora::gfx {
static Module Log("aurora::gfx");

using webgpu::g_device;
using webgpu::g_queue;

struct TextureFormatInfo {
  uint8_t blockWidth;
  uint8_t blockHeight;
  uint8_t blockSize;
  bool compressed;
};
static TextureFormatInfo format_info(wgpu::TextureFormat format) {
  switch (format) {
    DEFAULT_FATAL("unimplemented texture format {}", magic_enum::enum_name(format));
  case wgpu::TextureFormat::R8Unorm:
    return {1, 1, 1, false};
  case wgpu::TextureFormat::R16Sint:
    return {1, 1, 2, false};
  case wgpu::TextureFormat::RGBA8Unorm:
  case wgpu::TextureFormat::R32Float:
    return {1, 1, 4, false};
  case wgpu::TextureFormat::BC1RGBAUnorm:
    return {4, 4, 8, true};
  }
}
static wgpu::Extent3D physical_size(wgpu::Extent3D size, TextureFormatInfo info) {
  const uint32_t width = ((size.width + info.blockWidth - 1) / info.blockWidth) * info.blockWidth;
  const uint32_t height = ((size.height + info.blockHeight - 1) / info.blockHeight) * info.blockHeight;
  return {width, height, size.depthOrArrayLayers};
}

TextureHandle new_static_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 format, ArrayRef<uint8_t> data,
                                    const char* label) noexcept {
  auto handle = new_dynamic_texture_2d(width, height, mips, format, label);
  const auto& ref = *handle;

  ByteBuffer buffer;
  if (ref.gxFormat != InvalidTextureFormat) {
    buffer = convert_texture(ref.gxFormat, ref.size.width, ref.size.height, ref.mipCount, data);
    if (!buffer.empty()) {
      data = {buffer.data(), buffer.size()};
    }
  }

  uint32_t offset = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const wgpu::Extent3D mipSize{
        .width = std::max(ref.size.width >> mip, 1u),
        .height = std::max(ref.size.height >> mip, 1u),
        .depthOrArrayLayers = ref.size.depthOrArrayLayers,
    };
    const auto info = format_info(ref.format);
    const auto physicalSize = physical_size(mipSize, info);
    const uint32_t widthBlocks = physicalSize.width / info.blockWidth;
    const uint32_t heightBlocks = physicalSize.height / info.blockHeight;
    const uint32_t bytesPerRow = widthBlocks * info.blockSize;
    const uint32_t dataSize = bytesPerRow * heightBlocks * mipSize.depthOrArrayLayers;
    CHECK(offset + dataSize <= data.size(), "new_static_texture_2d[{}]: expected at least {} bytes, got {}", label,
          offset + dataSize, data.size());
    const wgpu::ImageCopyTexture dstView{
        .texture = ref.texture,
        .mipLevel = mip,
    };
    //    const auto range = push_texture_data(data.data() + offset, dataSize, bytesPerRow, heightBlocks);
    const wgpu::TextureDataLayout dataLayout{
        //        .offset = range.offset,
        .bytesPerRow = bytesPerRow,
        .rowsPerImage = heightBlocks,
    };
    // TODO
    //    g_textureUploads.emplace_back(dataLayout, std::move(dstView), physicalSize);
    g_queue.WriteTexture(&dstView, data.data() + offset, dataSize, &dataLayout, &physicalSize);
    offset += dataSize;
  }
  if (data.size() != UINT32_MAX && offset < data.size()) {
    Log.report(LOG_WARNING, FMT_STRING("new_static_texture_2d[{}]: texture used {} bytes, but given {} bytes"), label,
               offset, data.size());
  }
  return handle;
}

TextureHandle new_dynamic_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 format,
                                     const char* label) noexcept {
  const auto wgpuFormat = to_wgpu(format);
  const wgpu::Extent3D size{
      .width = width,
      .height = height,
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDescriptor{
      .label = label,
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = wgpuFormat,
      .mipLevelCount = mips,
      .sampleCount = 1,
  };
  const auto viewLabel = fmt::format(FMT_STRING("{} view"), label);
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = viewLabel.c_str(),
      .format = wgpuFormat,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = mips,
      .arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);
  auto textureView = texture.CreateView(&textureViewDescriptor);
  return std::make_shared<TextureRef>(std::move(texture), std::move(textureView), size, wgpuFormat, mips, format,
                                      false);
}

TextureHandle new_render_texture(uint32_t width, uint32_t height, u32 fmt, const char* label) noexcept {
  const auto wgpuFormat = webgpu::g_graphicsConfig.swapChainDescriptor.format;
  const wgpu::Extent3D size{
      .width = width,
      .height = height,
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDescriptor{
      .label = label,
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = wgpuFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  const auto viewLabel = fmt::format(FMT_STRING("{} view"), label);
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = viewLabel.c_str(),
      .format = wgpuFormat,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED,
      .arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);
  auto textureView = texture.CreateView(&textureViewDescriptor);
  return std::make_shared<TextureRef>(std::move(texture), std::move(textureView), size, wgpuFormat, 1, fmt, true);
}

void write_texture(const TextureRef& ref, ArrayRef<uint8_t> data) noexcept {
  ByteBuffer buffer;
  if (ref.gxFormat != InvalidTextureFormat) {
    buffer = convert_texture(ref.gxFormat, ref.size.width, ref.size.height, ref.mipCount, data);
    if (!buffer.empty()) {
      data = {buffer.data(), buffer.size()};
    }
  }

  uint32_t offset = 0;
  for (uint32_t mip = 0; mip < ref.mipCount; ++mip) {
    const wgpu::Extent3D mipSize{
        .width = std::max(ref.size.width >> mip, 1u),
        .height = std::max(ref.size.height >> mip, 1u),
        .depthOrArrayLayers = ref.size.depthOrArrayLayers,
    };
    const auto info = format_info(ref.format);
    const auto physicalSize = physical_size(mipSize, info);
    const uint32_t widthBlocks = physicalSize.width / info.blockWidth;
    const uint32_t heightBlocks = physicalSize.height / info.blockHeight;
    const uint32_t bytesPerRow = widthBlocks * info.blockSize;
    const uint32_t dataSize = bytesPerRow * heightBlocks * mipSize.depthOrArrayLayers;
    CHECK(offset + dataSize <= data.size(), "write_texture: expected at least {} bytes, got {}", offset + dataSize,
          data.size());
    //    auto dstView = wgpu::ImageCopyTexture{
    //        .texture = ref.texture,
    //        .mipLevel = mip,
    //    };
    //    const auto range = push_texture_data(data.data() + offset, dataSize, bytesPerRow, heightBlocks);
    //    const auto dataLayout = wgpu::TextureDataLayout{
    //        .offset = range.offset,
    //        .bytesPerRow = bytesPerRow,
    //        .rowsPerImage = heightBlocks,
    //    };
    //    g_textureUploads.emplace_back(dataLayout, std::move(dstView), physicalSize);
    const wgpu::ImageCopyTexture dstView{
        .texture = ref.texture,
        .mipLevel = mip,
    };
    const wgpu::TextureDataLayout dataLayout{
        .bytesPerRow = bytesPerRow,
        .rowsPerImage = heightBlocks,
    };
    g_queue.WriteTexture(&dstView, data.data() + offset, dataSize, &dataLayout, &physicalSize);
    offset += dataSize;
  }
  if (data.size() != UINT32_MAX && offset < data.size()) {
    Log.report(LOG_WARNING, FMT_STRING("write_texture: texture used {} bytes, but given {} bytes"), offset,
               data.size());
  }
}
} // namespace aurora::gfx

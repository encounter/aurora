#include "shader.hpp"

#include "../../webgpu/gpu.hpp"
#include "../gx_fmt.hpp"

#include <absl/container/flat_hash_map.h>

namespace aurora::gfx::model {
static Module Log("aurora::gfx::model");

using IndexedAttrs = std::array<bool, GX_VA_MAX_ATTR>;
struct DisplayListCache {
  ByteBuffer vtxBuf;
  ByteBuffer idxBuf;
  IndexedAttrs indexedAttrs;
  GXVtxFmt fmt;

  DisplayListCache(ByteBuffer&& vtxBuf, ByteBuffer&& idxBuf, IndexedAttrs indexedAttrs, GXVtxFmt fmt)
  : vtxBuf(std::move(vtxBuf)), idxBuf(std::move(idxBuf)), indexedAttrs(indexedAttrs), fmt(fmt) {}
};

static absl::flat_hash_map<HashType, DisplayListCache> sCachedDisplayLists;

static u32 prepare_vtx_buffer(ByteBuffer& buf, GXVtxFmt vtxfmt, const u8* ptr, u16 vtxCount,
                              IndexedAttrs& indexedAttrs) {
  using gx::g_gxState;
  struct {
    u8 count;
    GXCompType type;
  } attrArrays[GX_VA_MAX_ATTR] = {};
  u32 vtxSize = 0;
  u32 outVtxSize = 0;

  // Calculate attribute offsets and vertex size
  for (int attr = 0; attr < GX_VA_MAX_ATTR; attr++) {
    const auto& attrFmt = g_gxState.vtxFmts[vtxfmt].attrs[attr];
    switch (g_gxState.vtxDesc[attr]) {
      DEFAULT_FATAL("unhandled attribute type {}", g_gxState.vtxDesc[attr]);
    case GX_NONE:
      break;
    case GX_DIRECT:
#define COMBINE(val1, val2, val3) (((val1) << 16) | ((val2) << 8) | (val3))
      switch (COMBINE(attr, attrFmt.cnt, attrFmt.type)) {
        DEFAULT_FATAL("not handled: attr {}, cnt {}, type {}", attr, attrFmt.cnt, attrFmt.type);
      case COMBINE(GX_VA_POS, GX_POS_XYZ, GX_F32):
      case COMBINE(GX_VA_NRM, GX_NRM_XYZ, GX_F32):
        attrArrays[attr].count = 3;
        attrArrays[attr].type = GX_F32;
        vtxSize += 12;
        outVtxSize += 12;
        break;
      case COMBINE(GX_VA_POS, GX_POS_XYZ, GX_S16):
      case COMBINE(GX_VA_NRM, GX_NRM_XYZ, GX_S16):
        attrArrays[attr].count = 3;
        attrArrays[attr].type = GX_S16;
        vtxSize += 6;
        outVtxSize += 12;
        break;
      case COMBINE(GX_VA_TEX0, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX1, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX2, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX3, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX4, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX5, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX6, GX_TEX_ST, GX_F32):
      case COMBINE(GX_VA_TEX7, GX_TEX_ST, GX_F32):
        attrArrays[attr].count = 2;
        attrArrays[attr].type = GX_F32;
        vtxSize += 8;
        outVtxSize += 8;
        break;
      case COMBINE(GX_VA_TEX0, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX1, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX2, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX3, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX4, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX5, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX6, GX_TEX_ST, GX_S16):
      case COMBINE(GX_VA_TEX7, GX_TEX_ST, GX_S16):
        attrArrays[attr].count = 2;
        attrArrays[attr].type = GX_S16;
        vtxSize += 4;
        outVtxSize += 8;
        break;
      case COMBINE(GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8):
      case COMBINE(GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8):
        attrArrays[attr].count = 4;
        attrArrays[attr].type = GX_RGBA8;
        vtxSize += 4;
        outVtxSize += 16;
        break;
      }
#undef COMBINE
      break;
    case GX_INDEX8:
      ++vtxSize;
      outVtxSize += 2;
      indexedAttrs[attr] = true;
      break;
    case GX_INDEX16:
      vtxSize += 2;
      outVtxSize += 2;
      indexedAttrs[attr] = true;
      break;
    }
  }
  // Align to 4
  int rem = outVtxSize % 4;
  int padding = 0;
  if (rem != 0) {
    padding = 4 - rem;
    outVtxSize += padding;
  }

  // Build vertex buffer
  buf.reserve_extra(vtxCount * outVtxSize);
  std::array<f32, 4> out{};
  for (u32 v = 0; v < vtxCount; ++v) {
    for (int attr = 0; attr < GX_VA_MAX_ATTR; attr++) {
      if (g_gxState.vtxDesc[attr] == GX_INDEX8) {
        buf.append(static_cast<u16>(*ptr));
        ++ptr;
      } else if (g_gxState.vtxDesc[attr] == GX_INDEX16) {
        buf.append(bswap(*reinterpret_cast<const u16*>(ptr)));
        ptr += 2;
      }
      if (g_gxState.vtxDesc[attr] != GX_DIRECT) {
        continue;
      }
      const auto& attrFmt = g_gxState.vtxFmts[vtxfmt].attrs[attr];
      u8 count = attrArrays[attr].count;
      switch (attrArrays[attr].type) {
      case GX_U8:
        for (int i = 0; i < count; ++i) {
          const auto value = reinterpret_cast<const u8*>(ptr)[i];
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count;
        break;
      case GX_S8:
        for (int i = 0; i < count; ++i) {
          const auto value = reinterpret_cast<const s8*>(ptr)[i];
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count;
        break;
      case GX_U16:
        for (int i = 0; i < count; ++i) {
          const auto value = bswap(reinterpret_cast<const u16*>(ptr)[i]);
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(u16);
        break;
      case GX_S16:
        for (int i = 0; i < count; ++i) {
          const auto value = bswap(reinterpret_cast<const s16*>(ptr)[i]);
          out[i] = static_cast<f32>(value) / static_cast<f32>(1 << attrFmt.frac);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(s16);
        break;
      case GX_F32:
        for (int i = 0; i < count; ++i) {
          out[i] = bswap(reinterpret_cast<const f32*>(ptr)[i]);
        }
        buf.append(out.data(), sizeof(f32) * count);
        ptr += count * sizeof(f32);
        break;
      case GX_RGBA8:
        out[0] = static_cast<f32>(ptr[0]) / 255.f;
        out[1] = static_cast<f32>(ptr[1]) / 255.f;
        out[2] = static_cast<f32>(ptr[2]) / 255.f;
        out[3] = static_cast<f32>(ptr[3]) / 255.f;
        buf.append(out.data(), sizeof(f32) * 4);
        ptr += sizeof(u32);
        break;
      }
    }
    if (padding > 0) {
      buf.append_zeroes(padding);
    }
  }

  return vtxSize;
}

static u16 prepare_idx_buffer(ByteBuffer& buf, GXPrimitive prim, u16 vtxStart, u16 vtxCount) {
  u16 numIndices = 0;
  if (prim == GX_TRIANGLES) {
    buf.reserve_extra(vtxCount * sizeof(u16));
    for (u16 v = 0; v < vtxCount; ++v) {
      const u16 idx = vtxStart + v;
      buf.append(idx);
      ++numIndices;
    }
  } else if (prim == GX_TRIANGLEFAN) {
    buf.reserve_extra(((u32(vtxCount) - 3) * 3 + 3) * sizeof(u16));
    for (u16 v = 0; v < vtxCount; ++v) {
      const u16 idx = vtxStart + v;
      if (v < 3) {
        buf.append(idx);
        ++numIndices;
        continue;
      }
      buf.append(std::array{vtxStart, static_cast<u16>(idx - 1), idx});
      numIndices += 3;
    }
  } else if (prim == GX_TRIANGLESTRIP) {
    buf.reserve_extra(((static_cast<u32>(vtxCount) - 3) * 3 + 3) * sizeof(u16));
    for (u16 v = 0; v < vtxCount; ++v) {
      const u16 idx = vtxStart + v;
      if (v < 3) {
        buf.append(idx);
        ++numIndices;
        continue;
      }
      if ((v & 1) == 0) {
        buf.append(std::array{static_cast<u16>(idx - 2), static_cast<u16>(idx - 1), idx});
      } else {
        buf.append(std::array{static_cast<u16>(idx - 1), static_cast<u16>(idx - 2), idx});
      }
      numIndices += 3;
    }
  } else
    UNLIKELY FATAL("unsupported primitive type {}", static_cast<u32>(prim));
  return numIndices;
}

void queue_surface(const u8* dlStart, u32 dlSize) noexcept {
  const auto hash = xxh3_hash_s(dlStart, dlSize, 0);
  Range vertRange, idxRange;
  u32 numIndices = 0;
  IndexedAttrs indexedAttrs{};
  GXVtxFmt fmt = GX_MAX_VTXFMT;
  auto it = sCachedDisplayLists.find(hash);
  if (it != sCachedDisplayLists.end()) {
    const auto& cache = it->second;
    numIndices = cache.idxBuf.size() / 2;
    vertRange = push_verts(cache.vtxBuf.data(), cache.vtxBuf.size());
    idxRange = push_indices(cache.idxBuf.data(), cache.idxBuf.size());
    indexedAttrs = cache.indexedAttrs;
    fmt = cache.fmt;
  } else {
    const u8* data = dlStart;
    u32 pos = 0;
    ByteBuffer vtxBuf;
    ByteBuffer idxBuf;
    u16 vtxStart = 0;

    while (pos < dlSize) {
      u8 cmd = data[pos++];

      u8 opcode = cmd & GX_OPCODE_MASK;
      switch (opcode) {
        DEFAULT_FATAL("unimplemented opcode: {}", opcode);
      case GX_NOP:
        continue;
      case GX_LOAD_BP_REG:
        // TODO?
        pos += 4;
        break;
      case GX_DRAW_QUADS:
      case GX_DRAW_TRIANGLES:
      case GX_DRAW_TRIANGLE_STRIP:
      case GX_DRAW_TRIANGLE_FAN: {
        const auto prim = static_cast<GXPrimitive>(opcode);
        const auto newFmt = static_cast<GXVtxFmt>(cmd & GX_VAT_MASK);
        if (fmt != GX_MAX_VTXFMT && fmt != newFmt) {
          FATAL("Vertex format changed mid-display list: {} -> {}", fmt, newFmt);
        }
        fmt = newFmt;
        u16 vtxCount = bswap(*reinterpret_cast<const u16*>(data + pos));
        pos += 2;
        pos += vtxCount * prepare_vtx_buffer(vtxBuf, fmt, data + pos, vtxCount, indexedAttrs);
        numIndices += prepare_idx_buffer(idxBuf, prim, vtxStart, vtxCount);
        vtxStart += vtxCount;
        break;
      }
      case GX_DRAW_LINES:
      case GX_DRAW_LINE_STRIP:
      case GX_DRAW_POINTS:
        FATAL("unimplemented prim type: {}", opcode);
        break;
      }
    }
    vertRange = push_verts(vtxBuf.data(), vtxBuf.size());
    idxRange = push_indices(idxBuf.data(), idxBuf.size());
    sCachedDisplayLists.try_emplace(hash, std::move(vtxBuf), std::move(idxBuf), indexedAttrs, fmt);
  }

  gx::BindGroupRanges ranges{};
  for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
    if (!indexedAttrs[i]) {
      continue;
    }
    auto& array = gx::g_gxState.arrays[i];
    if (array.cachedRange.size > 0) {
      // Use the currently cached range
      ranges.vaRanges[i] = array.cachedRange;
    } else {
      // Push array data to storage and cache range
      const auto range = push_storage(static_cast<const uint8_t*>(array.data), array.size);
      ranges.vaRanges[i] = range;
      array.cachedRange = range;
    }
  }

  model::PipelineConfig config{};
  populate_pipeline_config(config, GX_TRIANGLES, fmt);
  const auto info = gx::build_shader_info(config.shaderConfig);
  const auto bindGroups = gx::build_bind_groups(info, config.shaderConfig, ranges);
  const auto pipeline = pipeline_ref(config);

  push_draw_command(model::DrawData{
      .pipeline = pipeline,
      .vertRange = vertRange,
      .idxRange = idxRange,
      .dataRanges = ranges,
      .uniformRange = build_uniform(info),
      .indexCount = numIndices,
      .bindGroups = bindGroups,
      .dstAlpha = gx::g_gxState.dstAlpha,
  });
}

State construct_state() { return {}; }

wgpu::RenderPipeline create_pipeline(const State& state, const PipelineConfig& config) {
  const auto info = build_shader_info(config.shaderConfig); // TODO remove
  const auto shader = build_shader(config.shaderConfig, info);

  std::array<wgpu::VertexAttribute, gx::MaxVtxAttr> vtxAttrs{};
  auto [num4xAttr, rem] = std::div(config.shaderConfig.indexedAttributeCount, 4);
  u32 num2xAttr = 0;
  if (rem > 2) {
    ++num4xAttr;
  } else if (rem > 0) {
    ++num2xAttr;
  }

  u32 offset = 0;
  u32 shaderLocation = 0;

  // Indexed attributes
  for (u32 i = 0; i < num4xAttr; ++i) {
    vtxAttrs[shaderLocation] = {
        .format = wgpu::VertexFormat::Uint16x4,
        .offset = offset,
        .shaderLocation = shaderLocation,
    };
    offset += 8;
    ++shaderLocation;
  }
  for (u32 i = 0; i < num2xAttr; ++i) {
    vtxAttrs[shaderLocation] = {
        .format = wgpu::VertexFormat::Uint16x2,
        .offset = offset,
        .shaderLocation = shaderLocation,
    };
    offset += 4;
    ++shaderLocation;
  }

  // Direct attributes
  for (int i = 0; i < gx::MaxVtxAttr; ++i) {
    const auto attrType = config.shaderConfig.vtxAttrs[i];
    if (attrType != GX_DIRECT) {
      continue;
    }
    const auto attr = static_cast<GXAttr>(i);
    switch (attr) {
      DEFAULT_FATAL("unhandled direct attr {}", i);
    case GX_VA_POS:
    case GX_VA_NRM:
      vtxAttrs[shaderLocation] = wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x3,
          .offset = offset,
          .shaderLocation = shaderLocation,
      };
      offset += 12;
      break;
    case GX_VA_CLR0:
    case GX_VA_CLR1:
      vtxAttrs[shaderLocation] = wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x4,
          .offset = offset,
          .shaderLocation = shaderLocation,
      };
      offset += 16;
      break;
    case GX_VA_TEX0:
    case GX_VA_TEX1:
    case GX_VA_TEX2:
    case GX_VA_TEX3:
    case GX_VA_TEX4:
    case GX_VA_TEX5:
    case GX_VA_TEX6:
    case GX_VA_TEX7:
      vtxAttrs[shaderLocation] = wgpu::VertexAttribute{
          .format = wgpu::VertexFormat::Float32x2,
          .offset = offset,
          .shaderLocation = shaderLocation,
      };
      offset += 8;
      break;
    }
    ++shaderLocation;
  }

  const std::array vtxBuffers{wgpu::VertexBufferLayout{
      .stepMode = wgpu::VertexStepMode::Vertex,
      .arrayStride = offset,
      .attributeCount = shaderLocation,
      .attributes = vtxAttrs.data(),
  }};

  return build_pipeline(config, info, vtxBuffers, shader, "GX Pipeline");
}

void render(const State& state, const DrawData& data, const wgpu::RenderPassEncoder& pass) {
  if (!bind_pipeline(data.pipeline, pass)) {
    return;
  }

  std::array<uint32_t, GX_VA_MAX_ATTR + 1> offsets{data.uniformRange.offset};
  uint32_t bindIdx = 1;
  for (uint32_t i = 0; i < GX_VA_MAX_ATTR; ++i) {
    const auto& range = data.dataRanges.vaRanges[i];
    if (range.size <= 0) {
      continue;
    }
    offsets[bindIdx] = range.offset;
    ++bindIdx;
  }
  pass.SetBindGroup(0, find_bind_group(data.bindGroups.uniformBindGroup), bindIdx, offsets.data());
  if (data.bindGroups.samplerBindGroup && data.bindGroups.textureBindGroup) {
    pass.SetBindGroup(1, find_bind_group(data.bindGroups.samplerBindGroup));
    pass.SetBindGroup(2, find_bind_group(data.bindGroups.textureBindGroup));
  }
  pass.SetVertexBuffer(0, g_vertexBuffer, data.vertRange.offset, data.vertRange.size);
  pass.SetIndexBuffer(g_indexBuffer, wgpu::IndexFormat::Uint16, data.idxRange.offset, data.idxRange.size);
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  pass.DrawIndexed(data.indexCount);
}
} // namespace aurora::gfx::model

static absl::flat_hash_map<aurora::HashType, aurora::gfx::Range> sCachedRanges;

#include "gx.hpp"

#include "aurora/math.hpp"
#include "../../gfx/model/shader.hpp"
#include "../../gfx/gx_fmt.hpp"
#include "../../gfx/shader_info.hpp"

#include <cstring>
#include <optional>

struct Attribute {
  uint32_t offset;
  GXAttr attr;
  GXAttrType type;
  aurora::gfx::gx::VtxAttrFmt fmt;
};

struct SStreamState {
  GXPrimitive primitive;
  GXVtxFmt vtxFmt;
  std::vector<Attribute> attrs;
  u16 curAttr = 0;
  u16 vertexCount = 0;
  u16 vertexStart;
  u16 vertexSize;
  aurora::ByteBuffer vertexBuffer;
  uint8_t* vertexData = nullptr;
  std::vector<u16> indices;

  explicit SStreamState(GXPrimitive primitive, GXVtxFmt vtxFmt, std::vector<Attribute> attrs, u16 numVerts,
                        u16 vertexSize, u16 vertexStart) noexcept
  : primitive(primitive), vtxFmt(vtxFmt), attrs(std::move(attrs)), vertexStart(vertexStart), vertexSize(vertexSize) {
    vertexBuffer.reserve_extra(static_cast<size_t>(numVerts) * vertexSize);
    if (numVerts > 3 && (primitive == GX_TRIANGLEFAN || primitive == GX_TRIANGLESTRIP)) {
      indices.reserve(((static_cast<u32>(numVerts) - 3) * 3) + 3);
    } else if (numVerts > 4 && primitive == GX_QUADS) {
      indices.reserve(static_cast<u32>(numVerts) / 4 * 6);
    } else {
      indices.reserve(numVerts);
    }
  }

  [[maybe_unused]] u8 check_direct(GXAttr attr, GXCompCnt cnt, GXCompType type) noexcept {
    const auto& curAttr = attrs[this->curAttr];
    ASSERT(curAttr.attr == attr, "bad attribute order: {}, expected {}", attr, curAttr.attr);
    ASSERT(curAttr.type == GX_DIRECT, "bad attribute type: GX_DIRECT, expected {}", curAttr.type);
    ASSERT(curAttr.fmt.cnt == cnt, "bad attribute count: {}, expected {}", cnt, curAttr.fmt.cnt);
    ASSERT(curAttr.fmt.type == type, "bad attribute type: {}, expected {}", type, curAttr.fmt.type);
    return curAttr.fmt.frac;
  }

  void check_indexed(GXAttr attr, GXAttrType type) noexcept {
    const auto& curAttr = attrs[this->curAttr];
    ASSERT(curAttr.attr == attr, "bad attribute order: {}, expected {}", attr, curAttr.attr);
    ASSERT(curAttr.type == type, "bad attribute type: {}, expected {}", type, curAttr.type);
  }

  template <typename T>
  void append(const T& value) noexcept {
    append_data(&value, sizeof(value), attrs[curAttr].offset);
    next_attribute();
  }

private:
  void append_data(const void* ptr, size_t size, uint32_t offset) {
    if (vertexData == nullptr) {
      const auto vertexStart = vertexBuffer.size();
      vertexBuffer.append_zeroes(vertexSize);
      vertexData = vertexBuffer.data() + vertexStart;
      inc_vertex_count();
    }
    ASSERT(offset + size <= vertexSize, "bad attribute end: {}, expected {}", offset + size, vertexSize);
    memcpy(vertexData + offset, ptr, size);
  }

  void next_attribute() noexcept {
    curAttr = curAttr + 1;
    if (curAttr >= attrs.size()) {
      curAttr = 0;
      vertexData = nullptr;
    }
  }

  void inc_vertex_count() noexcept {
    auto curVertex = vertexStart + vertexCount;
    if (primitive == GX_LINES || primitive == GX_LINESTRIP || primitive == GX_POINTS) {
      // Currently unsupported, skip
      return;
    }
    if (primitive == GX_TRIANGLES || primitive == GX_TRIANGLESTRIP || vertexCount < 3) {
      // pass
    } else if (primitive == GX_TRIANGLEFAN) {
      indices.push_back(vertexStart);
      indices.push_back(curVertex - 1);
    } /*else if (primitive == GX_TRIANGLESTRIP) {
      if ((vertexCount & 1) == 0) {
        indices.push_back(curVertex - 2);
        indices.push_back(curVertex - 1);
      } else {
        indices.push_back(curVertex - 1);
        indices.push_back(curVertex - 2);
      }
    }*/
    else if (primitive == GX_QUADS) {
      if ((vertexCount & 3) == 3) {
        indices.push_back(curVertex - 3);
        indices.push_back(curVertex - 1);
      }
    }
    indices.push_back(curVertex);
    ++vertexCount;
  }
};

static std::optional<SStreamState> sStreamState;
static u16 lastVertexStart = 0;

extern "C" {
void GXBegin(GXPrimitive primitive, GXVtxFmt vtxFmt, u16 nVerts) {
  CHECK(!sStreamState, "Stream began twice!");

  uint16_t vertexSize = 0;
  uint16_t numDirectAttrs = 0;
  uint16_t numIndexedAttrs = 0;
  for (GXAttr attr{}; const auto type : g_gxState.vtxDesc) {
    if (type == GX_DIRECT) {
      ++numDirectAttrs;
      if (attr == GX_VA_POS || attr == GX_VA_NRM) {
        vertexSize += 12;
      } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        vertexSize += 16;
      } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        vertexSize += 8;
      } else
        UNLIKELY { FATAL("dont know how to handle attr {}", attr); }
    } else if (type == GX_INDEX8 || type == GX_INDEX16) {
      ++numIndexedAttrs;
    }
    attr = static_cast<GXAttr>(attr + 1);
  }
  auto [num4xAttr, rem] = std::div(numIndexedAttrs, 4);
  u32 num2xAttr = 0;
  if (rem > 2) {
    ++num4xAttr;
  } else if (rem > 0) {
    ++num2xAttr;
  }
  u32 directStart = num4xAttr * 8 + num2xAttr * 4;
  vertexSize += directStart;

  u32 indexOffset = 0;
  u32 directOffset = directStart;
  std::vector<Attribute> attrs;
  attrs.reserve(numDirectAttrs + numIndexedAttrs);
  const auto& curVtxFmt = g_gxState.vtxFmts[vtxFmt];
  for (GXAttr attr{}; const auto type : g_gxState.vtxDesc) {
    if (type == GX_DIRECT) {
      u32 attrSize;
      if (attr == GX_VA_POS || attr == GX_VA_NRM) {
        attrSize = 12;
      } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        attrSize = 16;
      } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        attrSize = 8;
      } else
        UNLIKELY { FATAL("dont know how to handle attr {}", attr); }
      const auto& attrFmt = curVtxFmt.attrs[attr];
      attrs.emplace_back(directOffset, attr, type, attrFmt);
      directOffset += attrSize;
    } else if (type == GX_INDEX8 || type == GX_INDEX16) {
      attrs.emplace_back(indexOffset, attr, type);
      indexOffset += 2;
    }
    attr = static_cast<GXAttr>(attr + 1);
  }

  CHECK(vertexSize > 0, "no vtx attributes enabled?");
  sStreamState.emplace(primitive, vtxFmt, std::move(attrs), nVerts, vertexSize,
                       /*g_gxState.stateDirty ? 0 : lastVertexStart*/ 0);
}

void GXPosition3f32(f32 x, f32 y, f32 z) {
  sStreamState->check_direct(GX_VA_POS, GX_POS_XYZ, GX_F32);
  sStreamState->append(aurora::Vec3{x, y, z});
}

void GXPosition3u16(u16 x, u16 y, u16 z) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XYZ, GX_U16);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      static_cast<f32>(z) / static_cast<f32>(1 << frac),
  });
}

void GXPosition3s16(s16 x, s16 y, s16 z) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XYZ, GX_S16);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      static_cast<f32>(z) / static_cast<f32>(1 << frac),
  });
}

void GXPosition3u8(u8 x, u8 y, u8 z) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XYZ, GX_U8);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      static_cast<f32>(z) / static_cast<f32>(1 << frac),
  });
}

void GXPosition3s8(s8 x, s8 y, s8 z) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XYZ, GX_S8);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      static_cast<f32>(z) / static_cast<f32>(1 << frac),
  });
}

void GXPosition2f32(f32 x, f32 y) {
  sStreamState->check_direct(GX_VA_POS, GX_POS_XY, GX_F32);
  sStreamState->append(aurora::Vec3{x, y, 0.f});
}

void GXPosition2u16(u16 x, u16 y) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XY, GX_U16);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXPosition2s16(s16 x, s16 y) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XY, GX_S16);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXPosition2u8(u8 x, u8 y) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XY, GX_U8);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXPosition2s8(s8 x, s8 y) {
  const auto frac = sStreamState->check_direct(GX_VA_POS, GX_POS_XY, GX_S8);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXPosition1x16(u16 idx) {
  sStreamState->check_indexed(GX_VA_POS, GX_INDEX16);
  sStreamState->append<u16>(idx);
}

void GXPosition1x8(u8 idx) {
  sStreamState->check_indexed(GX_VA_POS, GX_INDEX8);
  sStreamState->append<u16>(idx);
}

void GXNormal3f32(f32 x, f32 y, f32 z) {
  sStreamState->check_direct(GX_VA_NRM, GX_NRM_XYZ, GX_F32);
  sStreamState->append(aurora::Vec3{x, y, z});
}

void GXNormal3s16(s16 x, s16 y, s16 z) {
  const auto frac = sStreamState->check_direct(GX_VA_NRM, GX_NRM_XYZ, GX_S16);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      static_cast<f32>(z) / static_cast<f32>(1 << frac),
  });
}

void GXNormal3s8(s8 x, s8 y, s8 z) {
  const auto frac = sStreamState->check_direct(GX_VA_NRM, GX_NRM_XYZ, GX_S8);
  sStreamState->append(aurora::Vec3{
      static_cast<f32>(x) / static_cast<f32>(1 << frac),
      static_cast<f32>(y) / static_cast<f32>(1 << frac),
      static_cast<f32>(z) / static_cast<f32>(1 << frac),
  });
}

void GXNormal1x16(u16 index) {
  sStreamState->check_indexed(GX_VA_NRM, GX_INDEX16);
  sStreamState->append<u16>(index);
}

void GXNormal1x8(u8 index) {
  sStreamState->check_indexed(GX_VA_POS, GX_INDEX8);
  sStreamState->append<u16>(index);
}

void GXColor4f32(f32 r, f32 g, f32 b, f32 a) {
  sStreamState->check_direct(GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8);
  sStreamState->append(aurora::Vec4{r, g, b, a});
}

void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
  sStreamState->check_direct(GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8);
  sStreamState->append(aurora::Vec4{
      static_cast<f32>(r) / 255.f,
      static_cast<f32>(g) / 255.f,
      static_cast<f32>(b) / 255.f,
      static_cast<f32>(a) / 255.f,
  });
}

void GXColor3u8(u8 r, u8 g, u8 b) {
  sStreamState->check_direct(GX_VA_CLR0, GX_CLR_RGB, GX_RGB8);
  sStreamState->append(aurora::Vec4{
      static_cast<f32>(r) / 255.f,
      static_cast<f32>(g) / 255.f,
      static_cast<f32>(b) / 255.f,
      1.f,
  });
}

void GXColor1u32(u32 clr) {
  sStreamState->check_direct(GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8);
  sStreamState->append(aurora::Vec4{
      static_cast<f32>((clr >> 24) & 0xff) / 255.f,
      static_cast<f32>((clr >> 16) & 0xff) / 255.f,
      static_cast<f32>((clr >> 8) & 0xff) / 255.f,
      static_cast<f32>(clr & 0xff) / 255.f,
  });
}

void GXColor1u16(u16 clr) {
  sStreamState->check_direct(GX_VA_CLR0, GX_CLR_RGB, GX_RGB565);
  sStreamState->append(aurora::Vec4{
      static_cast<f32>((clr >> 11) & 0x1f) / 31.f,
      static_cast<f32>((clr >> 5) & 0x3f) / 63.f,
      static_cast<f32>(clr & 0x1f) / 31.f,
      1.f,
  });
}

void GXColor1x16(u16 index) {
  sStreamState->check_indexed(GX_VA_CLR0, GX_INDEX16);
  sStreamState->append<u16>(index);
}

void GXColor1x8(u8 index) {
  sStreamState->check_indexed(GX_VA_CLR0, GX_INDEX8);
  sStreamState->append<u16>(index);
}

void GXTexCoord2f32(f32 s, f32 t) {
  sStreamState->check_direct(GX_VA_TEX0, GX_TEX_ST, GX_F32);
  sStreamState->append(aurora::Vec2{s, t});
}

void GXTexCoord2u16(u16 s, u16 t) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_ST, GX_U16);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      static_cast<f32>(t) / static_cast<f32>(1 << frac),
  });
}

void GXTexCoord2s16(s16 s, s16 t) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_ST, GX_S16);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      static_cast<f32>(t) / static_cast<f32>(1 << frac),
  });
}

void GXTexCoord2u8(u8 s, u8 t) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_ST, GX_U8);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      static_cast<f32>(t) / static_cast<f32>(1 << frac),
  });
}

void GXTexCoord2s8(s8 s, s8 t) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_ST, GX_S8);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      static_cast<f32>(t) / static_cast<f32>(1 << frac),
  });
}

void GXTexCoord1f32(f32 s) {
  sStreamState->check_direct(GX_VA_TEX0, GX_TEX_S, GX_F32);
  sStreamState->append(aurora::Vec2{s, 0.f});
}

void GXTexCoord1u16(u16 s) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_S, GX_U16);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXTexCoord1s16(s16 s) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_S, GX_S16);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXTexCoord1u8(u8 s) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_S, GX_U8);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXTexCoord1s8(s8 s) {
  const auto frac = sStreamState->check_direct(GX_VA_TEX0, GX_TEX_S, GX_S8);
  sStreamState->append(aurora::Vec2{
      static_cast<f32>(s) / static_cast<f32>(1 << frac),
      0.f,
  });
}

void GXTexCoord1x16(u16 index) {
  sStreamState->check_indexed(GX_VA_TEX0, GX_INDEX16);
  sStreamState->append(index);
}

void GXTexCoord1x8(u8 index) {
  sStreamState->check_indexed(GX_VA_TEX0, GX_INDEX8);
  sStreamState->append(static_cast<u16>(index));
}

void GXEnd() {
  if (sStreamState->vertexCount == 0) {
    sStreamState.reset();
    return;
  }
  const auto vertRange = aurora::gfx::push_verts(sStreamState->vertexBuffer.data(), sStreamState->vertexBuffer.size());
  const auto indexRange = aurora::gfx::push_indices(aurora::ArrayRef{sStreamState->indices});

  aurora::gfx::gx::BindGroupRanges ranges{};
  for (int i = 0; i < GX_VA_MAX_ATTR; ++i) {
    if (g_gxState.vtxDesc[i] != GX_INDEX8 && g_gxState.vtxDesc[i] != GX_INDEX16) {
      continue;
    }
    auto& array = g_gxState.arrays[i];
    if (array.cachedRange.size > 0) {
      // Use the currently cached range
      ranges.vaRanges[i] = array.cachedRange;
    } else {
      // Push array data to storage and cache range
      const auto range = aurora::gfx::push_storage(static_cast<const uint8_t*>(array.data), array.size);
      ranges.vaRanges[i] = range;
      array.cachedRange = range;
    }
  }

  // if (g_gxState.stateDirty) {
  aurora::gfx::model::PipelineConfig config{};
  GXPrimitive primitive = GX_TRIANGLES;
  switch (sStreamState->primitive) {
  case GX_TRIANGLESTRIP:
    primitive = GX_TRIANGLESTRIP;
    break;
  default:
    break;
  }
  populate_pipeline_config(config, primitive, sStreamState->vtxFmt);
  const auto info = build_shader_info(config.shaderConfig);
  const auto bindGroups = aurora::gfx::gx::build_bind_groups(info, config.shaderConfig, ranges);
  const auto pipeline = aurora::gfx::pipeline_ref(config);
  aurora::gfx::push_draw_command(aurora::gfx::model::DrawData{
      .pipeline = pipeline,
      .vertRange = vertRange,
      .idxRange = indexRange,
      .dataRanges = ranges,
      .uniformRange = build_uniform(info),
      .indexCount = static_cast<uint32_t>(sStreamState->indices.size()),
      .bindGroups = bindGroups,
      .dstAlpha = g_gxState.dstAlpha,
  });
  // } else {
  //   aurora::gfx::merge_draw_command(aurora::gfx::model::DrawData{
  //       .vertRange = vertRange,
  //       .idxRange = indexRange,
  //       .indexCount = static_cast<uint32_t>(sStreamState->indices.size()),
  //   });
  // }
  lastVertexStart = sStreamState->vertexStart + sStreamState->vertexCount;
  sStreamState.reset();
}
}
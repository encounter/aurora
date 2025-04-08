#include "gx.hpp"

#include "../../gfx/stream/shader.hpp"

#include <algorithm>
#include <optional>

#ifndef NDEBUG
static inline GXAttr next_attr(size_t begin) {
  auto iter = std::find_if(g_gxState.vtxDesc.begin() + begin, g_gxState.vtxDesc.end(),
                           [](const auto type) { return type != GX_NONE; });
  if (begin > 0 && iter == g_gxState.vtxDesc.end()) {
    // wrap around
    iter = std::find_if(g_gxState.vtxDesc.begin(), g_gxState.vtxDesc.end(),
                        [](const auto type) { return type != GX_NONE; });
  }
  return GXAttr(iter - g_gxState.vtxDesc.begin());
}
#endif

struct SStreamState {
  GXPrimitive primitive;
  GXVtxFmt vtxFmt;
  u16 vertexCount = 0;
  u16 vertexStart = 0;
  aurora::ByteBuffer vertexBuffer;
  std::vector<u16> indices;
#ifndef NDEBUG
  GXAttr nextAttr;
#endif

  explicit SStreamState(GXPrimitive primitive, GXVtxFmt vtxFmt, u16 numVerts, u16 vertexSize, u16 vertexStart) noexcept
  : primitive(primitive), vtxFmt(vtxFmt), vertexStart(vertexStart) {
    vertexBuffer.reserve_extra(size_t(numVerts) * vertexSize);
    if (numVerts > 3 && (primitive == GX_TRIANGLEFAN || primitive == GX_TRIANGLESTRIP)) {
      indices.reserve((u32(numVerts) - 3) * 3 + 3);
    } else if (numVerts > 4 && primitive == GX_QUADS) {
      indices.reserve(u32(numVerts) / 4 * 6);
    } else {
      indices.reserve(numVerts);
    }
#ifndef NDEBUG
    nextAttr = next_attr(0);
#endif
  }
};

static std::optional<SStreamState> sStreamState;
static u16 lastVertexStart = 0;

extern "C" {
void GXBegin(GXPrimitive primitive, GXVtxFmt vtxFmt, u16 nVerts) {
  CHECK(!sStreamState, "Stream began twice!");
  uint16_t vertexSize = 0;
  for (GXAttr attr{}; const auto type : g_gxState.vtxDesc) {
    if (type == GX_DIRECT) {
      if (attr == GX_VA_POS || attr == GX_VA_NRM) {
        vertexSize += 12;
      } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        vertexSize += 16;
      } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        vertexSize += 8;
      } else UNLIKELY {
        FATAL("dont know how to handle attr {}", static_cast<int>(attr));
      }
    } else if (type == GX_INDEX8 || type == GX_INDEX16) {
      vertexSize += 2;
    }
    attr = GXAttr(attr + 1);
  }
  CHECK(vertexSize > 0, "no vtx attributes enabled?");
  sStreamState.emplace(primitive, vtxFmt, nVerts, vertexSize, g_gxState.stateDirty ? 0 : lastVertexStart);
}

static inline void check_attr_order(GXAttr attr) noexcept {
#ifndef NDEBUG
  CHECK(sStreamState, "Stream not started!");
  CHECK(sStreamState->nextAttr == attr, "bad attribute order: {}, expected {}", static_cast<int>(attr),
        static_cast<int>(sStreamState->nextAttr));
  sStreamState->nextAttr = next_attr(attr + 1);
#endif
}

void GXPosition3f32(float x, float y, float z) {
  check_attr_order(GX_VA_POS);
  auto& state = *sStreamState;
  state.vertexBuffer.append(&x, sizeof(float));
  state.vertexBuffer.append(&y, sizeof(float));
  state.vertexBuffer.append(&z, sizeof(float));
  auto curVertex = state.vertexStart + state.vertexCount;
  if (state.primitive == GX_TRIANGLES || state.vertexCount < 3) {
    // pass
  } else if (state.primitive == GX_TRIANGLEFAN) {
    state.indices.push_back(state.vertexStart);
    state.indices.push_back(curVertex - 1);
  } else if (state.primitive == GX_TRIANGLESTRIP) {
    if ((state.vertexCount & 1) == 0) {
      state.indices.push_back(curVertex - 2);
      state.indices.push_back(curVertex - 1);
    } else {
      state.indices.push_back(curVertex - 1);
      state.indices.push_back(curVertex - 2);
    }
  } else if (state.primitive == GX_QUADS) {
    if ((state.vertexCount & 3) == 3) {
      state.indices.push_back(curVertex - 3);
      state.indices.push_back(curVertex - 1);
    }
  }
  state.indices.push_back(curVertex);
  ++state.vertexCount;
}

void GXPosition3u16(u16 x, u16 y, u16 z) {
  const auto& attrFmt = g_gxState.vtxFmts[sStreamState->vtxFmt].attrs[GX_VA_POS];
  GXPosition3f32(
    static_cast<float>(x) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(y) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(z) / static_cast<f32>(1 << attrFmt.frac)
  );
}

void GXPosition3s16(s16 x, s16 y, s16 z) {
  const auto& attrFmt = g_gxState.vtxFmts[sStreamState->vtxFmt].attrs[GX_VA_POS];
  GXPosition3f32(
    static_cast<float>(x) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(y) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(z) / static_cast<f32>(1 << attrFmt.frac)
  );
}

void GXPosition3u8(u8 x, u8 y, u8 z) {
  const auto& attrFmt = g_gxState.vtxFmts[sStreamState->vtxFmt].attrs[GX_VA_POS];
  GXPosition3f32(
    static_cast<float>(x) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(y) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(z) / static_cast<f32>(1 << attrFmt.frac)
  );
}

void GXPosition3s8(s8 x, s8 y, s8 z) {
  const auto& attrFmt = g_gxState.vtxFmts[sStreamState->vtxFmt].attrs[GX_VA_POS];
  GXPosition3f32(
    static_cast<float>(x) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(y) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(z) / static_cast<f32>(1 << attrFmt.frac)
  );
}

void GXPosition2f32(float x, float y) {
  GXPosition3f32(x, y, 0.f);
}

void GXPosition2u16(u16 x, u16 y) {
  GXPosition3u16(x, y, 0);
}

void GXPosition2s16(s16 x, s16 y) {
  GXPosition3s16(x, y, 0);
}

void GXPosition2u8(u8 x, u8 y) {
  GXPosition3u8(x, y, 0);
}

void GXPosition2s8(s8 x, s8 y) {
  GXPosition3s8(x, y, 0);
}

void GXPosition1x16(u16 idx) {
  check_attr_order(GX_VA_POS);
  // keep aligned
  if (sStreamState->vertexBuffer.size() % 4 != 0) {
    sStreamState->vertexBuffer.append_zeroes(4 - (sStreamState->vertexBuffer.size() % 4));
  }
  sStreamState->vertexBuffer.append(&idx, 2);
}

void GXPosition1x8(u8 idx) {
  GXPosition1x16(idx);
}

void GXNormal3f32(float x, float y, float z) {
  check_attr_order(GX_VA_NRM);
  sStreamState->vertexBuffer.append(&x, 4);
  sStreamState->vertexBuffer.append(&y, 4);
  sStreamState->vertexBuffer.append(&z, 4);
}

void GXNormal3s16(s16 x, s16 y, s16 z) {
  const auto& attrFmt = g_gxState.vtxFmts[sStreamState->vtxFmt].attrs[GX_VA_NRM];
  GXNormal3f32(
    static_cast<float>(x) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(y) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(z) / static_cast<f32>(1 << attrFmt.frac)
  );
}

void GXNormal3s8(s8 x, s8 y, s8 z) {
  const auto& attrFmt = g_gxState.vtxFmts[sStreamState->vtxFmt].attrs[GX_VA_NRM];
  GXNormal3f32(
    static_cast<float>(x) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(y) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(z) / static_cast<f32>(1 << attrFmt.frac)
  );
}

void GXNormal1x16(u16 idx) {
  check_attr_order(GX_VA_NRM);
  // keep aligned
  if (sStreamState->vertexBuffer.size() % 4 != 0) {
    sStreamState->vertexBuffer.append_zeroes(4 - (sStreamState->vertexBuffer.size() % 4));
  }
  sStreamState->vertexBuffer.append(&idx, 2);
}

void GXNormal1x8(u8 idx) {
  GXNormal1x16(idx);
}

void GXColor4f32(float r, float g, float b, float a) {
  check_attr_order(GX_VA_CLR0);
  sStreamState->vertexBuffer.append(&r, 4);
  sStreamState->vertexBuffer.append(&g, 4);
  sStreamState->vertexBuffer.append(&b, 4);
  sStreamState->vertexBuffer.append(&a, 4);
}

void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
  GXColor4f32(static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f,
              static_cast<float>(a) / 255.f);
}

void GXColor3u8(u8 r, u8 g, u8 b) {
  GXColor4u8(r, g, b, 255);
}

void GXColor1x16(u16 idx) {
  check_attr_order(GX_VA_CLR0);
  // keep aligned
  if (sStreamState->vertexBuffer.size() % 4 != 0) {
    sStreamState->vertexBuffer.append_zeroes(4 - (sStreamState->vertexBuffer.size() % 4));
  }
  sStreamState->vertexBuffer.append(&idx, 2);
}

void GXColor1x8(u8 idx) {
  GXColor1x16(idx);
}

void GXTexCoord2f32(float u, float v) {
  check_attr_order(GX_VA_TEX0);
  sStreamState->vertexBuffer.append(&u, 4);
  sStreamState->vertexBuffer.append(&v, 4);
}

void GXTexCoord2s16(s16 s, s16 t) {
  const auto& attrFmt = g_gxState.vtxFmts[sStreamState->vtxFmt].attrs[GX_VA_TEX0];
  GXTexCoord2f32(
    static_cast<float>(s) / static_cast<f32>(1 << attrFmt.frac),
    static_cast<float>(t) / static_cast<f32>(1 << attrFmt.frac)
  );
}

void GXTexCoord1x16(u16 idx) {
  check_attr_order(GX_VA_TEX0);
  // keep aligned
  if (sStreamState->vertexBuffer.size() % 4 != 0) {
    sStreamState->vertexBuffer.append_zeroes(4 - (sStreamState->vertexBuffer.size() % 4));
  }
  sStreamState->vertexBuffer.append(&idx, 2);
}

void GXTexCoord1x8(u8 idx) {
  GXTexCoord1x16(idx);
}

void GXEnd() {
  if (sStreamState->vertexCount == 0) {
    sStreamState.reset();
    return;
  }
  const auto vertRange = aurora::gfx::push_verts(sStreamState->vertexBuffer.data(), sStreamState->vertexBuffer.size());
  const auto indexRange = aurora::gfx::push_indices(aurora::ArrayRef{sStreamState->indices});
  if (g_gxState.stateDirty) {
    aurora::gfx::stream::PipelineConfig config{};
    populate_pipeline_config(config, GX_TRIANGLES);
    const auto info = build_shader_info(config.shaderConfig);
    const auto pipeline = aurora::gfx::pipeline_ref(config);
    aurora::gfx::push_draw_command(aurora::gfx::stream::DrawData{
        .pipeline = pipeline,
        .vertRange = vertRange,
        .uniformRange = build_uniform(info),
        .indexRange = indexRange,
        .indexCount = static_cast<uint32_t>(sStreamState->indices.size()),
        .bindGroups = build_bind_groups(info, config.shaderConfig, {}),
        .dstAlpha = g_gxState.dstAlpha,
    });
  } else {
    aurora::gfx::merge_draw_command(aurora::gfx::stream::DrawData{
        .vertRange = vertRange,
        .indexRange = indexRange,
        .indexCount = static_cast<uint32_t>(sStreamState->indices.size()),
    });
  }
  lastVertexStart = sStreamState->vertexStart + sStreamState->vertexCount;
  sStreamState.reset();
}
}
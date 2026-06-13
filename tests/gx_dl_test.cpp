#include "gx_test_common.hpp"

#include "aurora/dl.hpp"
#include "dolphin/gx/GXAurora.h"
#include "gx/pipeline.hpp"

#include <cstring>

using namespace aurora::gx::dl;

namespace aurora::gfx {
extern gx::DrawData g_testLastDraw;
extern uint32_t g_testDrawCount;
} // namespace aurora::gfx

namespace {

const GXVtxDescList kPosClrDesc[] = {
    {GX_VA_POS, GX_INDEX8},
    {GX_VA_CLR0, GX_INDEX8},
    {GX_VA_NULL, GX_NONE},
};

const GXVtxDescList kVtxDesc[] = {
    {GX_VA_POS, GX_INDEX8},  {GX_VA_NRM, GX_INDEX8}, {GX_VA_CLR0, GX_INDEX8},
    {GX_VA_TEX0, GX_INDEX8}, {GX_VA_NULL, GX_NONE},
};

u8 op(GXPrimitive prim, GXVtxFmt fmt) { return static_cast<u8>(prim) | static_cast<u8>(fmt); }

void be16(std::vector<u8>& out, u16 value) {
  out.push_back(value >> 8);
  out.push_back(value & 0xFF);
}

void draw_cmd(std::vector<u8>& out, u8 opcode, u16 vtxCount, std::initializer_list<u8> vertices) {
  out.push_back(opcode);
  be16(out, vtxCount);
  out.insert(out.end(), vertices);
}

u16 host_u16(const u8* data) {
  u16 value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

std::vector<std::array<u16, 3>> collect_triangles(GXPrimitive prim, u16 vtxCount) {
  std::vector<std::array<u16, 3>> tris;
  expand_triangles(prim, vtxCount, [&](u16 i0, u16 i1, u16 i2) { tris.push_back({i0, i1, i2}); });
  return tris;
}

} // namespace

TEST(GXDlReader, WalksLeafStripDl) {
  std::vector<u8> dl;
  dl.push_back(GX_NOP);
  // 4-vertex strip, vertices are (pos, nrm, clr, tex) index tuples
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 4, {0, 0, 0, 0, 1, 0, 1, 1, 2, 0, 2, 2, 3, 0, 3, 3});
  dl.push_back(GX_NOP);

  Reader reader{dl.data(), static_cast<u32>(dl.size()), kVtxDesc};

  auto cmd = reader.next();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->kind, Command::Kind::Passthrough);
  EXPECT_EQ(cmd->size, 1u);

  cmd = reader.next();
  ASSERT_TRUE(cmd.has_value());
  ASSERT_EQ(cmd->kind, Command::Kind::Draw);
  EXPECT_EQ(cmd->draw.prim, GX_TRIANGLESTRIP);
  EXPECT_EQ(cmd->draw.fmt, GX_VTXFMT0);
  EXPECT_EQ(cmd->draw.vtxCount, 4);
  EXPECT_EQ(cmd->draw.layout->stride, 4);
  EXPECT_EQ(cmd->draw.attr_idx(0, GX_VA_POS), 0);
  EXPECT_EQ(cmd->draw.attr_idx(2, GX_VA_POS), 2);
  EXPECT_EQ(cmd->draw.attr_idx(2, GX_VA_CLR0), 2);
  EXPECT_EQ(cmd->draw.attr_idx(3, GX_VA_TEX0), 3);
  EXPECT_EQ(cmd->draw.attr_idx(3, GX_VA_NRM), 0);

  cmd = reader.next();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->kind, Command::Kind::Passthrough);

  EXPECT_FALSE(reader.next().has_value());
  EXPECT_FALSE(reader.failed());
}

TEST(GXDlReader, FailsOnUnknownOpcode) {
  const std::vector<u8> dl{0x70, 0x00, 0x00};
  Reader reader{dl.data(), static_cast<u32>(dl.size()), kPosClrDesc};
  EXPECT_FALSE(reader.next().has_value());
  EXPECT_TRUE(reader.failed());
}

TEST(GXDlReader, FailsOnDrawOverrun) {
  std::vector<u8> dl;
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 100, {0, 0, 1, 1});
  Reader reader{dl.data(), static_cast<u32>(dl.size()), kPosClrDesc};
  EXPECT_FALSE(reader.next().has_value());
  EXPECT_TRUE(reader.failed());
}

TEST(GXDlReader, StrideOnlyWalksAndSizes) {
  std::vector<u8> dl;
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 3, {0, 0, 1, 1, 2, 2});
  // BP write passes through
  dl.insert(dl.end(), {0x61, 0x41, 0x00, 0x00, 0x01});
  draw_cmd(dl, op(GX_TRIANGLES, GX_VTXFMT0), 3, {0, 0, 1, 1, 2, 2});

  Reader reader{dl.data(), static_cast<u32>(dl.size()), static_cast<u8>(2)};
  u32 vtxTotal = 0;
  u32 passthrough = 0;
  while (const auto cmd = reader.next()) {
    if (cmd->kind == Command::Kind::Draw) {
      vtxTotal += cmd->draw.vtxCount;
    } else {
      ++passthrough;
    }
  }
  EXPECT_FALSE(reader.failed());
  EXPECT_EQ(vtxTotal, 6u);
  EXPECT_EQ(passthrough, 1u);
}

TEST(GXDlExpand, StripFanQuadWinding) {
  using Tri = std::array<u16, 3>;
  EXPECT_EQ(collect_triangles(GX_TRIANGLESTRIP, 5), (std::vector<Tri>{{0, 1, 2}, {2, 1, 3}, {2, 3, 4}}));
  EXPECT_EQ(collect_triangles(GX_TRIANGLEFAN, 4), (std::vector<Tri>{{0, 1, 2}, {0, 2, 3}}));
  EXPECT_EQ(collect_triangles(GX_QUADS, 8), (std::vector<Tri>{{0, 1, 2}, {2, 3, 0}, {4, 5, 6}, {6, 7, 4}}));
  EXPECT_EQ(collect_triangles(GX_TRIANGLES, 3), (std::vector<Tri>{{0, 1, 2}}));

  EXPECT_FALSE(expand_triangles(GX_LINES, 4, [](u16, u16, u16) {}));
  EXPECT_FALSE(expand_triangles(GX_TRIANGLESTRIP, 2, [](u16, u16, u16) {}));
  EXPECT_FALSE(expand_triangles(GX_QUADS, 6, [](u16, u16, u16) {}));
}

TEST(GXDlOptimize, MergesAdjacentStrips) {
  std::vector<u8> dl;
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 4, {0, 0, 1, 1, 2, 2, 3, 3});
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 4, {4, 4, 5, 5, 6, 6, 7, 7});
  dl.push_back(GX_NOP);

  const auto result = optimize(dl.data(), static_cast<u32>(dl.size()), kPosClrDesc);
  ASSERT_TRUE(result.has_value());
  const auto& out = *result;

  // One DRAW_INDEXED command: 10-byte header, 12 u16 indices, 8 2-byte vertices
  ASSERT_EQ(out.size(), 10u + 12 * 2 + 8 * 2);
  EXPECT_EQ(out[0], GX_AURORA);
  EXPECT_EQ((out[1] << 8 | out[2]), GX_AURORA_DRAW_INDEXED);
  EXPECT_EQ(out[3], op(GX_TRIANGLES, GX_VTXFMT0));
  EXPECT_EQ((out[4] << 8 | out[5]), 8);                                // vtxCount
  EXPECT_EQ((out[6] << 24 | out[7] << 16 | out[8] << 8 | out[9]), 12); // indexCount

  // Host-endian indices: strip 0 at base 0, strip 1 at base 4
  const u16 expected[12] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
  for (int i = 0; i < 12; i++) {
    EXPECT_EQ(host_u16(out.data() + 10 + i * 2), expected[i]) << "index " << i;
  }

  // Vertex tuples concatenated verbatim
  const u8 expectedVerts[16] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7};
  EXPECT_EQ(std::memcmp(out.data() + 10 + 12 * 2, expectedVerts, sizeof(expectedVerts)), 0);
}

TEST(GXDlOptimize, PureTrianglesStayPlain) {
  std::vector<u8> dl;
  draw_cmd(dl, op(GX_TRIANGLES, GX_VTXFMT0), 3, {0, 0, 1, 1, 2, 2});
  draw_cmd(dl, op(GX_TRIANGLES, GX_VTXFMT0), 3, {3, 3, 4, 4, 5, 5});

  const auto result = optimize(dl.data(), static_cast<u32>(dl.size()), kPosClrDesc);
  ASSERT_TRUE(result.has_value());
  const auto& out = *result;

  // Merged into a single plain triangles draw (no index buffer needed at runtime)
  ASSERT_EQ(out.size(), 3u + 6 * 2);
  EXPECT_EQ(out[0], op(GX_TRIANGLES, GX_VTXFMT0));
  EXPECT_EQ((out[1] << 8 | out[2]), 6);
}

TEST(GXDlOptimize, StateCommandIsBarrier) {
  std::vector<u8> dl;
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 3, {0, 0, 1, 1, 2, 2});
  const u8 bpCmd[] = {0x61, 0x41, 0x00, 0x00, 0x01};
  dl.insert(dl.end(), std::begin(bpCmd), std::end(bpCmd));
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 3, {3, 3, 4, 4, 5, 5});

  const auto result = optimize(dl.data(), static_cast<u32>(dl.size()), kPosClrDesc);
  ASSERT_TRUE(result.has_value());
  const auto& out = *result;

  // DRAW_INDEXED(3 verts, 3 indices), BP, DRAW_INDEXED(3 verts, 3 indices)
  const u32 drawSize = 10 + 3 * 2 + 3 * 2;
  ASSERT_EQ(out.size(), drawSize * 2 + sizeof(bpCmd));
  EXPECT_EQ(out[0], GX_AURORA);
  EXPECT_EQ(std::memcmp(out.data() + drawSize, bpCmd, sizeof(bpCmd)), 0);
  EXPECT_EQ(out[drawSize + sizeof(bpCmd)], GX_AURORA);

  // Re-walking the optimized list yields DrawIndexed commands with the same vertices
  Reader reader{out.data(), static_cast<u32>(out.size()), kPosClrDesc};
  auto cmd = reader.next();
  ASSERT_TRUE(cmd.has_value());
  ASSERT_EQ(cmd->kind, Command::Kind::DrawIndexed);
  EXPECT_EQ(cmd->draw.vtxCount, 3);
  EXPECT_EQ(cmd->draw.indexCount, 3u);
  EXPECT_EQ(cmd->draw.index(2), 2);
  EXPECT_EQ(cmd->draw.attr_idx(1, GX_VA_POS), 1);
  cmd = reader.next();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->kind, Command::Kind::Passthrough);
  cmd = reader.next();
  ASSERT_TRUE(cmd.has_value());
  ASSERT_EQ(cmd->kind, Command::Kind::DrawIndexed);
  EXPECT_EQ(cmd->draw.attr_idx(0, GX_VA_POS), 3);
  EXPECT_FALSE(reader.failed());
}

TEST(GXDlOptimize, FailsOnDirectAttrWithoutFmt) {
  const GXVtxDescList desc[] = {
      {GX_VA_POS, GX_DIRECT},
      {GX_VA_NULL, GX_NONE},
  };
  std::vector<u8> dl;
  draw_cmd(dl, op(GX_TRIANGLES, GX_VTXFMT0), 3, {0, 0, 0, 0, 0, 0});
  EXPECT_FALSE(optimize(dl.data(), static_cast<u32>(dl.size()), desc).has_value());
}

TEST(GXDlOptimize, DirectAttrWithFmt) {
  const GXVtxDescList desc[] = {
      {GX_VA_PNMTXIDX, GX_DIRECT},
      {GX_VA_POS, GX_DIRECT},
      {GX_VA_NULL, GX_NONE},
  };
  const GXVtxAttrFmtList fmt0[] = {
      {GX_VA_POS, GX_POS_XYZ, GX_S16, 0},
      {GX_VA_NULL, GX_POS_XYZ, GX_U8, 0},
  };
  const VtxFmtLists fmts{fmt0};

  // Stride: 1 (pnmtxidx) + 6 (3x s16) = 7; quad of 4 vertices
  std::vector<u8> dl;
  dl.push_back(op(GX_QUADS, GX_VTXFMT0));
  be16(dl, 4);
  for (u8 v = 0; v < 4; v++) {
    dl.push_back(v * 3); // pnmtxidx
    for (int b = 0; b < 6; b++) {
      dl.push_back(v);
    }
  }

  const auto result = optimize(dl.data(), static_cast<u32>(dl.size()), desc, &fmts);
  ASSERT_TRUE(result.has_value());
  // DRAW_INDEXED: 10-byte header, 6 u16 indices, 4 7-byte vertices
  ASSERT_EQ(result->size(), 10u + 6 * 2 + 4 * 7);

  Reader reader{result->data(), static_cast<u32>(result->size()), desc, &fmts};
  const auto cmd = reader.next();
  ASSERT_TRUE(cmd.has_value());
  ASSERT_EQ(cmd->kind, Command::Kind::DrawIndexed);
  EXPECT_EQ(cmd->draw.layout->stride, 7);
  EXPECT_EQ(cmd->draw.attr_idx(2, GX_VA_PNMTXIDX), 6);
}

TEST_F(GXFifoTest, DrawIndexed_RoundTripThroughProcessor) {
  std::vector<u8> dl;
  draw_cmd(dl, op(GX_TRIANGLESTRIP, GX_VTXFMT0), 4, {0, 0, 1, 1, 2, 2, 3, 3});
  draw_cmd(dl, op(GX_TRIANGLEFAN, GX_VTXFMT0), 4, {4, 4, 5, 5, 6, 6, 7, 7});

  const auto result = optimize(dl.data(), static_cast<u32>(dl.size()), kPosClrDesc);
  ASSERT_TRUE(result.has_value());

  // Match the optimizer's descriptor in runtime CP state
  gxState().vtxDesc[GX_VA_POS] = GX_INDEX8;
  gxState().vtxDesc[GX_VA_CLR0] = GX_INDEX8;

  aurora::gfx::g_testDrawCount = 0;
  decode_fifo(*result);

  EXPECT_EQ(aurora::gfx::g_testDrawCount, 1u);
  EXPECT_EQ(aurora::gfx::g_testLastDraw.vtxCount, 8u);
  EXPECT_EQ(aurora::gfx::g_testLastDraw.indexCount, 12u);
  EXPECT_EQ(aurora::gfx::g_testLastDraw.instanceCount, 1u);
}

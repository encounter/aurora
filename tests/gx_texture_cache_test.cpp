#include "gx/texture.hpp"

#include <aurora/texture.hpp>
#include <gtest/gtest.h>
#include <xxhash.h>

#include <array>
#include <cstdint>
#include <vector>

namespace aurora::gx::testing {
void reset_texture_stubs();
uint64_t texture_allocations();
uint64_t palette_conversions();
gfx::TextureHandle make_texture_handle(uint32_t width, uint32_t height, u32 format = GX_TF_RGBA8_PC);
void set_replacement(gfx::TextureHandle handle, uint64_t id = 1);
void set_source_replacement(aurora::texture::TextureSourceKey key, gfx::TextureHandle handle);
} // namespace aurora::gx::testing

namespace aurora::gx {
namespace {
GXTexObj_ make_texture(const void* data, u32 id, u32 format = GX_TF_RGBA8_PC, u32 width = 2, u32 height = 2) {
  GXTexObj_ obj{};
  obj.data = data;
  obj.mWidth = width;
  obj.mHeight = height;
  obj.mFormat = format;
  obj.texObjId = id;
  obj.texDataVersion = 1;
  return obj;
}

GXTlutObj_ make_tlut(const void* data, u32 id, u16 entries = 16) {
  GXTlutObj_ tlut{};
  tlut.data = data;
  tlut.format = GX_TL_RGB5A3;
  tlut.numEntries = entries;
  tlut.tlutObjId = id;
  tlut.tlutDataVersion = 1;
  return tlut;
}

class GxTextureCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    texture::shutdown();
    g_gxState = {};
    testing::reset_texture_stubs();
    texture::end_frame();
  }

  void TearDown() override { texture::shutdown(); }
};

TEST_F(GxTextureCacheTest, CalculatesTiledAndLinearMipSourceSizes) {
  EXPECT_EQ(texture::texture_source_size(GX_TF_I4, 8, 8, 1), 32);
  EXPECT_EQ(texture::texture_source_size(GX_TF_I4, 9, 9, 3), 192);
  EXPECT_EQ(texture::texture_source_size(GX_TF_RGBA8, 5, 5, 1), 256);
  EXPECT_EQ(texture::texture_source_size(GX_TF_RGBA8_PC, 5, 3, 3), 72);
  EXPECT_EQ(texture::texture_source_size(GX_TF_BC1_PC, 5, 5, 2), 40);
  EXPECT_EQ(texture::tlut_source_size(256), 512);
}

TEST_F(GxTextureCacheTest, ReusesIdenticalContentAtDifferentAddresses) {
  std::array<uint8_t, 16> first{};
  std::array<uint8_t, 16> second{};
  const auto firstHandle = texture::resolve_static_texture(make_texture(first.data(), 1));
  const auto secondHandle = texture::resolve_static_texture(make_texture(second.data(), 2));

  EXPECT_EQ(firstHandle, secondHandle);
  EXPECT_EQ(testing::texture_allocations(), 1);
  EXPECT_EQ(texture_stats().contentHits, 1);
}

TEST_F(GxTextureCacheTest, ObjectHitDoesNotHashAgain) {
  std::array<uint8_t, 16> pixels{};
  const auto obj = make_texture(pixels.data(), 1);
  texture::resolve_static_texture(obj);
  const uint64_t hashedBytes = texture_stats().hashedBytes;

  texture::resolve_static_texture(obj);

  EXPECT_EQ(texture_stats().hashedBytes, hashedBytes);
  EXPECT_EQ(texture_stats().objectHits, 1);
  EXPECT_EQ(testing::texture_allocations(), 1);
}

TEST_F(GxTextureCacheTest, ChangedContentAndMetadataMiss) {
  std::array<uint8_t, 16> first{};
  std::array<uint8_t, 16> second{};
  second[7] = 1;
  texture::resolve_static_texture(make_texture(first.data(), 1));
  texture::resolve_static_texture(make_texture(second.data(), 2));

  std::array<uint8_t, 32> wider{};
  texture::resolve_static_texture(make_texture(wider.data(), 3, GX_TF_RGBA8_PC, 4, 2));

  EXPECT_EQ(testing::texture_allocations(), 3);
  EXPECT_EQ(texture_stats().misses, 3);
}

TEST_F(GxTextureCacheTest, DestroyedObjectStillReusesContent) {
  std::array<uint8_t, 16> pixels{};
  auto obj = make_texture(pixels.data(), 1);
  obj.set_no_cache(true);

  const auto first = texture::resolve_static_texture(obj);
  const auto second = texture::resolve_static_texture(obj);

  EXPECT_EQ(first, second);
  EXPECT_EQ(testing::texture_allocations(), 1);
  EXPECT_EQ(texture_stats().contentHits, 1);
  EXPECT_EQ(texture_stats().objectHits, 0);
}

TEST_F(GxTextureCacheTest, PaletteContentIncludesTlutBytesAndMetadata) {
  std::array<uint8_t, 32> indicesA{};
  std::array<uint8_t, 32> indicesB{};
  std::array<uint8_t, 32> paletteA{};
  std::array<uint8_t, 32> paletteB{};
  auto objA = make_texture(indicesA.data(), 1, GX_TF_C4, 4, 4);
  auto objB = make_texture(indicesB.data(), 2, GX_TF_C4, 4, 4);
  auto tlutA = make_tlut(paletteA.data(), 1);
  auto tlutB = make_tlut(paletteB.data(), 2);

  const auto first = texture::resolve_static_palette_texture(objA, tlutA);
  const auto second = texture::resolve_static_palette_texture(objB, tlutB);
  EXPECT_EQ(first, second);
  EXPECT_EQ(testing::texture_allocations(), 1);

  paletteB[3] = 1;
  tlutB.tlutDataVersion = 2;
  const auto third = texture::resolve_static_palette_texture(objB, tlutB);
  EXPECT_NE(first, third);
  EXPECT_EQ(testing::texture_allocations(), 2);

  auto tlutC = make_tlut(paletteA.data(), 3);
  tlutC.format = GX_TL_RGB565;
  texture::resolve_static_palette_texture(make_texture(indicesA.data(), 3, GX_TF_C4, 4, 4), tlutC);
  EXPECT_EQ(testing::texture_allocations(), 3);
}

TEST_F(GxTextureCacheTest, StaticClearReresolvesBoundTextureButRetainsContent) {
  std::array<uint8_t, 16> pixels{};
  g_gxState.loadedTextures[0] = make_texture(pixels.data(), 1);
  ShaderInfo info{};
  info.sampledTextures.set(0);
  resolve_sampled_textures(info);
  const auto first = g_gxState.textures[0].ref;

  clear_static_texture_cache();
  resolve_sampled_textures(info);

  EXPECT_EQ(g_gxState.textures[0].ref, first);
  EXPECT_EQ(testing::texture_allocations(), 1);
  EXPECT_EQ(texture_stats().contentHits, 1);
}

TEST_F(GxTextureCacheTest, ReplacementAppearsOnAlreadyBoundTexture) {
  std::array<uint8_t, 16> pixels{};
  g_gxState.loadedTextures[0] = make_texture(pixels.data(), 1);
  ShaderInfo info{};
  info.sampledTextures.set(0);
  resolve_sampled_textures(info);
  const auto raw = g_gxState.textures[0].ref;

  const auto replacement = testing::make_texture_handle(8, 8);
  testing::set_replacement(replacement);
  clear_static_texture_cache();
  resolve_sampled_textures(info);

  EXPECT_NE(raw, replacement);
  EXPECT_EQ(g_gxState.textures[0].ref, replacement);
  EXPECT_EQ(texture_stats().replacementHits, 1);
}

TEST_F(GxTextureCacheTest, PendingReplacementPublishEvictsItsVanillaObjectEntry) {
  std::array<uint8_t, 16> pixels{};
  const auto obj = make_texture(pixels.data(), 1);
  testing::set_replacement({}, 77);
  const auto vanilla = texture::resolve_static_texture(obj);
  ASSERT_NE(vanilla, nullptr);

  const auto replacement = testing::make_texture_handle(8, 8);
  testing::set_replacement(replacement, 77);
  texture::invalidate_replacement(77);
  const auto resolved = texture::resolve_static_texture(obj);

  EXPECT_NE(vanilla, replacement);
  EXPECT_EQ(resolved, replacement);
  EXPECT_EQ(texture_stats().objectHits, 0);
}

TEST_F(GxTextureCacheTest, PendingReplacementPublishRebindsLoadedSlot) {
  std::array<uint8_t, 16> pixels{};
  g_gxState.loadedTextures[0] = make_texture(pixels.data(), 1);
  ShaderInfo info{};
  info.sampledTextures.set(0);
  testing::set_replacement({}, 91);
  resolve_sampled_textures(info);
  const auto vanilla = g_gxState.textures[0].ref;
  ASSERT_NE(vanilla, nullptr);

  const auto replacement = testing::make_texture_handle(8, 8);
  testing::set_replacement(replacement, 91);
  texture::invalidate_replacement(91);
  texture::invalidate_bindings();
  resolve_sampled_textures(info);

  EXPECT_NE(vanilla, replacement);
  EXPECT_EQ(g_gxState.textures[0].ref, replacement);
}

TEST_F(GxTextureCacheTest, SourceReplacementReusesContentHashPass) {
  std::array<uint8_t, 16> pixels{};
  pixels[4] = 7;
  const auto replacement = testing::make_texture_handle(8, 8);
  testing::set_source_replacement(
      {
          .textureHash = XXH64(pixels.data(), pixels.size(), 0),
          .width = 2,
          .height = 2,
          .format = GX_TF_RGBA8_PC,
          .hasTlut = false,
      },
      replacement);

  const auto resolved = texture::resolve_static_texture(make_texture(pixels.data(), 1));

  EXPECT_EQ(resolved, replacement);
  EXPECT_EQ(testing::texture_allocations(), 0);
  EXPECT_EQ(texture_stats().hashedBytes, pixels.size());
  EXPECT_EQ(texture_stats().replacementHits, 1);
}

TEST_F(GxTextureCacheTest, TlutRefreshInvalidatesBoundPaletteTexture) {
  std::array<uint8_t, 32> indices{};
  std::array<uint8_t, 32> palette{};
  g_gxState.loadedTextures[0] = make_texture(indices.data(), 1, GX_TF_C4, 4, 4);
  g_gxState.loadedTextures[0].tlut = GX_TLUT0;
  g_gxState.loadedTluts[0] = make_tlut(palette.data(), 1);
  ShaderInfo info{};
  info.sampledTextures.set(0);
  resolve_sampled_textures(info);
  const auto first = g_gxState.textures[0].ref;

  palette[1] = 1;
  ++g_gxState.loadedTluts[0].tlutDataVersion;
  texture::invalidate_bindings();
  resolve_sampled_textures(info);

  EXPECT_NE(g_gxState.textures[0].ref, first);
  EXPECT_EQ(testing::texture_allocations(), 2);
}

TEST_F(GxTextureCacheTest, CopyRevisionRequeuesDynamicPaletteConversion) {
  std::array<uint8_t, 32> palette{};
  const auto copy = testing::make_texture_handle(4, 4, GX_TF_C8);
  g_gxState.loadedTextures[0] = make_texture(palette.data(), 1, GX_TF_C8, 4, 4);
  g_gxState.loadedTextures[0].tlut = GX_TLUT0;
  g_gxState.loadedTluts[0] = make_tlut(palette.data(), 1, 256);
  g_gxState.copyTextures[palette.data()] = {.handle = copy, .revision = 1};
  ShaderInfo info{};
  info.sampledTextures.set(0);
  resolve_sampled_textures(info);
  EXPECT_EQ(testing::palette_conversions(), 1);

  ++g_gxState.copyTextures[palette.data()].revision;
  texture::invalidate_bindings();
  resolve_sampled_textures(info);

  EXPECT_EQ(testing::palette_conversions(), 2);
}

TEST_F(GxTextureCacheTest, CopyRecreationAtSameDestinationRebindsHandle) {
  std::array<uint8_t, 16> destination{};
  const auto first = testing::make_texture_handle(2, 2);
  const auto second = testing::make_texture_handle(2, 2);
  g_gxState.loadedTextures[0] = make_texture(destination.data(), 1);
  g_gxState.copyTextures[destination.data()] = {.handle = first, .revision = 1};
  ShaderInfo info{};
  info.sampledTextures.set(0);
  resolve_sampled_textures(info);
  EXPECT_EQ(g_gxState.textures[0].ref, first);

  g_gxState.copyTextures[destination.data()] = {.handle = second, .revision = 1};
  texture::invalidate_bindings();
  resolve_sampled_textures(info);

  EXPECT_EQ(g_gxState.textures[0].ref, second);
}

TEST_F(GxTextureCacheTest, ObjectAgingKeepsContentEntry) {
  std::array<uint8_t, 16> pixels{};
  const auto obj = make_texture(pixels.data(), 1);
  const auto first = texture::resolve_static_texture(obj);
  for (uint64_t i = 0; i <= texture::ObjectCacheIdleFrames; ++i) {
    texture::end_frame();
  }

  const auto second = texture::resolve_static_texture(obj);

  EXPECT_EQ(first, second);
  EXPECT_EQ(testing::texture_allocations(), 1);
  EXPECT_EQ(texture_stats().contentHits, 1);
  EXPECT_EQ(texture_stats().objectHits, 0);
}

TEST_F(GxTextureCacheTest, BoundObjectIsNotAgedOut) {
  std::array<uint8_t, 16> pixels{};
  g_gxState.loadedTextures[0] = make_texture(pixels.data(), 1);
  ShaderInfo info{};
  info.sampledTextures.set(0);
  resolve_sampled_textures(info);

  for (uint64_t i = 0; i <= texture::ObjectCacheIdleFrames; ++i) {
    texture::end_frame();
    resolve_sampled_textures(info);
  }
  texture::invalidate_bindings();
  resolve_sampled_textures(info);

  EXPECT_EQ(testing::texture_allocations(), 1);
  EXPECT_EQ(texture_stats().hashedBytes, 0);
  EXPECT_EQ(texture_stats().objectHits, 1);
}

TEST_F(GxTextureCacheTest, ContentCacheEvictsLeastRecentlyUsedEntry) {
  texture::set_content_cache_budget_for_testing(128);
  std::array<uint8_t, 64> a{};
  std::array<uint8_t, 64> b{};
  std::array<uint8_t, 64> c{};
  b[0] = 1;
  c[0] = 2;
  texture::resolve_static_texture(make_texture(a.data(), 1, GX_TF_RGBA8_PC, 4, 4));
  texture::resolve_static_texture(make_texture(b.data(), 2, GX_TF_RGBA8_PC, 4, 4));
  texture::resolve_static_texture(make_texture(a.data(), 3, GX_TF_RGBA8_PC, 4, 4));
  texture::resolve_static_texture(make_texture(c.data(), 4, GX_TF_RGBA8_PC, 4, 4));
  texture::resolve_static_texture(make_texture(b.data(), 5, GX_TF_RGBA8_PC, 4, 4));

  EXPECT_EQ(testing::texture_allocations(), 4);
  EXPECT_EQ(texture_stats().evictions, 2);
  EXPECT_EQ(texture_stats().contentCacheBytes, 128);
  EXPECT_EQ(texture_stats().contentCacheEntries, 2);
}

TEST_F(GxTextureCacheTest, OversizedEntryIsReturnedButNotRetained) {
  texture::set_content_cache_budget_for_testing(32);
  std::array<uint8_t, 64> pixels{};
  auto obj = make_texture(pixels.data(), 1, GX_TF_RGBA8_PC, 4, 4);
  obj.set_no_cache(true);

  EXPECT_NE(texture::resolve_static_texture(obj), nullptr);
  EXPECT_NE(texture::resolve_static_texture(obj), nullptr);

  EXPECT_EQ(testing::texture_allocations(), 2);
  EXPECT_EQ(texture_stats().contentCacheEntries, 0);
}
} // namespace
} // namespace aurora::gx

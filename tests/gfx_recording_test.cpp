#include <gtest/gtest.h>

#include "gfx/frame_packet.hpp"
#include "gfx/lru_cache.hpp"
#include "gfx/recording.hpp"
#include "gfx/texture.hpp"
#include "webgpu/gpu.hpp"

#include <algorithm>
#include <memory>

namespace aurora::gfx {
namespace {

constexpr auto ColorFormat = wgpu::TextureFormat::RGBA8Unorm;
constexpr auto DepthFormat = wgpu::TextureFormat::Depth24Plus;

class GfxRecordingTest : public ::testing::Test {
protected:
  void SetUp() override {
    webgpu::g_graphicsConfig.surfaceConfiguration.format = ColorFormat;
    webgpu::g_graphicsConfig.depthFormat = DepthFormat;
    webgpu::g_graphicsConfig.msaaSamples = 1;
    webgpu::g_frameBuffer.size = {640, 480, 1};
    webgpu::g_frameBuffer.format = ColorFormat;
    webgpu::g_depthBuffer.size = {640, 480, 1};
    webgpu::g_depthBuffer.format = DepthFormat;
    detail::testing::suppress_render_worker(true);
    detail::begin_recording(frame, 0);
  }

  void TearDown() override {
    if (recordingActive) {
      if (is_offscreen()) {
        end_offscreen();
      }
      finish();
      detail::end_recording();
    }
    detail::shutdown_recording();
  }

  void seed(uint32_t width, uint32_t height) {
    detail::testing::seed_offscreen_cache(width, height, ColorFormat, DepthFormat);
  }

  bool cached(uint32_t width, uint32_t height) {
    return detail::testing::is_offscreen_cached(width, height, ColorFormat, DepthFormat);
  }

  void next_frame() {
    finish();
    detail::end_recording();
    frame = {};
    detail::begin_recording(frame, 0);
  }

  void copy_current_offscreen() {
    const auto& pass = frame.renderPasses.back();
    const auto& size = pass.colorAttachments[SceneColorAttachmentIndex].size;
    auto target = std::make_shared<TextureRef>(wgpu::Texture{}, wgpu::TextureView{}, wgpu::TextureView{}, size,
                                               ColorFormat, 1, GX_TF_RGBA8);
    resolve_pass_into(std::move(target), {0, 0, static_cast<int32_t>(size.width), static_cast<int32_t>(size.height)},
                      false, false, false, {}, 1.f, GX_TF_RGBA8, GX_PF_RGBA6_Z24);
  }

  size_t count_efb_passes() const {
    return static_cast<size_t>(
        std::ranges::count_if(frame.renderPasses, [](const auto& pass) { return pass.label.starts_with("EFB"); }));
  }

  detail::FramePacket frame;
  bool recordingActive = true;
};

TEST_F(GfxRecordingTest, CreateRestoreReturnsToEfb) {
  seed(320, 180);
  begin_offscreen(320, 180);
  ASSERT_TRUE(is_offscreen());

  end_offscreen();

  EXPECT_FALSE(is_offscreen());
  ASSERT_EQ(frame.renderPasses.size(), 2u);
  EXPECT_TRUE(frame.renderPasses[0].sealed);
  EXPECT_TRUE(frame.renderPasses[0].discardable);
  EXPECT_EQ(count_efb_passes(), 1u);
}

TEST_F(GfxRecordingTest, EfbPassUsesDiscoveredSceneLayout) {
  ASSERT_FALSE(frame.renderPasses.empty());
  const auto discovered = scene_render_target_layout();
  const auto targetLayout = frame.renderPasses.front().target_layout();

  EXPECT_EQ(targetLayout.key, discovered.key);
  EXPECT_EQ(targetLayout.colorAttachmentCount, discovered.colorAttachmentCount);
  EXPECT_EQ(targetLayout.colorAttachments[SceneColorAttachmentIndex].semantic, ColorAttachmentSemantic::SceneColor);
}

TEST_F(GfxRecordingTest, CopiedOffscreenPassIsRetainedOnRestore) {
  seed(320, 180);
  begin_offscreen(320, 180);
  copy_current_offscreen();

  end_offscreen();

  ASSERT_EQ(frame.renderPasses.size(), 3u);
  EXPECT_TRUE(frame.renderPasses[0].sealed);
  EXPECT_FALSE(frame.renderPasses[0].discardable);
  EXPECT_TRUE(frame.renderPasses[0].has_consumer());
  EXPECT_TRUE(frame.renderPasses[1].sealed);
  EXPECT_TRUE(frame.renderPasses[1].discardable);
}

TEST_F(GfxRecordingTest, ReplacementRetainsCopiedPassesAndDiscardsContinuations) {
  seed(320, 180);
  seed(160, 90);
  begin_offscreen(320, 180);
  copy_current_offscreen();
  begin_offscreen(160, 90);
  copy_current_offscreen();

  end_offscreen();

  ASSERT_EQ(frame.renderPasses.size(), 5u);
  EXPECT_TRUE(frame.renderPasses[0].has_consumer());
  EXPECT_FALSE(frame.renderPasses[0].discardable);
  EXPECT_TRUE(frame.renderPasses[1].discardable);
  EXPECT_TRUE(frame.renderPasses[2].has_consumer());
  EXPECT_FALSE(frame.renderPasses[2].discardable);
  EXPECT_TRUE(frame.renderPasses[3].discardable);
  EXPECT_EQ(count_efb_passes(), 1u);
}

TEST_F(GfxRecordingTest, RepeatedUncopiedCreatesDiscardEarlierPasses) {
  seed(320, 180);
  seed(160, 90);
  begin_offscreen(320, 180);
  begin_offscreen(160, 90);
  end_offscreen();

  ASSERT_EQ(frame.renderPasses.size(), 3u);
  EXPECT_TRUE(frame.renderPasses[0].sealed);
  EXPECT_TRUE(frame.renderPasses[0].discardable);
  EXPECT_TRUE(frame.renderPasses[1].sealed);
  EXPECT_TRUE(frame.renderPasses[1].discardable);
  EXPECT_EQ(count_efb_passes(), 1u);
}

TEST_F(GfxRecordingTest, PublicCreatePassRejectsExistingOffscreenPass) {
  seed(320, 180);
  seed(160, 90);
  ASSERT_TRUE(create_pass(320, 180));

  EXPECT_FALSE(create_pass(160, 90));

  ResolvedTargets ignored;
  EXPECT_TRUE(resolve_pass({.color = false, .depth = false}, ignored));
}

TEST_F(GfxRecordingTest, FinalizedPassesAreSealedOrDeliberatelyDiscarded) {
  seed(320, 180);
  seed(160, 90);
  begin_offscreen(320, 180);
  copy_current_offscreen();
  begin_offscreen(160, 90);
  end_offscreen();
  finish();

  ASSERT_FALSE(frame.renderPasses.empty());
  for (const auto& pass : frame.renderPasses) {
    EXPECT_TRUE(pass.sealed);
    if (!pass.has_consumer() && pass.label.starts_with("Offscreen")) {
      EXPECT_TRUE(pass.discardable);
    }
  }
  detail::end_recording();
  recordingActive = false;
}

TEST_F(GfxRecordingTest, OffscreenBudgetIncludesColorAndDepth) {
  seed(2048, 2048); // 16 MiB color + 16 MiB depth
  seed(4096, 1024); // Another 32 MiB
  ASSERT_EQ(detail::testing::offscreen_cache_stats().bytes, RenderTextureCacheLimits.bytes);
  ASSERT_EQ(detail::testing::offscreen_cache_stats().entries, 2);

  begin_offscreen(2048, 2048); // Refresh the older entry within the same frame.
  end_offscreen();
  seed(1024, 4096);

  EXPECT_TRUE(cached(2048, 2048));
  EXPECT_FALSE(cached(4096, 1024));
  EXPECT_TRUE(cached(1024, 4096));
  EXPECT_EQ(detail::testing::offscreen_cache_stats().bytes, RenderTextureCacheLimits.bytes);
}

TEST_F(GfxRecordingTest, OffscreenBudgetBoundsContinuousSizeChanges) {
  for (uint32_t width = 256; width < 2048; ++width) {
    seed(width, 1024);
    begin_offscreen(width, 1024);
    end_offscreen();
    EXPECT_LE(detail::testing::offscreen_cache_stats().bytes, RenderTextureCacheLimits.bytes);
  }
  EXPECT_FALSE(cached(256, 1024));
  EXPECT_TRUE(cached(2047, 1024));
}

TEST_F(GfxRecordingTest, OversizedOffscreenDoesNotDisplaceReusableSizes) {
  seed(320, 240);
  seed(4096, 4096); // 128 MiB including depth
  EXPECT_TRUE(cached(320, 240));
  EXPECT_FALSE(cached(4096, 4096));
  EXPECT_EQ(detail::testing::offscreen_cache_stats().entries, 1);
}

TEST_F(GfxRecordingTest, OffscreenSweepKeepsHotSizesAndExpiresIdleOnes) {
  seed(320, 240);
  seed(160, 120);
  for (uint64_t i = 0; i < RenderTextureCacheLimits.idleFrames.value(); ++i) {
    begin_offscreen(320, 240);
    end_offscreen();
    next_frame();
  }
  EXPECT_TRUE(cached(160, 120)); // Exactly 32 frames old is still retained.
  for (uint64_t i = 0; i < RenderTextureCacheLimits.sweepFrames; ++i) {
    begin_offscreen(320, 240);
    end_offscreen();
    next_frame();
  }
  EXPECT_TRUE(cached(320, 240));
  EXPECT_FALSE(cached(160, 120));
  EXPECT_EQ(detail::testing::offscreen_cache_stats().entries, 1);
}

TEST_F(GfxRecordingTest, OffscreenReuseSurvivesEfbResizeCycles) {
  seed(320, 240);
  seed(640, 360);
  for (uint64_t i = 0; i < 4 * RenderTextureCacheLimits.idleFrames.value(); ++i) {
    const uint32_t width = i % 2 == 0 ? 640 : 1280;
    const uint32_t height = i % 2 == 0 ? 480 : 720;
    webgpu::g_frameBuffer.size = {width, height, 1};
    webgpu::g_depthBuffer.size = {width, height, 1};
    next_frame();
    ASSERT_TRUE(cached(width / 2, height / 2));
    begin_offscreen(width / 2, height / 2);
    const auto size = get_render_target_size();
    EXPECT_EQ(size.x, width / 2);
    EXPECT_EQ(size.y, height / 2);
    end_offscreen();
  }
  EXPECT_EQ(detail::testing::offscreen_cache_stats().entries, 2);
}

TEST_F(GfxRecordingTest, OffscreenKeyIncludesColorAndDepthFormats) {
  constexpr auto OtherColor = wgpu::TextureFormat::BGRA8Unorm;
  constexpr auto OtherDepth = wgpu::TextureFormat::Depth32Float;
  seed(320, 240);
  detail::testing::seed_offscreen_cache(320, 240, OtherColor, DepthFormat);
  detail::testing::seed_offscreen_cache(320, 240, OtherColor, OtherDepth);
  ASSERT_EQ(detail::testing::offscreen_cache_stats().entries, 3);
  EXPECT_EQ(detail::testing::offscreen_cache_stats().bytes, 3 * 320 * 240 * 8);

  for (const auto [color, depth] :
       {std::pair{ColorFormat, DepthFormat}, std::pair{OtherColor, DepthFormat}, std::pair{OtherColor, OtherDepth}}) {
    webgpu::g_graphicsConfig.surfaceConfiguration.format = color;
    webgpu::g_graphicsConfig.depthFormat = depth;
    begin_offscreen(320, 240);
    const auto layout = get_render_target_layout();
    EXPECT_EQ(layout.colorAttachments[SceneColorAttachmentIndex].format, color);
    EXPECT_EQ(layout.depthStencilFormat, depth);
    end_offscreen();
  }
}

} // namespace
} // namespace aurora::gfx

#include <gtest/gtest.h>

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.hpp>
#include <dolphin/gx.h>
#include <dolphin/vi.h>
#include <SDL3/SDL_init.h>

#include "gfx/efb.hpp"
#include "gfx/render_worker.hpp"
#include "imgui.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <semaphore>

using namespace aurora;
using namespace std::chrono_literals;

TEST(FrameBufferSizing, ScaleAndAspectPolicies) {
  const AuroraWindowSize display{
      .width = 1280,
      .height = 720,
      .native_fb_width = 2560,
      .native_fb_height = 1440,
      .scale = 2.f,
  };
  const auto fixed = gfx::efb::calculate_size(display, 640, 480, 2.f, false);
  EXPECT_EQ(fixed.fb_width, 1707u);
  EXPECT_EQ(fixed.fb_height, 960u);
  const auto fixedFit = gfx::efb::calculate_size(display, 640, 480, 2.f, true);
  EXPECT_EQ(fixedFit.fb_width, 1280u);
  EXPECT_EQ(fixedFit.fb_height, 960u);
  const auto automatic = gfx::efb::calculate_size(display, 640, 480, 0.f, false);
  EXPECT_EQ(automatic.fb_width, 2560u);
  EXPECT_EQ(automatic.fb_height, 1440u);
  const auto automaticFit = gfx::efb::calculate_size(display, 640, 480, 0.f, true);
  EXPECT_EQ(automaticFit.fb_width, 1920u);
  EXPECT_EQ(automaticFit.fb_height, 1440u);
}

class HeadlessFrameTest : public ::testing::Test {
protected:
  void SetUp() override {
    path = ::testing::TempDir() + "aurora-present-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(path);
    videoBefore = SDL_WasInit(SDL_INIT_VIDEO);
    const AuroraConfig config{
        .appName = "Aurora present tests",
        .userPath = path.c_str(),
        .cachePath = path.c_str(),
        .desiredBackend = BACKEND_NULL,
        .windowWidth = 1920,
        .windowHeight = 1080,
        .logLevel = LOG_ERROR,
        .surface =
            {
                .type = AURORA_SURFACE_HEADLESS,
            },
    };
    info = aurora_initialize(0, nullptr, &config);
    GXInit(nullptr, 0);
    GXRenderModeObj mode{};
    mode.fbWidth = 32;
    mode.efbHeight = 24;
    VIConfigure(&mode);
    aurora_update();
  }

  void TearDown() override {
    gfx::synchronize();
    VIConfigure(nullptr);
    aurora_shutdown();
    std::filesystem::remove_all(path);
  }

  AuroraInfo info{};
  std::string path;
  SDL_InitFlags videoBefore = 0;
};

TEST_F(HeadlessFrameTest, IgnoresSavedWindowSizeAndRendersWithoutSdlOrUi) {
  EXPECT_EQ(info.window, nullptr);
  EXPECT_EQ(aurora_get_window(), nullptr);
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), videoBefore);
  EXPECT_FALSE(imgui::is_initialized());
  EXPECT_EQ(gfx::xfb_target(), gfx::InvalidPresentTarget);
  EXPECT_EQ(aurora_get_window_size().fb_width, 32u);
  EXPECT_EQ(aurora_get_window_size().fb_height, 24u);
  ASSERT_TRUE(aurora_begin_frame());
  VISetFrameBufferScale(2.f);
  // A frame's dimensions stay fixed during recording.
  EXPECT_EQ(aurora_get_window_size().fb_width, 32u);
  aurora_end_frame();
  ASSERT_TRUE(aurora_begin_frame());
  EXPECT_EQ(aurora_get_window_size().fb_width, 64u);
  EXPECT_EQ(aurora_get_window_size().fb_height, 48u);
  aurora_end_frame();
}

TEST_F(HeadlessFrameTest, ResizeEventsAreObservedWhileGettersRemainApplied) {
  ASSERT_TRUE(aurora_begin_frame());
  aurora_set_surface_size(200, 150, 2.f);
  EXPECT_EQ(aurora_get_window_size().native_fb_width, 32u);
  const auto* events = aurora_update();
  bool resized = false;
  bool scaled = false;
  for (; events->type != AURORA_NONE; ++events) {
    if (events->type == AURORA_WINDOW_RESIZED || events->type == AURORA_DISPLAY_SCALE_CHANGED) {
      EXPECT_EQ(events->windowSize.width, 100u);
      EXPECT_EQ(events->windowSize.height, 75u);
      EXPECT_EQ(events->windowSize.fb_width, 200u);
      EXPECT_EQ(events->windowSize.fb_height, 150u);
      EXPECT_EQ(events->windowSize.scale, 2.f);
      resized |= events->type == AURORA_WINDOW_RESIZED;
      scaled |= events->type == AURORA_DISPLAY_SCALE_CHANGED;
    }
  }
  EXPECT_TRUE(resized);
  EXPECT_TRUE(scaled);
  aurora_end_frame();
  ASSERT_TRUE(aurora_begin_frame());
  EXPECT_EQ(aurora_get_window_size().fb_width, 200u);
  EXPECT_EQ(aurora_get_window_size().fb_height, 150u);
  aurora_end_frame();
}

TEST_F(HeadlessFrameTest, UnchangedSurfaceSizeDoesNotQueueEvents) {
  aurora_set_surface_size(200, 150, 2.f);
  aurora_update();

  aurora_set_surface_size(200, 150, 2.f);
  for (const auto* event = aurora_update(); event->type != AURORA_NONE; ++event) {
    EXPECT_NE(event->type, AURORA_WINDOW_RESIZED);
    EXPECT_NE(event->type, AURORA_DISPLAY_SCALE_CHANGED);
  }
}

TEST_F(HeadlessFrameTest, ViewportPolicySetAfterUpdateIsAppliedBeforeRecording) {
  aurora_set_surface_size(200, 100, 2.f);
  aurora_update();
  AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);

  ASSERT_TRUE(aurora_begin_frame());
  EXPECT_EQ(aurora_get_window_size().fb_width, 133u);
  EXPECT_EQ(aurora_get_window_size().fb_height, 100u);
  aurora_end_frame();

  AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
  ASSERT_TRUE(aurora_begin_frame());
  EXPECT_EQ(aurora_get_window_size().fb_width, 200u);
  EXPECT_EQ(aurora_get_window_size().fb_height, 100u);
  aurora_end_frame();
}

TEST_F(HeadlessFrameTest, ResizeDoesNotDrainBlockedSceneWork) {
  struct Barrier {
    std::binary_semaphore entered{0};
    std::binary_semaphore release{0};
  } barrier;
  const auto task = gfx::register_encoder_task_type({
      .label = "Pause scene encoding",
      .callback =
          [](const gfx::EncoderTaskContext&, const wgpu::CommandEncoder&, const void*, size_t, void* data) {
            auto& barrier = *static_cast<Barrier*>(data);
            barrier.entered.release();
            barrier.release.acquire();
          },
      .userdata = &barrier,
  });
  ASSERT_TRUE(aurora_begin_frame());
  const auto old = gfx::efb::applied_state();
  ASSERT_TRUE(gfx::push_encoder_task(task, nullptr, 0));
  const bool entered = barrier.entered.try_acquire_for(5s);
  EXPECT_TRUE(entered);
  auto resize = std::async(std::launch::async, [] { aurora_set_surface_size(128, 80, 2.f); });
  const bool deferred = resize.wait_for(500ms) == std::future_status::ready;
  EXPECT_TRUE(deferred);
  if (!deferred || !entered) {
    barrier.release.release();
  }
  resize.get();
  aurora_end_frame();
  ASSERT_TRUE(aurora_begin_frame());
  const auto next = gfx::efb::applied_state();
  EXPECT_EQ(next.color.size.width, 128u);
  EXPECT_EQ(next.color.size.height, 80u);
  EXPECT_EQ(old.color.texture.GetWidth(), 32u);
  EXPECT_EQ(old.color.texture.GetHeight(), 24u);
  EXPECT_NE(next.color.view.Get(), old.color.view.Get());
  aurora_end_frame();
  if (deferred && entered) {
    barrier.release.release();
  }
  gfx::synchronize();
  gfx::unregister_encoder_task_type(task);
}

TEST_F(HeadlessFrameTest, InvalidTargetsAndHeadlessDetachAreSafe) {
  EXPECT_EQ(gfx::create_present_target({}), gfx::InvalidPresentTarget);
  EXPECT_FALSE(gfx::set_xfb_target(0x12345678));
  EXPECT_EQ(gfx::present_target_status(0x12345678), gfx::PresentTargetStatus::Lost);
  gfx::destroy_present_target(0x12345678);
  ASSERT_TRUE(aurora_begin_frame());
  EXPECT_FALSE(gfx::present_to(gfx::InvalidPresentTarget));
  EXPECT_TRUE(aurora_set_surface(nullptr));
  aurora_end_frame();
}

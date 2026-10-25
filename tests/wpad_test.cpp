#include <gtest/gtest.h>
#include <aurora/wpad.hpp>
#include <revolution/kpad.h>
#include "revolution/wpad/backend.hpp"
#include <dolphin/pad.h>

#include <cmath>
#include <cstddef>
#include <vector>

using namespace aurora;
namespace {

uint64_t clockNs;
uint64_t clock_now() { return clockNs; }
std::vector<std::pair<s32, s32>> connections, completions;
void connected(s32 channel, s32 result) { connections.emplace_back(channel, result); }
void completed(s32 channel, s32 result) { completions.emplace_back(channel, result); }

struct Provider : wpad::ChannelProvider {
  wpad::ChannelSample state;
  Provider() {
    state.core.dev = WPAD_DEV_FREESTYLE;
    state.core.err = WPAD_ERR_NONE;
    state.core.accZ = 106;
    state.fs.fsAccZ = 204;
    state.synthesizedPointer = true;
  }

  wpad::ChannelSample sample(uint32_t, uint64_t) override { return state; }
};

class WpadTest : public testing::Test {
protected:
  std::shared_ptr<Provider> provider;
  void SetUp() override {
    wpad::shutdown();
    PADBlockInput(false);
    connections.clear();
    completions.clear();
    clockNs = 1'000'000'000;
    wpad::detail::set_clock(clock_now);
    KPADInit();
    provider = std::make_shared<Provider>();
    wpad::set_channel_provider(0, provider);
    tick(0);
    KPADStatus discard[120];
    KPADRead(0, discard, 120);
  }

  void TearDown() override {
    wpad::shutdown();
    wpad::detail::set_clock(nullptr);
  }

  void tick(uint64_t delta = 5'000'000) {
    clockNs += delta;
    wpad::detail::update_at(clockNs);
  }

  KPADStatus latest() {
    KPADStatus result{};
    EXPECT_EQ(KPADRead(0, &result, 1), 1);
    return result;
  }
};

TEST_F(WpadTest, PublicReportLayoutsAndCalibrationMatchSdk) {
  EXPECT_EQ(sizeof(WPADStatus), 42u);
  EXPECT_EQ(sizeof(WPADFSStatus), 50u);
  EXPECT_EQ(sizeof(WPADCLStatus), 54u);
  EXPECT_EQ(sizeof(KPADStatus), 132u);
  EXPECT_EQ(offsetof(KPADStatus, ex_status), 96u);
  WPADAcc gravity{};
  WPADGetAccGravityUnit(0, WPAD_DEV_CORE, &gravity);
  EXPECT_EQ(gravity.x, 106);
  WPADGetAccGravityUnit(0, WPAD_DEV_FS, &gravity);
  EXPECT_EQ(gravity.x, 204);
  EXPECT_EQ(WPADProbe(-1, nullptr), WPAD_ERR_INVALID);
}

TEST_F(WpadTest, FixedClockAccumulatesZeroSampleReadsAndCapsHitches) {
  KPADStatus samples[120];
  for (int i = 0; i < 4; ++i) {
    tick(1'000'000);
    EXPECT_EQ(KPADRead(0, samples, 120), 0);
  }

  tick(1'000'000);
  EXPECT_EQ(KPADRead(0, samples, 120), 1);
  tick(16'000'000);
  EXPECT_EQ(KPADRead(0, samples, 120), 3);
  tick(4'000'000);
  EXPECT_EQ(KPADRead(0, samples, 120), 1);
  tick(1'000'000'000);
  EXPECT_EQ(KPADRead(0, samples, 120), 16);
}

TEST_F(WpadTest, BatchEdgesAreCopiedToEverySampleAndZeroReadsDoNotConsumeThem) {
  provider->state.core.button = WPAD_BUTTON_A;
  tick(1'000'000);
  KPADStatus samples[16];
  EXPECT_EQ(KPADRead(0, samples, 16), 0);
  tick(19'000'000);
  ASSERT_EQ(KPADRead(0, samples, 16), 4);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(samples[i].trig, WPAD_BUTTON_A);
    EXPECT_EQ(samples[i].hold, WPAD_BUTTON_A);
  }

  tick();
  EXPECT_EQ(latest().trig, 0u);
  provider->state.core.button = 0;
  tick(15'000'000);
  ASSERT_EQ(KPADRead(0, samples, 16), 3);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(samples[i].release, WPAD_BUTTON_A);
  }
}

TEST_F(WpadTest, UnifiedStatusMatchesLastReadAndIsNonDestructive) {
  provider->state.core.accX = -106;
  tick(15'000'000);
  KPADStatus samples[16];
  ASSERT_EQ(KPADRead(0, samples, 16), 3);
  KPADUnifiedWpadStatus raw[16]{};
  KPADGetUnifiedWpadStatus(0, raw, 16);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(raw[i].u.fs.accX, -106);
    EXPECT_EQ(raw[i].fmt, WPAD_FMT_FREESTYLE_ACC_DPD);
  }

  provider->state.core.accX = 0;
  tick();
  KPADUnifiedWpadStatus again[16]{};
  KPADGetUnifiedWpadStatus(0, again, 16);
  EXPECT_EQ(again[0].u.fs.accX, -106);
  EXPECT_EQ(latest().acc.x, 0.f);
}

TEST_F(WpadTest, PointerDistanceIgnoresSensorHeightForSimulatedPointers) {
  provider->state.pointer = {0.25f, -0.75f, TRUE};
  provider->state.distance = 4.f;
  KPADSetSensorHeight(0, 0.5f);
  tick();
  const auto status = latest();
  EXPECT_FLOAT_EQ(status.pos.x, 0.25f);
  EXPECT_FLOAT_EQ(status.pos.y, -0.75f);
  EXPECT_FLOAT_EQ(status.dist, 4.f);
  EXPECT_EQ(status.dpd_valid_fg, 2);
}

TEST_F(WpadTest, OverridesUseHardwareUnitsAndKeepReferencePoseSigns) {
  Vec core{-1, 0, 0}, fs{0, 0, 1};
  aurora_wpad_set_acceleration(0, &core, &fs);
  tick();
  auto status = latest();
  EXPECT_FLOAT_EQ(status.acc.x, -1.f);
  EXPECT_FLOAT_EQ(status.horizon.y, -1.f);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc.z, 1.f);
  KPADUnifiedWpadStatus raw{};
  KPADGetUnifiedWpadStatus(0, &raw, 1);
  EXPECT_EQ(raw.u.fs.accX, 106);
  EXPECT_EQ(raw.u.fs.fsAccY, 204);
}

TEST_F(WpadTest, CallbacksAndDisconnectAreDeferredAndConnectionAcceptanceIsRespected) {
  WPADSetConnectCallback(0, connected);
  tick();
  ASSERT_EQ(connections.size(), 1u);
  EXPECT_EQ(connections[0].second, WPAD_ERR_NONE);
  WPADInfo info{};
  EXPECT_EQ(WPADGetInfoAsync(0, &info, completed), WPAD_ERR_NONE);
  EXPECT_TRUE(completions.empty());
  tick(0);
  EXPECT_EQ(completions.size(), 1u);
  WPADDisconnect(0);
  tick();
  EXPECT_EQ(WPADProbe(0, nullptr), WPAD_ERR_NO_CONTROLLER);
  WPADSetAcceptConnection(FALSE);
  aurora_wpad_reconnect(0);
  tick();
  EXPECT_EQ(WPADProbe(0, nullptr), WPAD_ERR_NO_CONTROLLER);
  WPADSetAcceptConnection(TRUE);
  tick();
  EXPECT_EQ(WPADProbe(0, nullptr), WPAD_ERR_NONE);
}
} // namespace

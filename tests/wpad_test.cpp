#include <aurora/wpad.h>
#include <revolution/kpad.h>
#include "revolution/wpad/backend.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace {
std::array<aurora::wpad::HostState, 4> host;
double clockTime;
void (*updateHook)();
void (*shutdownHook)();
std::vector<std::pair<s32, s32>> connections, extensions, results;
std::vector<std::pair<u32, u32>> motors;
float battery;

void connected(s32 chan, s32 value) {
	connections.emplace_back(chan, value);
}

void extended(s32 chan, s32 value) {
	extensions.emplace_back(chan, value);
}

void completed(s32 chan, s32 value) {
	results.emplace_back(chan, value);
}

void advance(double seconds = 1.0 / 60) {
	clockTime += seconds;
	aurora_wpad_update();
}

KPADStatus latest(s32 chan = 0) {
	KPADStatus sample{};
	EXPECT_EQ(KPADRead(chan, &sample, 1), 1);

	return sample;
}

class WpadTest : public testing::Test {
	void SetUp() override {
		aurora_wpad_shutdown();
		host = {};

		for (auto& state : host) {
			state.pad.err = PAD_ERR_NO_CONTROLLER;
		}

		clockTime = 0;
		battery = 1;
		connections.clear();
		extensions.clear();
		results.clear();
		motors.clear();
	}

	void TearDown() override {
		aurora_wpad_shutdown();
	}
};
} // namespace

namespace aurora::wpad {
std::array<HostState, WPAD_MAX_CONTROLLERS> read_host() {
	return host;
}

void install_hooks(void (*update)(), void (*shutdown)()) {
	updateHook = update;
	shutdownHook = shutdown;
}

double now() {
	return clockTime;
}
} // namespace aurora::wpad

BOOL PADInit() {
	return TRUE;
}

void PADControlMotor(u32 chan, u32 command) {
	motors.emplace_back(chan, command);
}

PADBatteryState PADGetBatteryState(u32, f32* percent) {
	*percent = battery;

	return PAD_BATTERYSTATE_ON_BATTERY;
}

TEST_F(WpadTest, PublicReportLayoutsMatchSdk) {
	EXPECT_EQ(sizeof(WPADStatus), 42u);
	EXPECT_EQ(sizeof(WPADFSStatus), 50u);
	EXPECT_EQ(sizeof(WPADCLStatus), 54u);
	EXPECT_EQ(sizeof(WPADStatusEx), 90u);
	EXPECT_EQ(sizeof(KPADStatus), 132u);
	EXPECT_EQ(offsetof(KPADStatus, ex_status), 96u);
}

TEST_F(WpadTest, LifecycleAndInvalidChannels) {
	EXPECT_EQ(WPADGetStatus(), WPAD_STATE_DISABLED);
	WPADStatus report{};
	WPADRead(0, &report);
	EXPECT_EQ(report.err, WPAD_ERR_NO_CONTROLLER);
	KPADInit();
	KPADInit();
	EXPECT_EQ(WPADGetStatus(), WPAD_STATE_SETUP);
	ASSERT_NE(updateHook, nullptr);
	ASSERT_NE(shutdownHook, nullptr);
	u32 device = 0;
	EXPECT_EQ(WPADProbe(-1, &device), WPAD_ERR_INVALID);
	EXPECT_EQ(device, WPAD_DEV_NOT_FOUND);
	EXPECT_EQ(WPADProbe(4, nullptr), WPAD_ERR_INVALID);
	EXPECT_EQ(WPADSetDataFormat(4, 0), WPAD_ERR_INVALID);
	EXPECT_EQ(WPADSetDataFormat(0, 99), WPAD_ERR_INVALID);
	EXPECT_EQ(KPADRead(-1, nullptr, 5), 0);
	WPADRead(-1, &report);
	EXPECT_EQ(report.err, WPAD_ERR_INVALID);
	WPADControlMotor(-1, WPAD_MOTOR_RUMBLE);
	EXPECT_TRUE(motors.empty());
	shutdownHook();
	EXPECT_EQ(updateHook, nullptr);
	EXPECT_EQ(WPADGetStatus(), WPAD_STATE_DISABLED);
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	EXPECT_EQ(latest().wpad_err, WPAD_ERR_NONE);
}

TEST_F(WpadTest, ConnectsAllPortsAndReportsExtensionWithoutRepeatedCallbacks) {
	KPADInit();

	for (s32 i = 0; i < 4; ++i) {
		host[i].pad.err = PAD_ERR_NONE;
		WPADSetConnectCallback(i, connected);
		WPADSetExtensionCallback(i, extended);
	}

	advance();
	ASSERT_EQ(connections.size(), 4u);
	ASSERT_EQ(extensions.size(), 4u);

	for (s32 i = 0; i < 4; ++i) {
		EXPECT_EQ(connections[i], std::make_pair(i, WPAD_ERR_NONE));
		EXPECT_EQ(extensions[i], std::make_pair(i, WPAD_DEV_FREESTYLE));
		EXPECT_EQ(latest(i).dev_type, WPAD_DEV_FREESTYLE);
	}

	advance();
	EXPECT_EQ(connections.size(), 4u);
	EXPECT_EQ(extensions.size(), 4u);
	aurora_wpad_set_device(1, WPAD_DEV_CLASSIC);
	advance();
	ASSERT_EQ(extensions.size(), 5u);
	EXPECT_EQ(extensions.back(), std::make_pair(1, WPAD_DEV_CLASSIC));
	EXPECT_EQ(latest(1).data_format, WPAD_FMT_CLASSIC_ACC_DPD);
}

TEST_F(WpadTest, CallbackRegistrationOnConnectedPortIsDeferred) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	WPADSetConnectCallback(0, connected);
	WPADSetExtensionCallback(0, extended);
	EXPECT_TRUE(connections.empty());
	EXPECT_TRUE(extensions.empty());
	advance();
	EXPECT_EQ(connections.size(), 1u);
	EXPECT_EQ(extensions.size(), 1u);
}

TEST_F(WpadTest, RemoteAndNunchukMappingAndButtonEdges) {
	KPADInit();
	auto& pad = host[0].pad;
	pad.err = PAD_ERR_NONE;
	pad.button = PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y | PAD_BUTTON_START | PAD_BUTTON_LEFT |
				 PAD_TRIGGER_Z | PAD_TRIGGER_L;
	pad.extButton = PAD_BUTTON_BACK | PAD_BUTTON_GUIDE;
	pad.stickX = 127;
	advance();
	const auto sample = latest();
	const u32 expected = WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_1 | WPAD_BUTTON_2 | WPAD_BUTTON_PLUS |
						 WPAD_BUTTON_LEFT | WPAD_BUTTON_Z | WPAD_BUTTON_C | WPAD_BUTTON_MINUS | WPAD_BUTTON_HOME;
	EXPECT_EQ(sample.hold, expected);
	EXPECT_EQ(sample.trig, expected);
	EXPECT_EQ(sample.release, 0u);
	EXPECT_FLOAT_EQ(sample.ex_status.fs.stick.x, 1);
	EXPECT_FLOAT_EQ(sample.ex_status.fs.acc.y, -1);
	advance();
	EXPECT_EQ(latest().trig, 0u);
	pad.button = 0;
	pad.extButton = 0;
	advance();
	EXPECT_EQ(latest().release, expected);
}

TEST_F(WpadTest, SamplesAreNewestFirstAndReadDoesNotResample) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	host[0].pad.button = PAD_BUTTON_A;
	advance();
	host[0].pad.button = 0;
	advance();
	std::array<KPADStatus, 4> samples{};
	EXPECT_EQ(KPADRead(0, samples.data(), 4), 2);
	EXPECT_EQ(samples[0].release, WPAD_BUTTON_A);
	EXPECT_EQ(samples[1].trig, WPAD_BUTTON_A);
	EXPECT_EQ(KPADRead(0, samples.data(), 4), 0);

	for (int i = 0; i < 150; ++i) {
		advance();
	}

	std::array<KPADStatus, 125> many{};
	EXPECT_EQ(KPADRead(0, many.data(), many.size()), 120);
}

TEST_F(WpadTest, NullOrZeroReadsPreservePendingSamplesAndSmallReadDiscardsOlderOnes) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	advance();
	KPADStatus sample{};
	EXPECT_EQ(KPADRead(0, nullptr, 1), 0);
	EXPECT_EQ(KPADRead(0, &sample, 0), 0);
	EXPECT_EQ(KPADRead(0, &sample, 1), 1);
	EXPECT_EQ(KPADRead(0, &sample, 1), 0);
}

TEST_F(WpadTest, RepeatUsesElapsedTimeAndDoesNotChangeTriggers) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	KPADSetBtnRepeat(0, 0.5f, 0.1f);
	host[0].pad.button = PAD_BUTTON_UP;
	advance();
	EXPECT_NE(latest().hold & KPAD_BUTTON_RPT, 0u);
	advance(0.4);
	EXPECT_EQ(latest().hold & KPAD_BUTTON_RPT, 0u);
	advance(0.11);
	const auto sample = latest();
	EXPECT_NE(sample.hold & KPAD_BUTTON_RPT, 0u);
	EXPECT_EQ(sample.trig, 0u);
	KPADSetBtnRepeat(0, 0, 0);
	advance(1000);
	EXPECT_EQ(latest().hold & KPAD_BUTTON_RPT, 0u);
}

TEST_F(WpadTest, DisconnectClearsStateAndRequiresReconnect) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	host[0].pad.button = PAD_BUTTON_A;
	WPADSetConnectCallback(0, connected);
	advance();
	latest();
	WPADDisconnect(0);
	advance();
	auto sample = latest();
	EXPECT_EQ(sample.wpad_err, WPAD_ERR_NO_CONTROLLER);
	EXPECT_EQ(sample.release, WPAD_BUTTON_A);
	EXPECT_EQ(sample.hold, 0u);
	EXPECT_EQ(connections.back().second, WPAD_ERR_NO_CONTROLLER);
	advance();
	EXPECT_EQ(KPADRead(0, &sample, 1), 0);
	EXPECT_EQ(WPADProbe(0, nullptr), WPAD_ERR_NO_CONTROLLER);
	aurora_wpad_reconnect(0);
	advance();
	EXPECT_EQ(latest().trig, WPAD_BUTTON_A);
	WPADDisconnect(0);
	advance();
	latest();
	host[0].pad.err = PAD_ERR_NO_CONTROLLER;
	advance();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	EXPECT_EQ(latest().wpad_err, WPAD_ERR_NONE);
}

TEST_F(WpadTest, AsyncInfoIsDeferredAndReportsDisconnect) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	WPADInfo info{};
	battery = 0.1f;
	EXPECT_EQ(WPADGetInfoAsync(0, &info, completed), WPAD_ERR_NONE);
	EXPECT_TRUE(results.empty());
	EXPECT_EQ(info.battery, 0);
	advance();
	EXPECT_EQ(info.battery, WPAD_BATTERY_LEVEL_LOW);
	EXPECT_TRUE(info.lowBat);
	EXPECT_TRUE(info.attach);
	EXPECT_FALSE(info.speaker);
	EXPECT_EQ(results.back().second, WPAD_ERR_NONE);
	WPADGetInfoAsync(0, &info, completed);
	host[0].pad.err = PAD_ERR_NO_CONTROLLER;
	advance();
	EXPECT_EQ(results.back().second, WPAD_ERR_NO_CONTROLLER);
}

TEST_F(WpadTest, UnsupportedSpeakerAndRumbleForwarding) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	WPADControlMotor(0, WPAD_MOTOR_RUMBLE);
	EXPECT_EQ(motors.back(), std::make_pair(0u, 1u));
	EXPECT_FALSE(WPADIsSpeakerEnabled(0));
	EXPECT_FALSE(WPADCanSendStreamData(0));
	EXPECT_EQ(WPADControlSpeaker(0, 1, completed), WPAD_ERR_INVALID);
	EXPECT_EQ(WPADSendStreamData(0, nullptr, 20), WPAD_ERR_INVALID);
	EXPECT_TRUE(results.empty());
	advance();
	EXPECT_EQ(results.back().second, WPAD_ERR_INVALID);
}

TEST_F(WpadTest, PointerFilteringValidityAndAccelerationAxes) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	AuroraWpadPointer pointer{-1, -1, TRUE};
	Vec accel{0.5f, -1, 0.25f};
	aurora_wpad_set_pointer(0, &pointer);
	aurora_wpad_set_acceleration(0, &accel, &accel);
	advance();
	auto sample = latest();
	EXPECT_EQ(sample.dpd_valid_fg, 2);
	EXPECT_FLOAT_EQ(sample.pos.x, -1);
	EXPECT_FLOAT_EQ(sample.pos.y, -1);
	EXPECT_FLOAT_EQ(sample.acc.x, 0.5f);
	EXPECT_FLOAT_EQ(sample.acc.y, -1);
	EXPECT_FLOAT_EQ(sample.acc.z, 0.25f);
	WPADFSStatus raw{};
	WPADRead(0, &raw);
	EXPECT_EQ(raw.accX, -128);
	EXPECT_EQ(raw.accY, 64);
	EXPECT_EQ(raw.accZ, 256);
	KPADSetPosParam(0, 0, 0.5f);
	pointer = {1, 1, TRUE};
	aurora_wpad_set_pointer(0, &pointer);
	advance();
	sample = latest();
	EXPECT_FLOAT_EQ(sample.pos.x, 0);
	EXPECT_FLOAT_EQ(sample.pos.y, 0);
	pointer.valid = FALSE;
	aurora_wpad_set_pointer(0, &pointer);
	advance();
	EXPECT_EQ(latest().dpd_valid_fg, 0);
	pointer = {std::numeric_limits<float>::quiet_NaN(), 0, TRUE};
	aurora_wpad_set_pointer(0, &pointer);
	advance();
	EXPECT_EQ(latest().dpd_valid_fg, 0);
}

TEST_F(WpadTest, BlockedInputNeutralizesPointerAndMotionOverrides) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	host[0].pad.button = PAD_BUTTON_A;
	AuroraWpadPointer pointer{0.5f, 0.5f, TRUE};
	Vec accel{3, 2, 1};
	aurora_wpad_set_pointer(0, &pointer);
	aurora_wpad_set_acceleration(0, &accel, &accel);
	advance();
	latest();
	host[0].blocked = true;
	advance();
	auto sample = latest();
	EXPECT_EQ(sample.hold, 0u);
	EXPECT_EQ(sample.dpd_valid_fg, 0);
	EXPECT_FLOAT_EQ(sample.acc.y, -1);
	EXPECT_FLOAT_EQ(sample.ex_status.fs.acc.y, -1);
}

TEST_F(WpadTest, ClassicReportAndExtensionSwitchResetUnion) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	aurora_wpad_set_device(0, WPAD_DEV_CLASSIC);
	auto& pad = host[0].pad;
	pad.button = PAD_BUTTON_X | PAD_TRIGGER_R;
	pad.stickX = 127;
	pad.substickY = -127;
	pad.triggerRight = 255;
	advance();
	auto sample = latest();
	EXPECT_EQ(sample.ex_status.cl.hold, WPAD_CL_BUTTON_X | WPAD_CL_TRIGGER_R);
	EXPECT_EQ(sample.ex_status.cl.trig, sample.ex_status.cl.hold);
	EXPECT_FLOAT_EQ(sample.ex_status.cl.lstick.x, 1);
	EXPECT_FLOAT_EQ(sample.ex_status.cl.rstick.y, -1);
	EXPECT_FLOAT_EQ(sample.ex_status.cl.rtrigger, 1);
	WPADCLStatus raw{};
	WPADRead(0, &raw);
	EXPECT_EQ(raw.clLStickX, 308);
	EXPECT_EQ(raw.clRStickY, -308);
	EXPECT_EQ(raw.clTriggerR, 180);
	aurora_wpad_set_device(0, WPAD_DEV_CORE);
	advance();
	sample = latest();
	EXPECT_EQ(sample.dev_type, WPAD_DEV_CORE);
	EXPECT_FLOAT_EQ(sample.ex_status.fs.stick.x, 0);
}

TEST_F(WpadTest, ReportsHonorFormatSizeAndSamplingBufferBoundaries) {
	WPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();

	struct {
		WPADStatus status;
		u32 canary = 0xcafebabe;
	} core;

	WPADRead(0, &core.status);
	EXPECT_EQ(core.canary, 0xcafebabeu);
	ASSERT_EQ(WPADSetDataFormat(0, WPAD_FMT_FREESTYLE_ACC_DPD), WPAD_ERR_NONE);

	struct {
		std::array<WPADFSStatus, 2> samples;
		u32 canary = 0xfeedface;
	} buffer{};

	WPADSetAutoSamplingBuf(0, buffer.samples.data(), 2);
	host[0].pad.button = PAD_BUTTON_A;
	advance();
	host[0].pad.button = PAD_BUTTON_B;
	advance();
	EXPECT_EQ(buffer.samples[0].button, WPAD_BUTTON_A);
	EXPECT_EQ(buffer.samples[1].button, WPAD_BUTTON_B);
	host[0].pad.button = PAD_BUTTON_X;
	advance();
	EXPECT_EQ(buffer.samples[0].button, WPAD_BUTTON_1);
	EXPECT_EQ(buffer.canary, 0xfeedfaceu);
	WPADSetDataFormat(0, WPAD_FMT_CORE_ACC_DPD_FULL);
	advance();
	EXPECT_EQ(buffer.samples[0].button, WPAD_BUTTON_1);
	EXPECT_EQ(buffer.canary, 0xfeedfaceu);
}

TEST_F(WpadTest, ExplicitDataFormatIsNotOverriddenEveryUpdate) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	latest();
	WPADSetDataFormat(0, WPAD_FMT_CORE);
	advance();
	EXPECT_EQ(latest().data_format, WPAD_FMT_CORE);
	advance();
	EXPECT_EQ(latest().data_format, WPAD_FMT_CORE);
}

TEST_F(WpadTest, ResetDropsBufferedStateButPreservesConfiguration) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	KPADSetBtnRepeat(0, 0.5f, 0.1f);
	host[0].pad.button = PAD_BUTTON_A;
	advance();
	KPADReset();
	KPADStatus sample{};
	EXPECT_EQ(KPADRead(0, &sample, 1), 0);
	advance();
	sample = latest();
	EXPECT_EQ(sample.trig, WPAD_BUTTON_A);
	EXPECT_NE(sample.hold & KPAD_BUTTON_RPT, 0u);
}

TEST_F(WpadTest, ConnectionCallbackCanInstallExtensionCallbackAndQueueInfo) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	WPADSetConnectCallback(0, [](s32 chan, s32 reason) {
		connected(chan, reason);

		if (reason == WPAD_ERR_NONE) {
			WPADSetExtensionCallback(chan, extended);
			WPADControlDpd(chan, 0, completed);
			aurora_wpad_update();
		}
	});
	advance();
	EXPECT_EQ(connections.size(), 1u);
	EXPECT_EQ(extensions.size(), 1u);
	EXPECT_TRUE(results.empty());
	EXPECT_FALSE(WPADIsDpdEnabled(0));
	advance();
	EXPECT_EQ(results.size(), 1u);
	EXPECT_EQ(connections.size(), 1u);
}

TEST_F(WpadTest, SamplingCallbackReadsCurrentRawReportWithoutConsumingKpad) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	host[0].pad.button = PAD_BUTTON_A;
	WPADSetSamplingCallback(0, [](s32 chan) {
		WPADFSStatus raw{};
		WPADRead(chan, &raw);
		EXPECT_EQ(raw.button, WPAD_BUTTON_A);
		completed(chan, raw.err);
	});
	advance();
	EXPECT_EQ(results.size(), 1u);
	EXPECT_EQ(latest().trig, WPAD_BUTTON_A);
	EXPECT_NE(WPADSetSamplingCallback(0, nullptr), nullptr);
}

TEST_F(WpadTest, ShutdownFromCallbackCancelsPendingCompletions) {
	KPADInit();
	host[0].pad.err = PAD_ERR_NONE;
	advance();
	latest();
	WPADInfo info{};
	WPADGetInfoAsync(0, &info, completed);
	WPADSetSamplingCallback(0, [](s32) { aurora_wpad_shutdown(); });
	advance();
	EXPECT_TRUE(results.empty());
	EXPECT_EQ(WPADGetStatus(), WPAD_STATE_DISABLED);
	KPADInit();
	advance();
	EXPECT_EQ(latest().wpad_err, WPAD_ERR_NONE);
}

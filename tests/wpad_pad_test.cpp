#include <aurora/aurora.h>
#include <aurora/wpad.h>
#include <revolution/kpad.h>
#include "input.hpp"

#include <SDL3/SDL_init.h>
#include <gtest/gtest.h>

namespace aurora {
AuroraConfig g_config{};
}

TEST(WpadPadTest, VirtualPadUsesRealBackendAndInputLifecycle) {
	ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
	PADInit();
	PADClearAllVirtualStatus();

	for (u32 i = 0; i < PAD_CHANMAX; ++i) {
		PADClearKeyBindings(i);
	}

	PADBlockInput(false);
	KPADInit();

	PADStatus pad{};
	pad.button = PAD_BUTTON_A | PAD_TRIGGER_Z;
	pad.stickX = 127;
	PADSetVirtualStatus(2, &pad);
	aurora::input::update();

	u32 device = WPAD_DEV_UNKNOWN;
	EXPECT_EQ(WPADProbe(2, &device), WPAD_ERR_NONE);
	EXPECT_EQ(device, WPAD_DEV_FREESTYLE);
	KPADStatus status{};
	ASSERT_EQ(KPADRead(2, &status, 1), 1);
	EXPECT_EQ(status.hold, WPAD_BUTTON_A | WPAD_BUTTON_Z);
	EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, 1);
	EXPECT_FLOAT_EQ(status.acc.y, -1);
	EXPECT_EQ(status.dpd_valid_fg, 0);

	PADBlockInput(true);
	aurora::input::update();
	ASSERT_EQ(KPADRead(2, &status, 1), 1);
	EXPECT_EQ(status.hold, 0u);
	EXPECT_EQ(status.release, WPAD_BUTTON_A | WPAD_BUTTON_Z);
	EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, 0);

	PADClearVirtualStatus(2);
	aurora::input::update();
	EXPECT_EQ(WPADProbe(2, nullptr), WPAD_ERR_NO_CONTROLLER);
	EXPECT_EQ(KPADRead(2, &status, 1), 1);
	EXPECT_EQ(status.wpad_err, WPAD_ERR_NO_CONTROLLER);

	aurora::input::shutdown();
	EXPECT_EQ(WPADGetStatus(), WPAD_STATE_DISABLED);
	PADBlockInput(false);
	SDL_Quit();
}

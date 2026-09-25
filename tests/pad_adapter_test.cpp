#include <gtest/gtest.h>

#include <aurora/input.hpp>
#include <aurora/pad.hpp>
#include <dolphin/pad.h>

#include "input/router.hpp"

#include <array>

using namespace aurora;
using input::InputEvent;

namespace {

struct Capturer {
  bool captures = false;
  input::LayerId id = input::kInvalidLayerId;

  static input::EventResult on_event(const input::InputSource&, const InputEvent&, void*) {
    return input::EventResult::Pass;
  }
  static bool on_capture(const input::InputSource&, void* userdata) {
    return static_cast<Capturer*>(userdata)->captures;
  }
};

class PadAdapterTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(PADInit());
    PADKeyButtonBinding buttons[PAD_BUTTON_COUNT] = {
        {SDL_SCANCODE_SPACE, PAD_BUTTON_A},      {PAD_KEY_MOUSE_LEFT, PAD_BUTTON_B},
        {SDL_SCANCODE_F, PAD_BUTTON_X},          {SDL_SCANCODE_R, PAD_BUTTON_Y},
        {SDL_SCANCODE_RETURN, PAD_BUTTON_START}, {SDL_SCANCODE_TAB, PAD_TRIGGER_Z},
        {SDL_SCANCODE_Q, PAD_TRIGGER_L},         {SDL_SCANCODE_E, PAD_TRIGGER_R},
        {SDL_SCANCODE_UP, PAD_BUTTON_UP},        {SDL_SCANCODE_DOWN, PAD_BUTTON_DOWN},
        {SDL_SCANCODE_LEFT, PAD_BUTTON_LEFT},    {SDL_SCANCODE_RIGHT, PAD_BUTTON_RIGHT},
    };
    PADKeyAxisBinding axes[PAD_AXIS_COUNT] = {
        {SDL_SCANCODE_D, PAD_AXIS_LEFT_X_POS, 1},  {SDL_SCANCODE_A, PAD_AXIS_LEFT_X_NEG, 1},
        {SDL_SCANCODE_W, PAD_AXIS_LEFT_Y_POS, 1},  {SDL_SCANCODE_S, PAD_AXIS_LEFT_Y_NEG, 1},
        {SDL_SCANCODE_L, PAD_AXIS_RIGHT_X_POS, 1}, {SDL_SCANCODE_J, PAD_AXIS_RIGHT_X_NEG, 1},
        {SDL_SCANCODE_I, PAD_AXIS_RIGHT_Y_POS, 1}, {SDL_SCANCODE_K, PAD_AXIS_RIGHT_Y_NEG, 1},
        {SDL_SCANCODE_Q, PAD_AXIS_TRIGGER_L, 0},   {SDL_SCANCODE_E, PAD_AXIS_TRIGGER_R, 0},
    };
    ASSERT_TRUE(PADSetKeyButtonBindings(PAD_CHAN0, buttons));
    ASSERT_TRUE(PADSetKeyAxisBindings(PAD_CHAN0, axes));
    PADSetKeyboardActive(PAD_CHAN0, TRUE);
  }

  void SetUp() override {
    ui.captures = false;
    ui.id = input::register_layer({
        .label = "test.ui",
        .priority = input::kRmlUiLayerPriority,
        .onEvent = Capturer::on_event,
        .capturesSource = Capturer::on_capture,
        .userdata = &ui,
    });
    read();
    PADConsumeCancellation(PAD_CHAN0);
  }

  void TearDown() override {
    for (const auto scancode :
         {SDL_SCANCODE_SPACE, SDL_SCANCODE_D, SDL_SCANCODE_A, SDL_SCANCODE_Q, SDL_SCANCODE_ESCAPE}) {
      key(scancode, false);
    }
    input::unregister_layer(ui.id);
    PADBlockInput(false);
    PADClearAllVirtualStatus();
    pad::set_action_bindings(PAD_CHAN0, {});
  }

  static void key(SDL_Scancode scancode, bool pressed) {
    input::detail::dispatch(input::keyboard_source(),
                            {.payload = InputEvent::KeyChanged{.scancode = scancode, .pressed = pressed}});
  }

  PADStatus read() {
    std::array<PADStatus, PAD_CHANMAX> status{};
    PADRead(status.data());
    return status[PAD_CHAN0];
  }

  Capturer ui;
};

TEST_F(PadAdapterTest, KeyboardBindingsProduceStatus) {
  EXPECT_EQ(read().err, PAD_ERR_NONE);
  key(SDL_SCANCODE_SPACE, true);
  key(SDL_SCANCODE_D, true);
  key(SDL_SCANCODE_Q, true);
  const auto status = read();
  EXPECT_NE(status.button & PAD_BUTTON_A, 0);
  EXPECT_EQ(status.stickX, 127);
  // Digital L forces the analog trigger to 180.
  EXPECT_NE(status.button & PAD_TRIGGER_L, 0);
  EXPECT_EQ(status.triggerLeft, 180);

  // Opposing keys on one axis sum to zero.
  key(SDL_SCANCODE_A, true);
  EXPECT_EQ(read().stickX, 0);
}

TEST_F(PadAdapterTest, CaptureCancelsHeldInputWithoutReplay) {
  key(SDL_SCANCODE_SPACE, true);
  ASSERT_NE(read().button & PAD_BUTTON_A, 0);
  EXPECT_FALSE(PADConsumeCancellation(PAD_CHAN0));

  // UI opens without any new event; the next read observes the cancellation.
  ui.captures = true;
  EXPECT_EQ(read().button & PAD_BUTTON_A, 0);
  EXPECT_TRUE(PADConsumeCancellation(PAD_CHAN0));
  EXPECT_FALSE(PADConsumeCancellation(PAD_CHAN0));
  EXPECT_TRUE(PADIsInputCaptured(PAD_CHAN0));

  // UI closes while A is still held: stays suppressed until a fresh press.
  ui.captures = false;
  EXPECT_EQ(read().button & PAD_BUTTON_A, 0);
  EXPECT_FALSE(PADIsInputCaptured(PAD_CHAN0));
  key(SDL_SCANCODE_SPACE, false);
  key(SDL_SCANCODE_SPACE, true);
  EXPECT_NE(read().button & PAD_BUTTON_A, 0);
}

TEST_F(PadAdapterTest, BlockInputShim) {
  key(SDL_SCANCODE_D, true);
  ASSERT_EQ(read().stickX, 127);
  PADBlockInput(true);
  EXPECT_EQ(read().stickX, 0);
  PADBlockInput(false);
  EXPECT_EQ(read().stickX, 0);
  key(SDL_SCANCODE_D, false);
  key(SDL_SCANCODE_D, true);
  EXPECT_EQ(read().stickX, 127);
}

TEST_F(PadAdapterTest, VirtualStatusShimIsWithheldWhileBlocked) {
  PADStatus virtualStatus{};
  virtualStatus.button = PAD_BUTTON_Y;
  virtualStatus.stickY = 64;
  PADSetVirtualStatus(PAD_CHAN0, &virtualStatus);
  auto status = read();
  EXPECT_NE(status.button & PAD_BUTTON_Y, 0);
  EXPECT_EQ(status.stickY, 64);
  EXPECT_FALSE(PADConsumeCancellation(PAD_CHAN0));

  // Withholding held virtual input is a cancellation, not a release.
  PADBlockInput(true);
  status = read();
  EXPECT_EQ(status.button & PAD_BUTTON_Y, 0);
  EXPECT_EQ(status.stickY, 0);
  EXPECT_TRUE(PADConsumeCancellation(PAD_CHAN0));

  PADBlockInput(false);
  PADClearVirtualStatus(PAD_CHAN0);
  EXPECT_EQ(read().button & PAD_BUTTON_Y, 0);
}

TEST_F(PadAdapterTest, ActionBindingsJoinPortSet) {
  const auto before = pad::binding_set(PAD_CHAN0);
  const auto menu = binding::register_control({.name = "test.open_menu", .kind = binding::ControlKind::Button});
  pad::set_action_bindings(
      PAD_CHAN0,
      {
          {.input = {.control = binding::PhysicalInput::Key{.scancode = SDL_SCANCODE_ESCAPE}}, .target = menu},
          // No controller is assigned, so this one is omitted.
          {.input = {.control = binding::PhysicalInput::GamepadButton{.button = SDL_GAMEPAD_BUTTON_BACK}},
           .target = menu},
      });
  const auto after = pad::binding_set(PAD_CHAN0);
  EXPECT_NE(after.generation, before.generation);
  ASSERT_NE(after.set, nullptr);

  binding::State uiState{after.set};
  const auto keyboard = input::keyboard_source();
  const auto result =
      uiState.process(keyboard, {.source = keyboard.id,
                                 .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_ESCAPE, .pressed = true}});
  EXPECT_TRUE(result.matched());
  EXPECT_EQ(uiState.value(menu), 1.f);

  size_t menuBindings = 0;
  for (const auto& binding : after.set->bindings) {
    menuBindings += binding.target == menu ? 1 : 0;
  }
  EXPECT_EQ(menuBindings, 1u);
  EXPECT_EQ(pad::binding_set(PAD_CHAN0).generation, after.generation);
}

TEST_F(PadAdapterTest, UnassignedPortReportsNoController) {
  std::array<PADStatus, PAD_CHANMAX> status{};
  PADRead(status.data());
  EXPECT_EQ(status[PAD_CHAN1].err, PAD_ERR_NO_CONTROLLER);
}

} // namespace

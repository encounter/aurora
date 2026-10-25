#include <gtest/gtest.h>
#include <aurora/pad.hpp>
#include <dolphin/si.h>
#include "gamepad.hpp"
#include "input/router.hpp"
#include "input/sdl_input.hpp"
#include "input/legacy.hpp"
#include "internal.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_timer.h>
#include <fstream>
#include <filesystem>

using namespace aurora;
using PhysicalInput = binding::PhysicalInput;
using Direction = PhysicalInput::GamepadAxis::Direction;
namespace {

class InputProfilesTest : public testing::Test {
protected:
  std::filesystem::path path;
  std::vector<SDL_JoystickID> joysticks;
  void SetUp() override {
    ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD));
    path = std::filesystem::temp_directory_path() / ("aurora-input-" + std::to_string(SDL_GetTicksNS()));
    std::filesystem::create_directories(path);
    ASSERT_TRUE(pad::load_profiles(path));
    ASSERT_TRUE(PADInit());
    for (uint32_t port = 0; port < 4; ++port) {
      pad::clear_port(port);
    }

    PADBlockInput(false);
    PADClearAllVirtualStatus();
  }

  void TearDown() override {
    for (auto id : joysticks) {
      input::sdl::gamepad_removed(id);
      gamepad::remove_controller(id);
      SDL_DetachVirtualJoystick(id);
    }

    for (uint32_t port = 0; port < 4; ++port) {
      (void)pad::gameplay_state(port).reset();
    }

    input::detail::dispatch(
        {.source = input::keyboard_source(), .payload = input::InputEvent::KeyChanged{.scancode = SDL_SCANCODE_SPACE}});
    std::filesystem::remove_all(path);
  }

  input::SourceId controller(const char* name = "Test Gamepad", uint16_t vid = 0x1234, uint16_t pid = 0x5678) {
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = vid;
    desc.product_id = pid;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = name;
    const auto id = SDL_AttachVirtualJoystick(&desc);
    EXPECT_NE(id, 0u);
    joysticks.push_back(id);
    EXPECT_EQ(gamepad::add_controller(id), id);
    input::sdl::gamepad_added(id);
    return input::source_for_gamepad(id);
  }

  static binding::Binding key(SDL_Scancode code, float scale = 1.f) {
    return {.input = {.control = PhysicalInput::Key{code}}, .scale = scale};
  }

  static binding::Binding button(SDL_GamepadButton code) {
    return {.input = {.control = PhysicalInput::GamepadButton{code}}};
  }

  static void press(input::SourceId source, SDL_GamepadButton button) {
    input::detail::dispatch({.source = {.id = source}, .payload = input::InputEvent::ButtonChanged{button, true}});
  }
};

TEST_F(InputProfilesTest, SimultaneousKeyboardAndControllerAndConnectionRules) {
  const auto& c = pad::controls();
  const auto source = controller();
  ASSERT_TRUE(pad::set_port_controller(0, source));
  ASSERT_TRUE(pad::set_binding(pad::ProfileTarget::keyboard(0), c.a, {key(SDL_SCANCODE_SPACE)}));
  auto set = pad::binding_set(0);
  EXPECT_TRUE(set->references(source));
  EXPECT_TRUE(set->references(input::keyboard_source().id));
  input::detail::dispatch({.source = input::keyboard_source(),
                           .payload = input::InputEvent::KeyChanged{.scancode = SDL_SCANCODE_SPACE, .pressed = true}});
  press(source, SDL_GAMEPAD_BUTTON_EAST);
  std::array<PADStatus, 4> status;
  PADRead(status.data());
  EXPECT_NE(status[0].button & PAD_BUTTON_A, 0);
  EXPECT_NE(status[0].button & PAD_BUTTON_B, 0);
  pad::clear_port(0);
  EXPECT_TRUE(pad::connected(0));
  EXPECT_EQ(SIProbe(0), SI_GC_CONTROLLER);
  EXPECT_FALSE(pad::connected(1));
  EXPECT_EQ(SIProbe(1), SI_ERROR_NO_RESPONSE);
  EXPECT_FALSE(pad::set_binding(pad::ProfileTarget::keyboard(0), c.a, {button(SDL_GAMEPAD_BUTTON_SOUTH)}));
  EXPECT_FALSE(pad::set_binding(pad::ProfileTarget::controller(source), c.a, {key(SDL_SCANCODE_SPACE)}));
}

TEST_F(InputProfilesTest, RebindKeepsOppositeAxisAndRespectsConflictGroupsAndLinkedTriggers) {
  const auto target = pad::ProfileTarget::keyboard(0);
  const auto& c = pad::controls();
  pad::set_binding(target, c.leftX, {key(SDL_SCANCODE_D), key(SDL_SCANCODE_A, -1)});
  ASSERT_TRUE(pad::rebind(target, c.a, key(SDL_SCANCODE_D)));
  auto left = pad::bindings_for_control(target, c.leftX);
  ASSERT_EQ(left.size(), 1u);
  EXPECT_EQ(left[0].input.control.get_if<PhysicalInput::Key>()->scancode, SDL_SCANCODE_A);
  pad::set_binding(target, c.triggerL, {key(SDL_SCANCODE_Q)});
  pad::rebind(target, c.l, key(SDL_SCANCODE_Q));
  EXPECT_EQ(pad::bindings_for_control(target, c.triggerL).size(), 1u);
  const auto app = binding::register_control({.name = "test.open_menu"});
  pad::rebind(target, app, key(SDL_SCANCODE_Q));
  EXPECT_EQ(pad::bindings_for_control(target, c.l).size(), 1u);
  pad::rebind(target, c.b, key(SDL_SCANCODE_D), pad::ConflictPolicy::Allow);
  EXPECT_EQ(pad::bindings_for_control(target, c.a).size(), 1u);
}

TEST_F(InputProfilesTest, JsonRoundTripPreservesNullAndUnknownControlNames) {
  const auto target = pad::ProfileTarget::keyboard(0);
  const auto& c = pad::controls();
  pad::set_binding(target, c.a, {});
  pad::set_binding(target, "future.action", {key(SDL_SCANCODE_F1)});
  pad::set_binding(target, c.leftX, {key(SDL_SCANCODE_D), key(SDL_SCANCODE_A, -1)});
  ASSERT_TRUE(pad::save_profiles());
  ASSERT_TRUE(pad::load_profiles(path));
  EXPECT_TRUE(pad::bindings_for_control(target, c.a).empty());
  EXPECT_EQ(pad::profile(target)->bindings.at("future.action").size(), 1u);
  EXPECT_EQ(pad::bindings_for_control(target, c.leftX).size(), 2u);
  std::ifstream stream(path / "input.json");
  std::string contents((std::istreambuf_iterator<char>(stream)), {});
  EXPECT_NE(contents.find("\"aurora.pad.a\": null"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(path / "keyboard_bindings.dat"));
}

TEST_F(InputProfilesTest, TriggerEmulationIgnoresKeyboardAndUsesPhysicalAxisDeadzone) {
  const auto source = controller();
  pad::set_port_controller(0, source);
  const auto& c = pad::controls();
  pad::set_binding(pad::ProfileTarget::keyboard(0), c.l, {key(SDL_SCANCODE_Q)});
  const auto set = pad::binding_set(0);
  binding::State state(set);
  (void)state.process(
      {.source = {.id = source}, .payload = input::InputEvent::AxisChanged{SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.f}});
  EXPECT_EQ(state.value(c.l), 1.f);
  const auto target = pad::ProfileTarget::controller(source);
  auto settings = pad::profile(target)->settings;
  settings.leftStickDeadZone = 0.1f;
  settings.rightStickDeadZone = 0.5f;
  pad::set_settings(target, settings);
  pad::set_binding(target, c.leftX, {{.input = {.control = PhysicalInput::GamepadAxis{SDL_GAMEPAD_AXIS_RIGHTX}}}});
  binding::State remapped(pad::binding_set(0));
  (void)remapped.process(
      {.source = {.id = source}, .payload = input::InputEvent::AxisChanged{SDL_GAMEPAD_AXIS_RIGHTX, 0.4f}});
  EXPECT_EQ(remapped.value(c.leftX), 0.f);
}

TEST_F(InputProfilesTest, NewIdentitiesCopyIndependentModelTemplatesAndNoneStopsAutoClaiming) {
  const auto first = controller("First");
  const auto second = controller("Second");
  ASSERT_NE(pad::controller_identity(first)->guid, pad::controller_identity(second)->guid);
  const auto control = pad::controls().a;
  pad::set_binding(pad::ProfileTarget::controller(first), control, {button(SDL_GAMEPAD_BUTTON_NORTH)});
  auto copy = pad::bindings_for_control(pad::ProfileTarget::controller(second), control);
  ASSERT_EQ(copy.size(), 1u);
  EXPECT_EQ(copy[0].input.control.get_if<PhysicalInput::GamepadButton>()->button, SDL_GAMEPAD_BUTTON_NORTH);
  pad::set_binding(pad::ProfileTarget::controller(second), control, {});
  EXPECT_EQ(pad::bindings_for_control(pad::ProfileTarget::controller(first), control).size(), 1u);
  pad::set_port_auto(0);
  EXPECT_NE(pad::port_controller(0), 0u);
  pad::clear_port(0);
  EXPECT_EQ(pad::port_controller(0), 0u);
}

template <typename T>
void write(std::ostream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

TEST_F(InputProfilesTest, LegacyKeyboardMigrationPreservesUnbindsAndInactivePortGating) {
  std::filesystem::remove(path / "input.json");
  {
    std::ofstream out(path / "keyboard_bindings.dat", std::ios::binary);
    write(out, uint32_t(SBIG('KBND')));
    write(out, uint32_t(3));
    out.seekp(32);
    const std::array<PADButton, 12> ids{
        PAD_BUTTON_A,
        PAD_BUTTON_B,
        PAD_BUTTON_X,
        PAD_BUTTON_Y,
        PAD_BUTTON_START,
        PAD_TRIGGER_Z,
        PAD_TRIGGER_L,
        PAD_TRIGGER_R,
        PAD_BUTTON_UP,
        PAD_BUTTON_DOWN,
        PAD_BUTTON_LEFT,
        PAD_BUTTON_RIGHT,
    };

    for (uint32_t port = 0; port < 4; ++port) {
      std::array<PADKeyButtonBinding, 12> buttons;
      std::array<PADKeyAxisBinding, 10> axes;
      for (uint32_t i = 0; i < buttons.size(); ++i) {
        buttons[i] = {i == 0 ? SDL_SCANCODE_SPACE : PAD_KEY_INVALID, ids[i]};
      }

      for (uint32_t i = 0; i < axes.size(); ++i) {
        axes[i] = {PAD_KEY_INVALID, static_cast<PADAxis>(i), 1};
      }

      write(out, uint8_t(port == 2));
      write(out, buttons);
      write(out, axes);
    }
  }

  ASSERT_TRUE(pad::load_profiles(path));
  ASSERT_TRUE(pad::legacy_migration());
  EXPECT_FALSE(pad::legacy_migration()->keyboardActive[0]);
  EXPECT_TRUE(pad::legacy_migration()->keyboardActive[2]);
  EXPECT_EQ(pad::profile(pad::ProfileTarget::keyboard(0))->bindings.size(), 18u);
  EXPECT_TRUE(pad::profile(pad::ProfileTarget::keyboard(1))->bindings.empty());
  EXPECT_FALSE(pad::profile(pad::ProfileTarget::keyboard(2))->bindings.empty());
  EXPECT_TRUE(pad::bindings_for_control(pad::ProfileTarget::keyboard(0), pad::controls().b).empty());
  EXPECT_FALSE(std::filesystem::exists(path / "keyboard_bindings.dat"));
  ASSERT_TRUE(pad::load_profiles(path));
  EXPECT_FALSE(pad::legacy_migration());
}

void legacy_controller_record(std::ostream& out, SDL_GamepadButton a) {
  PADDeadZones zones{true, true, 8000, 8000, 31150, 31150};
  const std::array<PADButtonMapping, PAD_BUTTON_COUNT> buttons{
      {
          {uint32_t(a), PAD_BUTTON_A},
          {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
          {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
          {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
          {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
          {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
          {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
          {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
          {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
          {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
          {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
          {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
      },
  };

  std::array<PADAxisMapping, PAD_AXIS_COUNT> axes{};
  for (uint32_t i = 0; i < 10; ++i) {
    axes[i] = {
        {
            int(i < 8 ? i / 2 : i - 4),
            i < 8 && i % 2 ? AXIS_SIGN_NEGATIVE : AXIS_SIGN_POSITIVE,
        },
        -1,
        PADAxis(i),
    };
  }

  write(out, zones);
  write(out, buttons);
  write(out, axes);
}

TEST_F(InputProfilesTest, LegacyControllerTemplateSurvivesMultipleNewIdentitiesAndEmulatesTriggers) {
  std::filesystem::remove(path / "input.json");
  {
    std::ofstream out(path / "Model_1234_5678.controller", std::ios::binary);
    write(out, uint32_t(SBIG('CTRL')));
    write(out, uint32_t(4));
    write(out, uint8_t(0));
    out.seekp(32);
    legacy_controller_record(out, SDL_GAMEPAD_BUTTON_NORTH);
    write(out, uint16_t(10000));
    write(out, uint16_t(20000));
    write(out, uint8_t(1));
  }

  ASSERT_TRUE(pad::load_profiles(path));
  const auto first = controller("First legacy model");
  const auto second = controller("Second legacy model");
  const auto c = pad::controls();
  for (const auto source : {first, second}) {
    const auto target = pad::ProfileTarget::controller(source);
    const auto binding = pad::bindings_for_control(target, c.a);
    ASSERT_EQ(binding.size(), 1u);
    EXPECT_EQ(binding[0].input.control.get_if<PhysicalInput::GamepadButton>()->button, SDL_GAMEPAD_BUTTON_NORTH);
    EXPECT_FALSE(pad::profile(target)->bindings.contains("aurora.pad.l"));
    EXPECT_TRUE(pad::profile(target)->settings.forceDeviceRumble);
    EXPECT_NEAR(pad::profile(target)->settings.rumbleIntensityHigh, 20000.f / 65535.f, 0.0001f);
  }

  ASSERT_TRUE(pad::save_profiles());
  std::ifstream in(path / "input.json");
  std::string json((std::istreambuf_iterator<char>(in)), {});
  EXPECT_NE(json.find("\"guid\": \"\""), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(path / "Model_1234_5678.controller"));
}

TEST_F(InputProfilesTest, AdapterTemplatesUseAssignedGamePortAndSkipZeroFilledRecords) {
  std::filesystem::remove(path / "input.json");
  {
    std::ofstream out(path / "Adapter_057E_0337.controller", std::ios::binary);
    write(out, uint32_t(SBIG('CTRL')));
    write(out, uint32_t(3));
    write(out, uint8_t(1));
    out.seekp(32);
    legacy_controller_record(out, SDL_GAMEPAD_BUTTON_NORTH);
    constexpr size_t size = sizeof(PADDeadZones) + sizeof(PADButtonMapping) * 12 + sizeof(PADAxisMapping) * 10;
    std::array<char, size> zero{};
    write(out, zero);
    legacy_controller_record(out, SDL_GAMEPAD_BUTTON_EAST);
  }

  ASSERT_TRUE(pad::load_profiles(path));
  const auto first = controller("Adapter one", 0x057e, 0x0337);
  const auto second = controller("Adapter two", 0x057e, 0x0337);
  pad::set_port_controller(0, first);
  pad::set_port_controller(2, second);
  const auto c = pad::controls();
  auto at = [&](input::SourceId source) {
    return pad::bindings_for_control(pad::ProfileTarget::controller(source), c.a)
        .front()
        .input.control.get_if<PhysicalInput::GamepadButton>()
        ->button;
  };
  EXPECT_EQ(at(first), SDL_GAMEPAD_BUTTON_NORTH);
  EXPECT_EQ(at(second), SDL_GAMEPAD_BUTTON_EAST);
  pad::set_port_controller(1, first);
  EXPECT_EQ(at(first), SDL_GAMEPAD_BUTTON_SOUTH);
}

TEST_F(InputProfilesTest, LegacyPortPreferencesPinBeforeAutoAndRetainMigrationReport) {
  const auto source = controller("Pinned");
  const auto identity = *pad::controller_identity(source);
  std::filesystem::remove(path / "input.json");
  {
    std::ofstream out(path / "controller_ports.dat", std::ios::binary);
    write(out, uint32_t(SBIG('CPRT')));
    write(out, uint32_t(3));
    for (int port = 0; port < 4; ++port) {
      write(out, uint8_t(port == 3 ? 2 : 1));
      const auto text = [&](const std::string& value) {
        write(out, uint32_t(value.size()));
        out.write(value.data(), value.size());
      };
      text(port == 3 ? identity.guid : "");
      text(port == 3 ? identity.serial : "");
    }

    write(out, uint16_t(12345));
    write(out, uint16_t(54321));
  }

  ASSERT_TRUE(pad::load_profiles(path));
  EXPECT_EQ(pad::port_preference(0).state, pad::PortAssignment::Auto);
  EXPECT_EQ(pad::port_preference(1).state, pad::PortAssignment::None);
  EXPECT_EQ(pad::port_controller(0), 0u);
  EXPECT_EQ(pad::port_controller(3), source);
  ASSERT_TRUE(pad::legacy_migration());
  ASSERT_TRUE(pad::legacy_migration()->controllers[3]);
  EXPECT_EQ(pad::legacy_migration()->controllers[3]->guid, identity.guid);
  uint16_t low;
  uint16_t high;
  gamepad::get_device_rumble_intensity(&low, &high);
  EXPECT_EQ(low, 12345);
  EXPECT_EQ(high, 54321);
}

TEST_F(InputProfilesTest, InvalidJsonAndInvalidRuntimeBindingsAreRejected) {
  const auto target = pad::ProfileTarget::keyboard(0);
  EXPECT_FALSE(pad::set_binding(target, pad::controls().a, {key(SDL_SCANCODE_UNKNOWN)}));
  auto settings = pad::profile(target)->settings;
  settings.leftStickDeadZone = 1.f;
  EXPECT_FALSE(pad::set_settings(target, settings));
  {
    std::ofstream out(path / "input.json");
    out << R"({"$version":1,"keyboard":[{"settings":{"leftStickDeadZone":5}}]})";
  }

  EXPECT_FALSE(pad::load_profiles(path));
  EXPECT_FLOAT_EQ(pad::profile(target)->settings.leftStickDeadZone, 8000.f / 32767.f);
}

TEST_F(InputProfilesTest, IdenticalControllersKeepTheExplicitLiveAssignment) {
  const auto first = controller("Identical");
  const auto second = controller("Identical");
  ASSERT_EQ(pad::controller_identity(first)->guid, pad::controller_identity(second)->guid);
  EXPECT_TRUE(pad::set_port_controller(0, second));
  EXPECT_EQ(pad::port_controller(0), second);
  EXPECT_TRUE(pad::set_port_controller(1, first));
  EXPECT_EQ(pad::port_controller(1), first);
  EXPECT_EQ(pad::port_controller(0), second);
  EXPECT_TRUE(pad::set_port_controller(1, second));
  EXPECT_EQ(pad::port_controller(1), second);
  EXPECT_EQ(pad::port_preference(0).state, pad::PortAssignment::None);
}

TEST_F(InputProfilesTest, RebindMaterializesInheritedDefaultsAndKeepsProfileSettingsIndependent) {
  const auto& c = pad::controls();
  auto up = key(SDL_SCANCODE_W);
  up.target = c.leftY;
  auto down = key(SDL_SCANCODE_S, -1);
  down.target = c.leftY;
  pad::set_default_keyboard_bindings(0, {up, down});
  const auto target = pad::ProfileTarget::keyboard(0);
  EXPECT_FALSE(pad::profile(target)->bindings.contains("aurora.pad.left_y"));
  ASSERT_TRUE(pad::rebind(target, c.a, key(SDL_SCANCODE_W)));
  auto remaining = pad::bindings_for_control(target, c.leftY);
  ASSERT_EQ(remaining.size(), 1u);
  EXPECT_EQ(remaining[0].input.control.get_if<PhysicalInput::Key>()->scancode, SDL_SCANCODE_S);
  EXPECT_TRUE(pad::profile(target)->bindings.contains("aurora.pad.left_y"));
  const auto source = controller();
  const auto controllerTarget = pad::ProfileTarget::controller(source);
  pad::set_binding(controllerTarget, c.a, {button(SDL_GAMEPAD_BUTTON_NORTH)});
  pad::reset_to_defaults(target);
  EXPECT_EQ(pad::bindings_for_control(controllerTarget, c.a)
                .front()
                .input.control.get_if<PhysicalInput::GamepadButton>()
                ->button,
            SDL_GAMEPAD_BUTTON_NORTH);
}

} // namespace

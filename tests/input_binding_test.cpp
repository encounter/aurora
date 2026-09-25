#include <gtest/gtest.h>

#include <aurora/binding.hpp>

#include <memory>

using namespace aurora;
using namespace aurora::binding;
using input::InputEvent;
using input::InputSource;

namespace {

constexpr InputSource kKeyboard{.id = 101, .kind = InputSource::Kind::Keyboard};
constexpr InputSource kPadA{.id = 102, .kind = InputSource::Kind::Controller};
constexpr InputSource kPadB{.id = 103, .kind = InputSource::Kind::Controller};

ControlId button_control(const char* name) { return register_control({.name = name, .kind = ControlKind::Button}); }
ControlId axis_control(const char* name) { return register_control({.name = name, .kind = ControlKind::Axis}); }

PhysicalInput key(SDL_Scancode scancode) {
  return {.source = kKeyboard.id, .control = PhysicalInput::Key{.scancode = scancode}};
}
PhysicalInput pad_button(const InputSource& source, SDL_GamepadButton button) {
  return {.source = source.id, .control = PhysicalInput::GamepadButton{.button = button}};
}
PhysicalInput pad_axis(const InputSource& source, SDL_GamepadAxis axis,
                       PhysicalInput::GamepadAxis::Direction direction = PhysicalInput::GamepadAxis::Direction::Full) {
  return {.source = source.id, .control = PhysicalInput::GamepadAxis{.axis = axis, .direction = direction}};
}

InputEvent key_event(SDL_Scancode scancode, bool pressed, bool repeat = false) {
  return {.source = kKeyboard.id,
          .payload = InputEvent::KeyChanged{.scancode = scancode, .pressed = pressed, .repeat = repeat}};
}
InputEvent button_event(const InputSource& source, SDL_GamepadButton button, bool pressed) {
  return {.source = source.id, .payload = InputEvent::ButtonChanged{.button = button, .pressed = pressed}};
}
InputEvent axis_event(const InputSource& source, SDL_GamepadAxis axis, float value) {
  return {.source = source.id, .payload = InputEvent::AxisChanged{.axis = axis, .value = value}};
}
InputEvent cancel_event(const InputSource& source, decltype(InputEvent::Cancelled::target) target) {
  return {.source = source.id, .payload = InputEvent::Cancelled{.target = target}};
}

std::shared_ptr<const BindingSet> make_set(std::vector<Binding> bindings) {
  return std::make_shared<const BindingSet>(BindingSet{std::move(bindings)});
}

TEST(BindingRegistry, SameNameSameKindReturnsSameId) {
  const auto a = button_control("test.registry.a");
  EXPECT_NE(a, kInvalidControlId);
  EXPECT_EQ(button_control("test.registry.a"), a);
  EXPECT_EQ(axis_control("test.registry.a"), kInvalidControlId);
  EXPECT_EQ(find_control("test.registry.a"), a);
  EXPECT_EQ(find_control("test.registry.missing"), kInvalidControlId);
  EXPECT_EQ(register_control({.name = ""}), kInvalidControlId);
  ASSERT_NE(describe_control(a), nullptr);
  EXPECT_EQ(describe_control(a)->name, "test.registry.a");
}

TEST(BindingState, TwoSourcesHoldingOneControlCancelOne) {
  const auto jump = button_control("test.two_sources.jump");
  State state{make_set({
      {.input = pad_button(kPadA, SDL_GAMEPAD_BUTTON_SOUTH), .target = jump},
      {.input = pad_button(kPadB, SDL_GAMEPAD_BUTTON_SOUTH), .target = jump},
  })};

  auto result = state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_SOUTH, true));
  ASSERT_EQ(result.changes.size(), 1u);
  EXPECT_EQ(result.changes[0].value, 1.f);
  result = state.process(kPadB, button_event(kPadB, SDL_GAMEPAD_BUTTON_SOUTH, true));
  EXPECT_TRUE(result.matched());
  EXPECT_TRUE(result.changes.empty());

  result = state.process(kPadA, cancel_event(kPadA, InputEvent::Cancelled::All{}));
  EXPECT_TRUE(result.changes.empty());
  EXPECT_EQ(state.value(jump), 1.f);

  result = state.process(kPadB, cancel_event(kPadB, InputEvent::Cancelled::Button{SDL_GAMEPAD_BUTTON_SOUTH}));
  ASSERT_EQ(result.changes.size(), 1u);
  EXPECT_EQ(result.changes[0].reason, ControlChange::Reason::Cancelled);
  EXPECT_EQ(result.changes[0].previousValue, 1.f);
  EXPECT_EQ(state.value(jump), 0.f);
}

TEST(BindingState, HeldConditionsEvaluateEagerly) {
  const auto menu = button_control("test.held.menu");
  State state{make_set({
      {.input = pad_button(kPadA, SDL_GAMEPAD_BUTTON_START),
       .target = menu,
       .held = {{.input = pad_button(kPadA, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)}}},
  })};

  auto result = state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_START, true));
  EXPECT_TRUE(result.matched());
  EXPECT_TRUE(result.changes.empty());
  state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_START, false));

  result = state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true));
  EXPECT_TRUE(result.matched());
  EXPECT_TRUE(result.changes.empty());
  result = state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_START, true));
  ASSERT_EQ(result.changes.size(), 1u);
  EXPECT_EQ(state.value(menu), 1.f);

  // Releasing the held condition deactivates immediately.
  result = state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false));
  ASSERT_EQ(result.changes.size(), 1u);
  EXPECT_EQ(state.value(menu), 0.f);
}

TEST(BindingState, KeysSumOnAxis) {
  const auto x = axis_control("test.keys.x");
  State state{make_set({
      {.input = key(SDL_SCANCODE_D), .target = x, .scale = 1.f},
      {.input = key(SDL_SCANCODE_A), .target = x, .scale = -1.f},
  })};

  state.process(kKeyboard, key_event(SDL_SCANCODE_D, true));
  EXPECT_EQ(state.value(x), 1.f);
  state.process(kKeyboard, key_event(SDL_SCANCODE_A, true));
  EXPECT_EQ(state.value(x), 0.f);
  state.process(kKeyboard, key_event(SDL_SCANCODE_D, false));
  EXPECT_EQ(state.value(x), -1.f);

  // Repeats match but do not retrigger.
  const auto result = state.process(kKeyboard, key_event(SDL_SCANCODE_A, true, true));
  EXPECT_TRUE(result.matched());
  EXPECT_TRUE(result.changes.empty());
}

TEST(BindingState, HalfAxesAndDeadZone) {
  using Direction = PhysicalInput::GamepadAxis::Direction;
  const auto y = axis_control("test.half.y");
  State state{make_set({
      {.input = pad_axis(kPadA, SDL_GAMEPAD_AXIS_LEFTY, Direction::Negative),
       .target = y,
       .deadZone = 0.25f,
       .scale = 1.f},
      {.input = pad_axis(kPadA, SDL_GAMEPAD_AXIS_LEFTY, Direction::Positive),
       .target = y,
       .deadZone = 0.25f,
       .scale = -1.f},
  })};

  state.process(kPadA, axis_event(kPadA, SDL_GAMEPAD_AXIS_LEFTY, -0.2f));
  EXPECT_EQ(state.value(y), 0.f);
  state.process(kPadA, axis_event(kPadA, SDL_GAMEPAD_AXIS_LEFTY, -0.5f));
  // SDL up (negative) is GC up (positive); the dead zone does not rescale.
  EXPECT_FLOAT_EQ(state.value(y), 0.5f);
  state.process(kPadA, axis_event(kPadA, SDL_GAMEPAD_AXIS_LEFTY, 0.75f));
  EXPECT_FLOAT_EQ(state.value(y), -0.75f);
}

TEST(BindingState, AxisToButtonThreshold) {
  const auto l = button_control("test.threshold.l");
  State state{make_set({
      {.input = pad_axis(kPadA, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), .target = l, .threshold = 0.9f},
  })};
  state.process(kPadA, axis_event(kPadA, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.5f));
  EXPECT_EQ(state.value(l), 0.f);
  state.process(kPadA, axis_event(kPadA, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.95f));
  EXPECT_EQ(state.value(l), 1.f);
}

TEST(BindingState, MatchedWithoutBindingChange) {
  const auto a = button_control("test.matched.a");
  State state{make_set({
      {.input = pad_button(kPadA, SDL_GAMEPAD_BUTTON_SOUTH), .target = a},
  })};
  auto result = state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_EAST, true));
  EXPECT_FALSE(result.matched());
  result = state.process(kPadB, button_event(kPadB, SDL_GAMEPAD_BUTTON_SOUTH, true));
  EXPECT_FALSE(result.matched());
  result = state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_SOUTH, false));
  EXPECT_TRUE(result.matched());
  EXPECT_TRUE(result.changes.empty());
}

TEST(BindingState, ProducersAggregateAndReset) {
  const auto a = button_control("test.producer.a");
  const auto x = axis_control("test.producer.x");
  State state{make_set({
      {.input = pad_axis(kPadA, SDL_GAMEPAD_AXIS_LEFTX), .target = x},
  })};
  const auto touch = state.add_producer("touch");

  auto changes = state.set_value(touch, a, 1.f);
  ASSERT_EQ(changes.size(), 1u);
  changes = state.set_value(touch, a, 0.f);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].reason, ControlChange::Reason::Input);

  state.process(kPadA, axis_event(kPadA, SDL_GAMEPAD_AXIS_LEFTX, 0.75f));
  state.set_value(touch, x, 0.5f);
  EXPECT_EQ(state.value(x), 1.f);
  state.set_value(touch, x, -0.5f);
  EXPECT_FLOAT_EQ(state.value(x), 0.25f);

  changes = state.reset();
  EXPECT_EQ(state.value(x), 0.f);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].reason, ControlChange::Reason::Cancelled);

  // Producers survive reset.
  state.set_value(touch, a, 1.f);
  EXPECT_EQ(state.value(a), 1.f);
  changes = state.remove_producer(touch);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(state.value(a), 0.f);
}

TEST(BindingState, SetBindingsCancelsOldContributions) {
  const auto a = button_control("test.rebind.a");
  State state{make_set({
      {.input = pad_button(kPadA, SDL_GAMEPAD_BUTTON_SOUTH), .target = a},
  })};
  state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_SOUTH, true));
  const auto changes = state.set_bindings(make_set({
      {.input = pad_button(kPadA, SDL_GAMEPAD_BUTTON_EAST), .target = a},
  }));
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].reason, ControlChange::Reason::Cancelled);
  EXPECT_EQ(state.value(a), 0.f);
  // Remembered input was cleared: the old press does not count for the new set.
  state.process(kPadA, button_event(kPadA, SDL_GAMEPAD_BUTTON_EAST, true));
  EXPECT_EQ(state.value(a), 1.f);
}

} // namespace

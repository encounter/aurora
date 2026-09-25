#include <gtest/gtest.h>

#include <aurora/binding.hpp>
#include <aurora/input.hpp>

#include "input/router.hpp"

#include <vector>

using namespace aurora::input;
using Cancelled = InputEvent::Cancelled;
using SourceChanged = InputEvent::SourceChanged;
using Phase = InputEvent::PointerChanged::Phase;

namespace {

struct Recorder {
  std::vector<InputEvent> events;
  EventResult result = EventResult::Pass;
  bool captures = false;
  PointerMode pointer = PointerMode::None;
  LayerId id = kInvalidLayerId;

  static EventResult on_event(const InputEvent& event, void* userdata) {
    auto* self = static_cast<Recorder*>(userdata);
    self->events.push_back(event);
    return self->result;
  }

  static bool on_capture(const InputSource&, void* userdata) { return static_cast<Recorder*>(userdata)->captures; }

  static PointerMode on_pointer(void* userdata) { return static_cast<Recorder*>(userdata)->pointer; }

  void attach(int32_t priority, const char* label = "test") {
    id = register_layer({
        .label = label,
        .priority = priority,
        .onEvent = on_event,
        .capturesSource = on_capture,
        .pointerMode = on_pointer,
        .userdata = this,
    });
  }

  template <typename T>
  size_t count() const {
    size_t n = 0;
    for (const auto& event : events) {
      n += event.payload.is<T>() ? 1 : 0;
    }
    return n;
  }

  template <typename T>
  const T* last() const {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
      if (const auto* payload = it->payload.get_if<T>()) {
        return payload;
      }
    }
    return nullptr;
  }

  size_t cancels() const { return count<Cancelled>(); }

  size_t cancels(SourceId source) const {
    size_t n = 0;
    for (const auto& event : events) {
      n += event.source.id == source && event.payload.is<Cancelled>() ? 1 : 0;
    }
    return n;
  }
};

class RouterTest : public ::testing::Test {
protected:
  void SetUp() override {
    detail::reset();
    aurora::binding::cancel_capture();
    pad = create_source(InputSource::Kind::Controller, "Virtual pad");
  }
  void TearDown() override {
    detail::reset();
    aurora::binding::cancel_capture();
  }

  void button(SDL_GamepadButton b, bool pressed) {
    inject({
        .source = pad,
        .payload = InputEvent::ButtonChanged{.button = b, .pressed = pressed},
    });
  }
  void axis(SDL_GamepadAxis a, float value) {
    inject({
        .source = pad,
        .payload = InputEvent::AxisChanged{.axis = a, .value = value},
    });
  }

  InputSource pad;
};

TEST_F(RouterTest, ConsumeStopsLowerLayersAndReleaseFollowsPress) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);

  ui.result = EventResult::Consume;
  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  EXPECT_EQ(ui.events.size(), 1u);
  EXPECT_EQ(game.events.size(), 0u);

  // A later Pass does not change the route; the release still belongs to UI only.
  ui.result = EventResult::Pass;
  button(SDL_GAMEPAD_BUTTON_SOUTH, false);
  EXPECT_EQ(ui.events.size(), 2u);
  EXPECT_EQ(game.events.size(), 0u);
}

TEST_F(RouterTest, PassedPressRoutesReleaseToEveryReceiver) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);

  button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
  ui.result = EventResult::Consume;
  button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
  EXPECT_EQ(ui.count<InputEvent::ButtonChanged>(), 2u);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 2u);
}

TEST_F(RouterTest, HeldInputAcrossCaptureBeginAndEnd) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);

  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  ASSERT_EQ(game.count<InputEvent::ButtonChanged>(), 1u);

  // Capture begins with no new events: the next reconciliation cancels gameplay.
  ui.captures = true;
  reconcile();
  ASSERT_EQ(game.cancels(pad.id), 1u);

  // Release while blocked goes nowhere below the barrier.
  button(SDL_GAMEPAD_BUTTON_SOUTH, false);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 1u);

  // Held again while blocked, then capture ends: not replayed.
  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  ui.captures = false;
  reconcile();
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 1u);
  button(SDL_GAMEPAD_BUTTON_SOUTH, false);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 1u);

  // A fresh press reaches gameplay again.
  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 2u);
}

TEST_F(RouterTest, CaptureBlocksEvenWhenHandlerPasses) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);
  ui.captures = true;

  button(SDL_GAMEPAD_BUTTON_EAST, true);
  EXPECT_EQ(ui.count<InputEvent::ButtonChanged>(), 1u);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 0u);
  EXPECT_TRUE(captured_above(pad.id, kGameLayerPriority));
  EXPECT_FALSE(captured_above(pad.id, kRmlUiLayerPriority));
}

TEST_F(RouterTest, StickResamplesAndTriggerRequiresNeutral) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);

  axis(SDL_GAMEPAD_AXIS_LEFTX, 0.8f);
  axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.9f);
  ASSERT_EQ(game.count<InputEvent::AxisChanged>(), 2u);

  ui.captures = true;
  reconcile();
  EXPECT_EQ(game.cancels(pad.id), 1u);

  axis(SDL_GAMEPAD_AXIS_LEFTX, 0.6f);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 2u);

  ui.captures = false;
  reconcile();
  // Stick resumes immediately with the last raw value; the trigger does not.
  ASSERT_EQ(game.count<InputEvent::AxisChanged>(), 3u);
  EXPECT_EQ(game.last<InputEvent::AxisChanged>()->axis, SDL_GAMEPAD_AXIS_LEFTX);
  EXPECT_FLOAT_EQ(game.last<InputEvent::AxisChanged>()->value, 0.6f);

  axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.95f);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 3u);
  axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.f);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 4u);
  axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.5f);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 5u);
}

TEST_F(RouterTest, AxisRoutesUntilNeutral) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);

  // A deflection belongs to the layers it first reached, whatever they return later.
  axis(SDL_GAMEPAD_AXIS_LEFTY, -0.7f);
  ui.result = EventResult::Consume;
  axis(SDL_GAMEPAD_AXIS_LEFTY, -0.8f);
  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.1f);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 3u);

  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.9f);
  ui.result = EventResult::Pass;
  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.6f);
  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.f);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 3u);

  // Neutral samples route per event.
  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.1f);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 4u);
  EXPECT_EQ(game.cancels(), 0u);
}

TEST_F(RouterTest, CaptureCancelsInputHeldFromBefore) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);

  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.8f);
  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  ui.captures = true;
  reconcile();
  // The capturing layer loses what it was already holding, too.
  EXPECT_EQ(ui.cancels(pad.id), 1u);
  EXPECT_EQ(game.cancels(pad.id), 1u);

  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.9f);
  button(SDL_GAMEPAD_BUTTON_SOUTH, false);
  EXPECT_EQ(ui.count<InputEvent::AxisChanged>(), 1u);
  EXPECT_EQ(ui.count<InputEvent::ButtonChanged>(), 1u);

  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.f);
  axis(SDL_GAMEPAD_AXIS_LEFTY, 0.7f);
  EXPECT_EQ(ui.count<InputEvent::AxisChanged>(), 3u);
}

TEST_F(RouterTest, CaptureWithNothingHeldDoesNotCancelCapturer) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);
  ui.captures = true;
  reconcile();
  EXPECT_EQ(ui.cancels(), 0u);
  EXPECT_EQ(game.cancels(pad.id), 1u);
}

TEST_F(RouterTest, NewLayerDoesNotInheritHeldStick) {
  Recorder game;
  game.attach(kGameLayerPriority);
  axis(SDL_GAMEPAD_AXIS_LEFTX, 0.8f);

  Recorder ui;
  ui.attach(kRmlUiLayerPriority);
  axis(SDL_GAMEPAD_AXIS_LEFTX, 0.9f);
  EXPECT_EQ(ui.count<InputEvent::AxisChanged>(), 0u);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 2u);
}

TEST_F(RouterTest, SourceRemovalCancels) {
  Recorder game;
  game.attach(kGameLayerPriority);
  button(SDL_GAMEPAD_BUTTON_NORTH, true);
  destroy_source(pad.id);
  ASSERT_EQ(game.cancels(), 1u);
  // The cancellation comes first, then the disconnect.
  ASSERT_TRUE(game.events.back().payload.is<SourceChanged>());
  EXPECT_EQ(game.last<SourceChanged>()->change, SourceChanged::Change::Disconnected);
  EXPECT_TRUE(game.events[game.events.size() - 2].payload.is<Cancelled>());
  EXPECT_FALSE(find_source(pad.id).has_value());
}

TEST_F(RouterTest, SourceRemovalDuringDispatchIsDeferred) {
  // Destroys its own source partway through a route.
  struct Destroyer {
    SourceId target = kInvalidSourceId;
    static EventResult on_event(const InputEvent& event, void* userdata) {
      const auto* key = event.payload.get_if<InputEvent::KeyChanged>();
      if (key != nullptr && key->repeat) {
        destroy_source(static_cast<Destroyer*>(userdata)->target);
      }
      return EventResult::Pass;
    }
  };
  const auto keyboard = create_source(InputSource::Kind::Keyboard, "Virtual keyboard");
  Destroyer destroyer{.target = keyboard.id};
  const auto id =
      register_layer({.priority = kRmlUiLayerPriority, .onEvent = Destroyer::on_event, .userdata = &destroyer});
  ASSERT_NE(id, kInvalidLayerId);
  Recorder game;
  game.attach(kGameLayerPriority);

  const auto key = [&](bool repeat) {
    inject({.source = keyboard,
            .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_A, .pressed = true, .repeat = repeat}});
  };
  key(false);
  key(true);
  // The repeat still reached every layer on the route; removal followed it.
  EXPECT_EQ(game.count<InputEvent::KeyChanged>(), 2u);
  ASSERT_EQ(game.cancels(keyboard.id), 1u);
  EXPECT_EQ(game.last<SourceChanged>()->change, SourceChanged::Change::Disconnected);
  EXPECT_FALSE(find_source(keyboard.id).has_value());
}

TEST_F(RouterTest, PointerModeFollowsMouseReachability) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);

  game.pointer = PointerMode::Relative;
  reconcile();
  EXPECT_EQ(pointer_mode(), PointerMode::Relative);

  // A UI that captures the mouse releases the lock without stating a preference.
  ui.captures = true;
  reconcile();
  EXPECT_EQ(pointer_mode(), PointerMode::Visible);

  ui.captures = false;
  reconcile();
  EXPECT_EQ(pointer_mode(), PointerMode::Relative);

  // A reachable layer above can still override without capturing.
  ui.pointer = PointerMode::Visible;
  reconcile();
  EXPECT_EQ(pointer_mode(), PointerMode::Visible);

  // Disabled layers have no say.
  set_layer_enabled(ui.id, false);
  EXPECT_EQ(pointer_mode(), PointerMode::Relative);
}

TEST_F(RouterTest, LayerRemovalMidPressDoesNotTransfer) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);
  ui.result = EventResult::Consume;

  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  unregister_layer(ui.id);
  button(SDL_GAMEPAD_BUTTON_SOUTH, false);
  EXPECT_EQ(ui.events.size(), 1u);
  EXPECT_EQ(game.events.size(), 0u);

  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 1u);
}

TEST_F(RouterTest, DisablingCancelsAndRemovesBarrier) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);
  ui.captures = true;

  axis(SDL_GAMEPAD_AXIS_LEFTX, 0.5f);
  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  EXPECT_EQ(game.count<InputEvent::AxisChanged>(), 0u);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 0u);

  set_layer_enabled(ui.id, false);
  EXPECT_EQ(ui.cancels(pad.id), 1u);
  // Barrier removed: stick resampled, held button not replayed.
  ASSERT_EQ(game.count<InputEvent::AxisChanged>(), 1u);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 0u);
}

TEST_F(RouterTest, TouchContactsRouteIndependently) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);
  const auto touch = create_source(InputSource::Kind::Touch, "Virtual touch");
  const auto contact = [&](PointerId id, Phase phase) {
    inject({.source = touch, .payload = InputEvent::PointerChanged{.pointer = id, .phase = phase}});
  };

  ui.result = EventResult::Consume;
  contact(1, Phase::Down);
  ui.result = EventResult::Pass;
  contact(2, Phase::Down);
  contact(1, Phase::Move);
  contact(2, Phase::Move);
  contact(1, Phase::Up);
  contact(2, Phase::Up);

  EXPECT_EQ(ui.count<InputEvent::PointerChanged>(), 6u);
  ASSERT_EQ(game.count<InputEvent::PointerChanged>(), 3u);
  for (const auto& event : game.events) {
    if (const auto* pointer = event.payload.get_if<InputEvent::PointerChanged>()) {
      EXPECT_EQ(pointer->pointer, 2u);
    }
  }
}

TEST_F(RouterTest, MouseButtonsShareRouteUntilAllUp) {
  Recorder ui;
  Recorder game;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);
  const auto mouse = create_source(InputSource::Kind::Mouse, "Virtual mouse");
  const auto pointer = [&](Phase phase, uint8_t buttonId) {
    inject({.source = mouse, .payload = InputEvent::PointerChanged{.phase = phase, .button = buttonId}});
  };

  ui.result = EventResult::Consume;
  pointer(Phase::Down, SDL_BUTTON_LEFT);
  ui.result = EventResult::Pass;
  pointer(Phase::Down, SDL_BUTTON_RIGHT);
  pointer(Phase::Move, 0);
  pointer(Phase::Up, SDL_BUTTON_LEFT);
  pointer(Phase::Up, SDL_BUTTON_RIGHT);
  EXPECT_EQ(game.count<InputEvent::PointerChanged>(), 0u);

  // Hover after the route ends is routed per event.
  pointer(Phase::Move, 0);
  EXPECT_EQ(game.count<InputEvent::PointerChanged>(), 1u);
}

TEST_F(RouterTest, RegistrationDuringCallbackIsDeferred) {
  struct Spawner {
    Recorder late;
    bool spawned = false;
    static EventResult on_event(const InputEvent&, void* userdata) {
      auto* self = static_cast<Spawner*>(userdata);
      if (!self->spawned) {
        self->spawned = true;
        self->late.attach(kImGuiLayerPriority);
      }
      return EventResult::Pass;
    }
  } spawner;
  const auto id = register_layer({.priority = kRmlUiLayerPriority, .onEvent = Spawner::on_event, .userdata = &spawner});
  ASSERT_NE(id, kInvalidLayerId);

  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  EXPECT_TRUE(spawner.spawned);
  EXPECT_EQ(spawner.late.events.size(), 0u);
  // The new layer does not inherit the ongoing press.
  button(SDL_GAMEPAD_BUTTON_SOUTH, false);
  EXPECT_EQ(spawner.late.events.size(), 0u);
  button(SDL_GAMEPAD_BUTTON_WEST, true);
  EXPECT_EQ(spawner.late.events.size(), 1u);
}

TEST_F(RouterTest, CaptureOpenedInCallbackCancelsLowerBeforeNextPoll) {
  struct Menu {
    bool open = false;
    static EventResult on_event(const InputEvent& event, void* userdata) {
      auto* self = static_cast<Menu*>(userdata);
      if (const auto* b = event.payload.get_if<InputEvent::ButtonChanged>();
          b != nullptr && b->button == SDL_GAMEPAD_BUTTON_START && b->pressed) {
        self->open = true;
        return EventResult::Consume;
      }
      return EventResult::Pass;
    }
    static bool on_capture(const InputSource&, void* userdata) { return static_cast<Menu*>(userdata)->open; }
  } menu;
  Recorder game;
  const auto id = register_layer({.priority = kRmlUiLayerPriority,
                                  .onEvent = Menu::on_event,
                                  .capturesSource = Menu::on_capture,
                                  .userdata = &menu});
  ASSERT_NE(id, kInvalidLayerId);
  game.attach(kGameLayerPriority);

  button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
  ASSERT_EQ(game.count<InputEvent::ButtonChanged>(), 1u);
  button(SDL_GAMEPAD_BUTTON_START, true);
  EXPECT_TRUE(menu.open);
  EXPECT_EQ(game.count<InputEvent::ButtonChanged>(), 1u);
  EXPECT_EQ(game.cancels(pad.id), 1u);
}

TEST_F(RouterTest, WatcherReportsFirstPhysicalPress) {
  Recorder ui;
  ui.attach(kRmlUiLayerPriority);
  const auto keyboard = keyboard_source();

  int calls = 0;
  SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
  aurora::binding::capture_next([&](const aurora::binding::PhysicalInput& input) {
    ++calls;
    if (const auto* key = input.control.get_if<aurora::binding::PhysicalInput::Key>()) {
      scancode = key->scancode;
    }
  });
  EXPECT_TRUE(aurora::binding::capturing());

  // Synthetic sources are ignored.
  button(SDL_GAMEPAD_BUTTON_SOUTH, true);
  EXPECT_EQ(calls, 0);

  detail::dispatch(
      {.source = keyboard, .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_J, .pressed = true}});
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(scancode, SDL_SCANCODE_J);
  EXPECT_FALSE(aurora::binding::capturing());
  EXPECT_EQ(ui.count<InputEvent::KeyChanged>(), 0u);
  detail::dispatch(
      {.source = keyboard, .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_J, .pressed = false}});
  EXPECT_EQ(ui.count<InputEvent::KeyChanged>(), 0u);

  // Later input reaches the UI again.
  detail::dispatch(
      {.source = keyboard, .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_K, .pressed = true}});
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(ui.count<InputEvent::KeyChanged>(), 1u);
}

TEST_F(RouterTest, WatcherHoldsPulledAxisAcrossChainedWatches) {
  Recorder ui;
  ui.attach(kRmlUiLayerPriority);
  const auto controller = detail::add_gamepad(1, "Test pad", {});
  const auto stick = [&](float value) {
    detail::dispatch(
        {.source = controller, .payload = InputEvent::AxisChanged{.axis = SDL_GAMEPAD_AXIS_LEFTX, .value = value}});
  };

  std::vector<aurora::binding::PhysicalInput> reported;
  const auto record = [&](const aurora::binding::PhysicalInput& input) { reported.push_back(input); };
  // Rebinding several controls in a row starts the next watch from the callback.
  aurora::binding::capture_next([&](const aurora::binding::PhysicalInput& input) {
    record(input);
    aurora::binding::capture_next(record);
  });

  stick(0.8f);
  ASSERT_EQ(reported.size(), 1u);
  // Still pulled: consumed, and not reported to the next watch.
  stick(0.9f);
  stick(0.1f);
  EXPECT_EQ(reported.size(), 1u);
  EXPECT_EQ(ui.count<InputEvent::AxisChanged>(), 0u);
  EXPECT_TRUE(aurora::binding::capturing());

  stick(-0.7f);
  ASSERT_EQ(reported.size(), 2u);
  const auto* axis = reported[1].control.get_if<aurora::binding::PhysicalInput::GamepadAxis>();
  ASSERT_NE(axis, nullptr);
  EXPECT_EQ(axis->direction, aurora::binding::PhysicalInput::GamepadAxis::Direction::Negative);

  aurora::binding::cancel_capture();
  EXPECT_FALSE(aurora::binding::capturing());
  stick(0.f);
  EXPECT_EQ(ui.count<InputEvent::AxisChanged>(), 1u);
}

TEST_F(RouterTest, SourceChangesReachEveryEnabledLayer) {
  Recorder ui;
  Recorder game;
  Recorder disabled;
  ui.attach(kRmlUiLayerPriority);
  game.attach(kGameLayerPriority);
  disabled.attach(kGameLayerPriority - 1);
  set_layer_enabled(disabled.id, false);
  ui.captures = true;

  const auto controller = detail::add_gamepad(7, "Test pad", {});
  EXPECT_EQ(source_for_gamepad(7), controller.id);
  EXPECT_EQ(gamepad_for_source(controller.id), 7u);
  // Capture doesn't hide connection changes.
  EXPECT_EQ(ui.count<SourceChanged>(), 1u);
  EXPECT_EQ(game.count<SourceChanged>(), 1u);
  EXPECT_EQ(disabled.count<SourceChanged>(), 0u);
  EXPECT_EQ(game.last<SourceChanged>()->change, SourceChanged::Change::Connected);

  detail::remap_gamepad(7);
  EXPECT_EQ(game.last<SourceChanged>()->change, SourceChanged::Change::Remapped);

  detail::remove_gamepad(7);
  EXPECT_EQ(ui.last<SourceChanged>()->change, SourceChanged::Change::Disconnected);
  EXPECT_EQ(game.last<SourceChanged>()->change, SourceChanged::Change::Disconnected);
  EXPECT_EQ(disabled.count<SourceChanged>(), 0u);
  // The name outlives the connection, e.g. for a "disconnected" notification.
  EXPECT_EQ(source_name(controller.id), "Test pad");
  EXPECT_EQ(gamepad_for_source(controller.id), 0u);
  EXPECT_EQ(source_for_gamepad(7), kInvalidSourceId);
}

TEST_F(RouterTest, FocusLossCancelsKeyboard) {
  Recorder game;
  game.attach(kGameLayerPriority);
  const auto keyboard = keyboard_source();
  detail::dispatch(
      {.source = keyboard, .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_W, .pressed = true}});
  detail::focus_lost();
  EXPECT_EQ(game.cancels(keyboard.id), 1u);
  // The release SDL sends afterwards has no route.
  detail::dispatch(
      {.source = keyboard, .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_W, .pressed = false}});
  EXPECT_EQ(game.count<InputEvent::KeyChanged>(), 1u);
}

} // namespace

#include <gtest/gtest.h>
#include <aurora/input.hpp>
#include "input/router.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <functional>
#include <vector>

namespace aurora::input {
namespace {

struct Platform : detail::TextInputBackend {
  int starts = 0, stops = 0, clears = 0, areas = 0;
  bool succeeds = true;
  TextInputOptions options;
  std::optional<TextInputArea> area;

  bool start(const TextInputOptions& value) override {
    ++starts;
    options = value;
    return succeeds;
  }

  void stop() override { ++stops; }

  void clear_composition() override { ++clears; }

  void set_area(const std::optional<TextInputArea>& value) override {
    ++areas;
    area = value;
  }
};

struct Editor {
  LayerId layer = 0;
  bool capture = false;
  std::vector<InputEvent> events;
  std::function<void(const InputEvent&)> callback;

  void attach(int priority) {
    layer = register_layer({
        .priority = priority,
        .onEvent =
            [](const InputEvent& event, void* data) {
              auto& self = *static_cast<Editor*>(data);
              self.events.push_back(event);
              if (self.callback) {
                self.callback(event);
              }
              return EventResult::Pass;
            },
        .capturesSource =
            [](const InputSource& source, void* data) {
              return source.kind == InputSource::Kind::Keyboard && static_cast<Editor*>(data)->capture;
            },
        .userdata = this,
    });
  }

  template <class T>
  size_t count() const {
    size_t count = 0;
    for (const auto& event : events) {
      count += event.payload.is<T>();
    }
    return count;
  }
};

class TextInputTest : public ::testing::Test {
protected:
  Platform platform;
  Editor game, host, debug;
  void SetUp() override {
    detail::reset();
    detail::set_text_input_backend(&platform);
    game.attach(0);
    host.attach(100);
    debug.attach(200);
  }

  void TearDown() override { detail::reset(); }

  void text(std::string value = "committed", uint64_t timestamp = 0) {
    detail::dispatch(
        {.source = keyboard_source(), .timestampNs = timestamp, .payload = InputEvent::TextInput{std::move(value)}});
  }
};

TEST_F(TextInputTest, ExclusiveDeliveryAndLateCloseCannotStopNewOwner) {
  text();
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 0);
  const auto a = begin_text_input(game.layer);
  EXPECT_EQ(text_input_state(a), TextInputState::Active);
  text();
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 1);
  const auto b = begin_text_input(host.layer);
  EXPECT_EQ(text_input_state(a), TextInputState::Suspended);
  EXPECT_EQ(text_input_state(b), TextInputState::Active);
  EXPECT_EQ(platform.starts, 1); // Compatible handoff keeps the software keyboard visible.
  EXPECT_EQ(platform.stops, 0);
  EXPECT_EQ(platform.clears, 1);
  const int areas = platform.areas;
  end_text_input(a);
  EXPECT_EQ(platform.starts, 1);
  EXPECT_EQ(platform.stops, 0);
  EXPECT_EQ(platform.areas, areas);
  text();
  EXPECT_EQ(host.count<InputEvent::TextInput>(), 1);
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 1); // Pass never forwards text.
  EXPECT_EQ(debug.count<InputEvent::TextInput>(), 0);
}

TEST_F(TextInputTest, CaptureWithoutRequestSuspendsAndClosingSuspendedEditorDoesNotResumeIt) {
  const auto a = begin_text_input(game.layer);
  host.capture = true;
  reconcile();
  EXPECT_EQ(text_input_state(a), TextInputState::Suspended);
  EXPECT_EQ(platform.stops, 1);
  EXPECT_FALSE(platform.area);
  text();
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 0);
  end_text_input(a);
  host.capture = false;
  reconcile();
  EXPECT_EQ(platform.starts, 1);
  EXPECT_EQ(text_input_state(a), TextInputState::Invalid);
}

TEST_F(TextInputTest, EqualPrioritiesUseRegistrationOrderAndCaptures) {
  Editor first, second;
  first.attach(300);
  second.attach(300);
  first.capture = true;
  const auto b = begin_text_input(second.layer);
  EXPECT_EQ(text_input_state(b), TextInputState::Suspended);
  const auto a = begin_text_input(first.layer);
  EXPECT_EQ(text_input_state(a), TextInputState::Active);
  first.capture = false;
  reconcile();
  EXPECT_EQ(text_input_state(b), TextInputState::Suspended); // Higher request wins without capture too.
  end_text_input(a);
  EXPECT_EQ(text_input_state(b), TextInputState::Active);
  unregister_layer(first.layer);
  unregister_layer(second.layer);
}

TEST_F(TextInputTest, DisabledRequestsResumeAndRemovedGenerationsStayInvalid) {
  const auto a = begin_text_input(game.layer);
  const auto b = begin_text_input(host.layer);
  set_layer_enabled(host.layer, false);
  EXPECT_EQ(text_input_state(b), TextInputState::Suspended);
  EXPECT_EQ(text_input_state(a), TextInputState::Active);
  set_layer_enabled(host.layer, true);
  EXPECT_EQ(text_input_state(b), TextInputState::Active);
  unregister_layer(host.layer);
  EXPECT_EQ(text_input_state(b), TextInputState::Invalid);
  EXPECT_FALSE(update_text_input(b, {}));
  end_text_input(b);
  EXPECT_EQ(text_input_state(a), TextInputState::Active);
  EXPECT_EQ(begin_text_input(host.layer), 0);
}

TEST_F(TextInputTest, SupersededHandlesAndMovedWrappersCannotEndCurrentSession) {
  TextInputSession first(game.layer);
  const auto old = first.id();
  TextInputSession second(game.layer);
  const auto current = second.id();
  EXPECT_NE(current, old);
  EXPECT_EQ(first.state(), TextInputState::Invalid);
  EXPECT_FALSE(first.update({}));
  first.reset();
  TextInputSession moved(std::move(second));
  second.reset();
  EXPECT_EQ(moved.state(), TextInputState::Active);
  EXPECT_EQ(platform.starts, 1);
  EXPECT_EQ(platform.stops, 0);
}
TEST_F(TextInputTest, CaretOnlyUpdatesAndUnchangedOptionsDoNotRestartPlatform) {
  TextInputOptions options{.area = TextInputArea{{10, 20, 1, 15}, 0}};
  const auto id = begin_text_input(game.layer, options);
  EXPECT_EQ(platform.areas, 1);
  update_text_input(id, options);
  EXPECT_EQ(platform.areas, 1);
  options.area->rect.x = 25;
  options.area->cursorOffset = 2;
  update_text_input(id, options);
  EXPECT_EQ(platform.areas, 2);
  EXPECT_EQ(platform.starts, 1);
  EXPECT_EQ(platform.stops, 0);
  options.type = TextInputType::Number;
  options.multiline = true;
  update_text_input(id, options);
  EXPECT_EQ(platform.starts, 2);
  EXPECT_EQ(platform.options.type, TextInputType::Number);
  EXPECT_TRUE(platform.options.multiline);
  const auto b = begin_text_input(host.layer);
  EXPECT_EQ(text_input_state(b), TextInputState::Active);
  EXPECT_FALSE(platform.area); // An omitted area clears the previous editor's rectangle.
}

TEST_F(TextInputTest, FocusAndOwnershipChangesDiscardDelayedTextAndComposition) {
  const auto a = begin_text_input(game.layer);
  detail::dispatch({.source = keyboard_source(), .payload = InputEvent::TextEditing{"preview", 0, 7}});
  EXPECT_EQ(game.count<InputEvent::TextEditing>(), 1);
  const auto oldTime = SDL_GetTicksNS();
  detail::focus_lost();
  EXPECT_EQ(text_input_state(a), TextInputState::Suspended);
  text();
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 0);
  detail::focus_gained();
  EXPECT_EQ(text_input_state(a), TextInputState::Active);
  text("delayed", oldTime);
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 0);
  text();
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 1);
  const auto beforeHandoff = SDL_GetTicksNS();
  const auto b = begin_text_input(host.layer);
  text("old IME", beforeHandoff);
  EXPECT_EQ(host.count<InputEvent::TextInput>(), 0);
  EXPECT_EQ(text_input_state(b), TextInputState::Active);
  EXPECT_GE(platform.clears, 2);
}

TEST_F(TextInputTest, FailureIsReportedWithoutFallbackOrBusyRetry) {
  const auto a = begin_text_input(game.layer);
  platform.succeeds = false;
  const TextInputOptions numeric{.type = TextInputType::Number};
  const auto b = begin_text_input(host.layer, numeric);
  EXPECT_EQ(text_input_state(b), TextInputState::Failed);
  EXPECT_EQ(text_input_state(a), TextInputState::Suspended);
  EXPECT_EQ(platform.starts, 2);
  for (int i = 0; i < 10; ++i) {
    reconcile();
    text();
  }
  EXPECT_EQ(platform.starts, 2);
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 0);
  EXPECT_EQ(host.count<InputEvent::TextInput>(), 0);
  platform.succeeds = true;
  update_text_input(b, numeric); // Explicit retry, even when options match.
  EXPECT_EQ(text_input_state(b), TextInputState::Active);
  EXPECT_EQ(platform.starts, 3);
}
TEST_F(TextInputTest, CallbackMutationsArePendingAndQueuedTextKeepsItsGeneration) {
  const auto a = begin_text_input(game.layer);
  TextInputId b = 0;
  SDL_Event raw{};
  game.callback = [&](const InputEvent& event) {
    if (!event.payload.is<InputEvent::KeyChanged>()) {
      return;
    }
    b = begin_text_input(host.layer);
    EXPECT_EQ(text_input_state(b), TextInputState::Pending);
    // A nested platform delivery was enqueued for A before B became active.
    detail::dispatch({.source = keyboard_source(), .payload = InputEvent::TextInput{"stale"}}, &raw);
  };
  detail::dispatch({.source = keyboard_source(),
                    .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_A, .keycode = SDLK_A, .pressed = true}});
  EXPECT_EQ(text_input_state(a), TextInputState::Suspended);
  EXPECT_EQ(text_input_state(b), TextInputState::Active);
  EXPECT_EQ(game.count<InputEvent::TextInput>(), 0);
  EXPECT_EQ(host.count<InputEvent::TextInput>(), 0);
  text();
  EXPECT_EQ(host.count<InputEvent::TextInput>(), 1);
}

TEST_F(TextInputTest, CanRegisterAndRequestFromCallbackAndReceiveTargetedState) {
  Editor deferred;
  TextInputId id = 0;
  game.callback = [&](const InputEvent& event) {
    if (!event.payload.is<InputEvent::KeyChanged>()) {
      return;
    }
    deferred.attach(250);
    id = begin_text_input(deferred.layer);
    EXPECT_NE(id, 0);
    EXPECT_EQ(text_input_state(id), TextInputState::Pending);
  };
  detail::dispatch({.source = keyboard_source(),
                    .payload = InputEvent::KeyChanged{.scancode = SDL_SCANCODE_A, .keycode = SDLK_A, .pressed = true}});
  EXPECT_EQ(text_input_state(id), TextInputState::Active);
  ASSERT_EQ(deferred.count<InputEvent::TextInputStateChanged>(), 1);
  const auto* state = deferred.events.back().payload.get_if<InputEvent::TextInputStateChanged>();
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->session, id);
  EXPECT_EQ(state->state, TextInputState::Active);
  unregister_layer(deferred.layer);
}

TEST_F(TextInputTest, WindowDestructionAndResetInvalidateWithoutReusingTokens) {
  const auto a = begin_text_input(game.layer);
  detail::text_window_destroyed();
  EXPECT_EQ(text_input_state(a), TextInputState::Invalid);
  EXPECT_EQ(platform.stops, 1);
  detail::set_text_input_backend(&platform);
  const auto b = begin_text_input(game.layer);
  EXPECT_NE(a, b);
  detail::reset();
  EXPECT_EQ(text_input_state(b), TextInputState::Invalid);
  EXPECT_EQ(platform.stops, 2);
  game.attach(0);
  detail::set_text_input_backend(&platform);
  const auto c = begin_text_input(game.layer);
  EXPECT_NE(c, b);
  end_text_input(a);
  end_text_input(b);
  EXPECT_EQ(text_input_state(c), TextInputState::Active);
}
} // namespace
} // namespace aurora::input

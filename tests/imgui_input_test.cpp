#include <gtest/gtest.h>
#include <aurora/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include "imgui.hpp"
#include "input/router.hpp"

namespace {
namespace input = aurora::input;
using Event = input::InputEvent;

struct Platform : input::detail::TextInputBackend {
  int starts = 0, stops = 0, clears = 0;

  bool start(const input::TextInputOptions&) override {
    ++starts;
    return true;
  }

  void stop() override { ++stops; }

  void clear_composition() override { ++clears; }

  void set_area(const std::optional<input::TextInputArea>&) override {}
};

class ImGuiInputTest : public testing::Test {
protected:
  Platform platform;

  void SetUp() override {
    input::detail::reset();
    input::detail::set_text_input_backend(&platform);
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = {640, 480};
    aurora::imgui::initialize_input();
  }

  void TearDown() override {
    aurora::imgui::shutdown_input();
    EXPECT_EQ(ImGui::GetPlatformIO().Platform_SetImeDataFn, nullptr);
    ImGui::DestroyContext();
    input::detail::reset();
  }

  void field(ImGuiID id) {
    auto* context = ImGui::GetCurrentContext();
    context->ActiveId = id;
    context->IO.WantTextInput = id != 0;
    ImGuiPlatformImeData data{};
    data.WantVisible = id != 0;
    data.InputPos = {20, 40};
    data.InputLineHeight = 16;
    ImGui::GetPlatformIO().Platform_SetImeDataFn(context, ImGui::GetMainViewport(), &data);
  }

  void text() {
    input::detail::dispatch({
        .source = input::keyboard_source(),
        .payload = Event::TextInput{"queued"},
    });
  }

  int queued_text() {
    int count = ImGui::GetIO().InputQueueCharacters.Size;
    for (const auto& event : ImGui::GetCurrentContext()->InputEventsQueue) {
      count += event.Type == ImGuiInputEventType_Text;
    }
    return count;
  }
};

TEST_F(ImGuiInputTest, ClosingGameEditorLatePreservesImGuiPlatformSession) {
  const auto layer = input::register_layer({
      .onEvent = [](const Event&, void*) { return input::EventResult::Pass; },
  });
  const auto game = input::begin_text_input(layer);
  field(1);
  EXPECT_EQ(input::text_input_state(game), input::TextInputState::Suspended);
  EXPECT_EQ(platform.starts, 1);
  EXPECT_EQ(platform.stops, 0);
  input::end_text_input(game);
  EXPECT_EQ(platform.starts, 1);
  EXPECT_EQ(platform.stops, 0);
  text();
  EXPECT_GT(queued_text(), 0);
  field(0);
  EXPECT_EQ(queued_text(), 0);
  EXPECT_EQ(platform.stops, 1);
  input::unregister_layer(layer);
}

TEST_F(ImGuiInputTest, FieldSwitchClearsQueuedCharactersWithoutKeyboardHideShow) {
  field(1);
  text();
  ASSERT_GT(queued_text(), 0);
  const int clears = platform.clears;
  field(2);
  EXPECT_EQ(queued_text(), 0);
  EXPECT_GT(platform.clears, clears);
  EXPECT_EQ(platform.starts, 1);
  EXPECT_EQ(platform.stops, 0);
  text();
  EXPECT_GT(queued_text(), 0);
}

TEST_F(ImGuiInputTest, CaptureAndFocusLossDiscardBothImGuiTextQueues) {
  field(1);
  text();
  ImGui::GetIO().InputQueueCharacters.push_back('x');
  const auto modal = input::register_layer({
      .priority = 1000,
      .onEvent = [](const Event&, void*) { return input::EventResult::Pass; },
      .capturesSource = [](const input::InputSource& source,
                           void*) { return source.kind == input::InputSource::Kind::Keyboard; },
  });
  EXPECT_EQ(queued_text(), 0);
  EXPECT_EQ(platform.stops, 1);
  text();
  EXPECT_EQ(queued_text(), 0);
  input::unregister_layer(modal);
  text();
  EXPECT_GT(queued_text(), 0);
  input::detail::focus_lost();
  EXPECT_EQ(queued_text(), 0);
  input::detail::focus_gained();
  text();
  EXPECT_GT(queued_text(), 0);
}
} // namespace

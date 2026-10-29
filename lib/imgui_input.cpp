#include "imgui.hpp"
#include "input/router.hpp"
#include "window.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace aurora::imgui {
namespace {
input::LayerId g_inputLayer = input::kInvalidLayerId;
input::TextInputSession g_textSession;
input::TextInputOptions g_textOptions;
ImGuiID g_textWidget = 0;

void discard_text() {
  auto& context = *ImGui::GetCurrentContext();
  for (int i = context.InputEventsQueue.Size - 1; i >= 0; --i) {
    if (context.InputEventsQueue[i].Type == ImGuiInputEventType_Text) {
      context.InputEventsQueue.erase(context.InputEventsQueue.begin() + i);
    }
  }
  context.IO.InputQueueCharacters.clear();
}

void set_ime_data(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
  if (!data->WantVisible) {
    g_textSession.reset();
    g_textWidget = 0;
    return;
  }
  const auto size = window::get_window_size();
  const auto display = ImGui::GetIO().DisplaySize;
  const float xScale = display.x > 0 ? size.width / display.x : 1.f;
  const float yScale = display.y > 0 ? size.height / display.y : 1.f;
  const input::TextInputOptions options{
      .area =
          input::TextInputArea{
              .rect =
                  {
                      .x = static_cast<int>(std::lround((data->InputPos.x - viewport->Pos.x) * xScale)),
                      .y = static_cast<int>(
                          std::lround((data->InputPos.y - viewport->Pos.y + data->InputLineHeight) * yScale)),
                      .w = 1,
                      .h = std::max(1, static_cast<int>(std::lround(data->InputLineHeight * yScale))),
                  },
          },
  };
  if (g_textWidget != context->ActiveId || g_textSession.state() == input::TextInputState::Invalid) {
    g_textSession = input::TextInputSession(g_inputLayer, options);
  } else if (options != g_textOptions) {
    g_textSession.update(options);
  }
  g_textOptions = options;
  g_textWidget = context->ActiveId;
}

input::EventResult input_layer_event(const input::InputEvent& event, void*) {
  using Kind = input::InputSource::Kind;
  const Kind kind = event.source.kind;
  if (ImGui::GetCurrentContext() == nullptr || (kind != Kind::Keyboard && kind != Kind::Mouse)) {
    return input::EventResult::Pass;
  }
  ImGuiIO& io = ImGui::GetIO();
  if (const auto* state = event.payload.get_if<input::InputEvent::TextInputStateChanged>()) {
    if (state->state != input::TextInputState::Active) {
      discard_text();
    }
    return input::EventResult::Pass;
  }
  if (const auto* text = event.payload.get_if<input::InputEvent::TextInput>()) {
    io.AddInputCharactersUTF8(text->text.c_str());
    return input::EventResult::Consume;
  }
  if (event.payload.is<input::InputEvent::TextEditing>()) {
    return input::EventResult::Consume;
  }
  if (event.payload.is<input::InputEvent::Cancelled>()) {
    if (kind == Kind::Keyboard) {
      io.ClearInputKeys();
    } else {
      io.ClearInputMouse();
    }
    return input::EventResult::Pass;
  }
  if (const SDL_Event* raw = input::detail::current_sdl_event()) {
    process_event(*raw);
  }
  const bool capture = kind == Kind::Keyboard ? io.WantCaptureKeyboard || io.WantTextInput : io.WantCaptureMouse;
  return capture ? input::EventResult::Consume : input::EventResult::Pass;
}

bool input_layer_captures(const input::InputSource& source, void*) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  const ImGuiIO& io = ImGui::GetIO();
  switch (source.kind) {
  case input::InputSource::Kind::Keyboard:
    return io.WantCaptureKeyboard || io.WantTextInput;
  case input::InputSource::Kind::Mouse:
    return io.WantCaptureMouse;
  default:
    return false;
  }
}
} // namespace

void initialize_input() {
  ImGui::GetPlatformIO().Platform_SetImeDataFn = set_ime_data;
  g_inputLayer = input::register_layer({
      .label = "aurora.imgui",
      .priority = input::kImGuiLayerPriority,
      .onEvent = input_layer_event,
      .capturesSource = input_layer_captures,
  });
}

void shutdown_input() {
  g_textSession.reset();
  ImGui::GetPlatformIO().Platform_SetImeDataFn = nullptr;
  input::unregister_layer(g_inputLayer);
  g_inputLayer = input::kInvalidLayerId;
  g_textWidget = 0;
}

} // namespace aurora::imgui

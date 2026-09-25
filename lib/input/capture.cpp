#include <aurora/binding.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace aurora::binding {
namespace {

using input::InputEvent;

constexpr float kAxisPull = 0.5f;

std::optional<PhysicalInput> pressed(const InputEvent& event) {
  const input::SourceId source = event.source.id;
  return event.payload.match(
      [&](const InputEvent::KeyChanged& key) -> std::optional<PhysicalInput> {
        if (!key.pressed || key.repeat) {
          return std::nullopt;
        }
        return PhysicalInput{
            .source = source,
            .control = PhysicalInput::Key{.scancode = key.scancode},
        };
      },
      [&](const InputEvent::ButtonChanged& button) -> std::optional<PhysicalInput> {
        if (!button.pressed) {
          return std::nullopt;
        }
        return PhysicalInput{
            .source = source,
            .control = PhysicalInput::GamepadButton{.button = button.button},
        };
      },
      [&](const InputEvent::PointerChanged& pointer) -> std::optional<PhysicalInput> {
        if (event.source.kind != input::InputSource::Kind::Mouse ||
            pointer.phase != InputEvent::PointerChanged::Phase::Down || pointer.button == 0) {
          return std::nullopt;
        }
        return PhysicalInput{
            .source = source,
            .control = PhysicalInput::MouseButton{.button = pointer.button},
        };
      },
      [&](const InputEvent::AxisChanged& axis) -> std::optional<PhysicalInput> {
        using Direction = PhysicalInput::GamepadAxis::Direction;
        if (std::abs(axis.value) < kAxisPull) {
          return std::nullopt;
        }
        return PhysicalInput{
            .source = source,
            .control =
                PhysicalInput::GamepadAxis{
                    .axis = axis.axis,
                    .direction = axis.value > 0.f ? Direction::Positive : Direction::Negative,
                },
        };
      },
      [](const auto&) -> std::optional<PhysicalInput> { return std::nullopt; });
}

struct Capture {
  CaptureCallback callback;
  input::LayerId layer = input::kInvalidLayerId;

  static input::EventResult on_event(const InputEvent& event, void* userdata) {
    auto& capture = *static_cast<Capture*>(userdata);
    if (event.source.origin != input::InputSource::Origin::Physical || event.payload.is<InputEvent::Cancelled>() ||
        event.payload.is<InputEvent::SourceChanged>()) {
      return input::EventResult::Pass;
    }
    const auto input = pressed(event);
    if (!input) {
      return event.payload.is<InputEvent::AxisChanged>() ? input::EventResult::Consume : input::EventResult::Pass;
    }
    input::unregister_layer(std::exchange(capture.layer, input::kInvalidLayerId));
    std::exchange(capture.callback, nullptr)(*input);
    return input::EventResult::Consume;
  }
};

Capture g_capture;

} // namespace

void capture_next(CaptureCallback callback) {
  if (callback == nullptr) {
    cancel_capture();
    return;
  }
  g_capture.callback = std::move(callback);
  if (g_capture.layer == input::kInvalidLayerId) {
    g_capture.layer = input::register_layer({
        .label = "aurora.capture",
        .priority = INT32_MAX,
        .onEvent = Capture::on_event,
        .userdata = &g_capture,
    });
  }
}

void cancel_capture() {
  g_capture.callback = nullptr;
  input::unregister_layer(std::exchange(g_capture.layer, input::kInvalidLayerId));
}

bool capturing() { return g_capture.callback != nullptr; }

} // namespace aurora::binding

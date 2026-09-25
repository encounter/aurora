#pragma once

#include <aurora/input.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Maps physical input to named logical controls.
namespace aurora::binding {

// Runtime ID for registered controls.
using ControlId = uint64_t;
inline constexpr ControlId kInvalidControlId = 0;

enum class ControlKind {
  Button,
  Axis,
};

struct ControlDescriptor {
  std::string name; // e.g. "aurora.pad.a" or "dusklight.open_map".
  ControlKind kind = ControlKind::Button;
};

// Registering an existing name with the same kind returns its ID.
[[nodiscard]] ControlId register_control(const ControlDescriptor& desc);
[[nodiscard]] ControlId find_control(std::string_view name);
[[nodiscard]] const ControlDescriptor* describe_control(ControlId control);

struct PhysicalInput {
  struct Key {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
  };

  struct MouseButton {
    uint8_t button = 0; // SDL_BUTTON_*
  };

  struct GamepadButton {
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
  };

  struct GamepadAxis {
    enum class Direction {
      Full,
      Positive,
      Negative,
    };

    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
    Direction direction = Direction::Full;

    [[nodiscard]] float select(float value) const {
      switch (direction) {
      case Direction::Positive:
        return value > 0.f ? value : 0.f;
      case Direction::Negative:
        return value < 0.f ? -value : 0.f;
      case Direction::Full:
      default:
        return value;
      }
    }
  };

  input::SourceId source = input::kInvalidSourceId;
  input::Enum<Key, MouseButton, GamepadButton, GamepadAxis> control = Key{};
};

struct HeldInput {
  PhysicalInput input;
  float threshold = 0.5f;
};

struct Binding {
  PhysicalInput input;
  ControlId target = kInvalidControlId;
  std::vector<HeldInput> held;
  float deadZone = 0.f;
  float scale = 1.f;
  float threshold = 0.5f;
};

struct BindingSet {
  std::vector<Binding> bindings;

  [[nodiscard]] bool references(input::SourceId source) const;
};

struct ControlChange {
  enum class Reason {
    Input,
    Cancelled,
  };

  ControlId control = kInvalidControlId;
  float previousValue = 0.f;
  float value = 0.f; // buttons: 0 or 1, axes: [-1, 1]
  Reason reason = Reason::Input;
};

using ProducerId = uint64_t;
inline constexpr ProducerId kInvalidProducerId = 0;

struct MappingResult {
  // Every control the event is bound to, even if its value didn't change.
  std::vector<ControlId> targets;
  std::vector<ControlChange> changes;

  [[nodiscard]] bool matched() const { return !targets.empty(); }
};

// Computed state from input events and producers.
class State {
public:
  explicit State(std::shared_ptr<const BindingSet> bindings = {});
  ~State();
  State(State&&) noexcept;
  State& operator=(State&&) noexcept;
  State(const State&) = delete;
  State& operator=(const State&) = delete;

  [[nodiscard]] MappingResult process(const input::InputEvent& event);
  [[nodiscard]] float value(ControlId control) const;
  [[nodiscard]] std::vector<ControlChange> set_bindings(std::shared_ptr<const BindingSet> bindings);
  [[nodiscard]] std::vector<ControlChange> reset();
  [[nodiscard]] const std::shared_ptr<const BindingSet>& bindings() const;

  // Producers set logical values directly, bypassing bindings, e.g. touch controls.
  [[nodiscard]] ProducerId add_producer();
  std::vector<ControlChange> set_value(ProducerId producer, ControlId control, float value);
  std::vector<ControlChange> clear_producer(ProducerId producer);
  std::vector<ControlChange> remove_producer(ProducerId producer); // Reports Reason::Cancelled.

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

using CaptureCallback = std::function<void(const PhysicalInput& input)>;

// Consumes the next key, mouse button, gamepad button or axis (|value| >= 0.5) from a
// physical source and reports it, e.g. for rebinding. Replaces any pending capture.
void capture_next(CaptureCallback callback);
void cancel_capture();
[[nodiscard]] bool capturing();

} // namespace aurora::binding

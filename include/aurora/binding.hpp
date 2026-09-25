#pragma once

#include <aurora/input.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::binding {

// Registry operations and each evaluator are confined to the input event thread.

// Process-local handles; persist names instead of numeric IDs.
using ControlId = uint64_t;
inline constexpr ControlId kInvalidControlId = 0;

enum class ControlKind {
  Button,
  Axis,
};

struct ControlDescriptor {
  // Stable, namespaced configuration key, e.g. "aurora.pad.a" or "dusklight.open_map".
  std::string name;
  ControlKind kind = ControlKind::Button;
};

// PAD registers its standard controls through this same API. Registration defines
// a target; consumers decide what its activation does. Names are copied and kept
// for the process lifetime. Registering the same name and kind returns the same
// ID; an empty name or a conflicting kind returns kInvalidControlId.
[[nodiscard]] ControlId register_control(const ControlDescriptor& desc);
[[nodiscard]] ControlId find_control(std::string_view name);

// find_control returns kInvalidControlId for an unknown name. describe_control
// returns null for an unknown ID; descriptors remain valid across registrations.
[[nodiscard]] const ControlDescriptor* describe_control(ControlId control);

// Runtime identities only. Saved profiles refer to logical control names and
// device selectors; resolve those to ControlId/SourceId when creating a BindingSet.
struct PhysicalInput {
  struct Key {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
  };

  struct MouseButton {
    uint8_t button = 0; // SDL_BUTTON_*; zero is invalid.
  };

  struct GamepadButton {
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
  };

  struct GamepadAxis {
    enum class Direction {
      Full,     // Keep the signed value.
      Positive, // max(value, 0).
      Negative, // max(-value, 0).
    };

    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
    Direction direction = Direction::Full;

    // Applies the direction to a signed axis value.
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
  float threshold = 0.5f; // (0, 1], after axis direction selection; buttons simply require pressed.
};

struct Binding {
  PhysicalInput input;
  ControlId target = kInvalidControlId;

  // All must be held for this binding to contribute. Only input admitted to this
  // evaluator qualifies; never consult SDL/global state. Reevaluate immediately
  // when any constituent changes, with no delay, reservation, or overlap suppression.
  std::vector<HeldInput> held;

  // Applied to the primary input after axis direction selection. Values within
  // deadZone become zero; the remaining range is not rescaled. Buttons supply 0/1.
  float deadZone = 0.f;   // [0, 1).
  float scale = 1.f;      // Axis targets: multiply and clamp to [-1, 1]; supports inversion and key-to-axis input.
  float threshold = 0.5f; // (0, 1]; button targets driven by an axis activate when value >= threshold.
};

// Publish an immutable snapshot for UI and gameplay to share. Each State keeps
// independent admitted input and binding contributions, scoped to one player/context.
struct BindingSet {
  std::vector<Binding> bindings;

  // Whether any binding's primary or held input comes from the source.
  [[nodiscard]] bool references(input::SourceId source) const;
};

struct ControlChange {
  enum class Reason {
    Input,
    Cancelled,
  };

  ControlId control = kInvalidControlId;
  float previousValue = 0.f;
  float value = 0.f; // Buttons: 0 or 1. Axes: [-1, 1], including nonnegative trigger values.
  Reason reason = Reason::Input;
};

// Identifies a logical producer within one State (e.g. a touch control or the
// PADSetVirtualStatus shim). Zero is invalid; IDs are not reused within a State.
using ProducerId = uint64_t;
inline constexpr ProducerId kInvalidProducerId = 0;

struct MappingResult {
  // Targets of every binding whose primary or held input the event addressed, in
  // binding order without duplicates, even if held conditions were false or the
  // logical value did not change. This is for UI fallback decisions (e.g. whether
  // the input has a navigation meaning); matching does not consume the event.
  std::vector<ControlId> targets;
  std::vector<ControlChange> changes;

  [[nodiscard]] bool matched() const { return !targets.empty(); }
};

class State {
public:
  // Null is an empty binding set. Binding sets must not be mutated while in use.
  explicit State(std::shared_ptr<const BindingSet> bindings = {});
  ~State();
  State(State&&) noexcept;
  State& operator=(State&&) noexcept;
  State(const State&) = delete;
  State& operator=(const State&) = delete;

  // Feed only this consumer's routed events, including targeted Cancelled events.
  // source.id must equal event.source. Keyboard repeats do not retrigger bindings;
  // text, scroll, touch, and pointer motion do not participate in this initial mapper.
  // Changes describe aggregate logical values: button contributions are ORed;
  // axis contributions are summed and clamped to [-1, 1]. Cancelling one input
  // removes only affected contributions, preserving other inputs to the same target.
  [[nodiscard]] MappingResult process(const input::InputSource& source, const input::InputEvent& event);

  // Current aggregate value; zero for neutral or unknown controls. Reading does
  // not consume changes or advance state, so repeated PAD reads are harmless.
  [[nodiscard]] float value(ControlId control) const;

  // Replacing definitions cancels old contributions and clears remembered input.
  // Returns resulting changes with Reason::Cancelled. The input router owns
  // held-input suppression and any deliberate resampling into the new state.
  [[nodiscard]] std::vector<ControlChange> set_bindings(std::shared_ptr<const BindingSet> bindings);

  // Same cancellation behavior while retaining definitions. Producer values are
  // cleared as well; producers stay registered. Forward the returned changes to
  // consumers without treating them as ordinary releases/activations.
  [[nodiscard]] std::vector<ControlChange> reset();

  [[nodiscard]] const std::shared_ptr<const BindingSet>& bindings() const;

  // Logical producers contribute already-mapped values that bypass physical
  // bindings (touch controls, virtual PAD input). Their values aggregate with
  // binding contributions under the same rules: buttons are active at >= 0.5 and
  // ORed; axes are summed and clamped. A one-shot action is set_value(1) followed
  // by set_value(0); each returns its own change, so event consumers see both even
  // when no poll happens between them.
  [[nodiscard]] ProducerId add_producer(std::string_view label);
  // Unknown producers or controls are ignored. Returns changes with Reason::Input.
  std::vector<ControlChange> set_value(ProducerId producer, ControlId control, float value);
  // Zeroes every value of the producer (Reason::Input), keeping it registered.
  std::vector<ControlChange> clear_producer(ProducerId producer);
  // Zeroes and unregisters the producer (Reason::Cancelled).
  std::vector<ControlChange> remove_producer(ProducerId producer);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace aurora::binding

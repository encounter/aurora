#pragma once

#include <aurora/binding.hpp>
#include <dolphin/pad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace aurora::pad {

// Canonical logical controls, registered as "aurora.pad.*" on first use.
struct Controls {
  // The twelve PADButton bits.
  binding::ControlId a = binding::kInvalidControlId;
  binding::ControlId b = binding::kInvalidControlId;
  binding::ControlId x = binding::kInvalidControlId;
  binding::ControlId y = binding::kInvalidControlId;
  binding::ControlId z = binding::kInvalidControlId;
  binding::ControlId start = binding::kInvalidControlId;
  binding::ControlId l = binding::kInvalidControlId;
  binding::ControlId r = binding::kInvalidControlId;
  binding::ControlId up = binding::kInvalidControlId;
  binding::ControlId down = binding::kInvalidControlId;
  binding::ControlId left = binding::kInvalidControlId;
  binding::ControlId right = binding::kInvalidControlId;
  // Full axes in [-1, 1], positive right and up (GC convention).
  binding::ControlId leftX = binding::kInvalidControlId;
  binding::ControlId leftY = binding::kInvalidControlId;
  binding::ControlId rightX = binding::kInvalidControlId;
  binding::ControlId rightY = binding::kInvalidControlId;
  // [0, 1].
  binding::ControlId triggerL = binding::kInvalidControlId;
  binding::ControlId triggerR = binding::kInvalidControlId;
  // "aurora.pad.ext.<name>" passthrough buttons, in PADExtButton bit order
  // (back, guide, misc1-6, paddles, stick clicks, touchpad).
  std::array<binding::ControlId, PAD_EXT_BUTTON_COUNT> ext{};
};

[[nodiscard]] const Controls& controls();
// kInvalidControlId for anything other than a single PADButton / PADExtButton bit.
[[nodiscard]] binding::ControlId control_for_button(PADButton button);
[[nodiscard]] binding::ControlId control_for_ext_button(PADExtButton button);

struct PortBindings {
  std::shared_ptr<const binding::BindingSet> set;
  // Changes whenever the port's set is rebuilt (reassignment, reconnect,
  // keyboard-mode toggle, mapping/dead-zone/action edits).
  uint64_t generation = 0;
};

// Resolved from the controller assigned to the port, the keyboard and mouse when
// keyboard mode is active for the port, the port's dead zones, and its action
// bindings. UI consumers hold their own binding::State over this set and rebuild
// it when the generation changes.
[[nodiscard]] PortBindings binding_set(uint32_t port);

// The controller source assigned to the port, or kInvalidSourceId.
[[nodiscard]] input::SourceId source(uint32_t port);
[[nodiscard]] bool keyboard_active(uint32_t port);

// Port-defined logical controls (e.g. "metaforce.open_menu") bound per port and
// merged into the port's binding set. Sources in `input` and `held` are ignored
// and resolved from the control type: keys to the keyboard and mouse buttons to
// the mouse (both only while keyboard mode is active for the port), gamepad
// buttons/axes to the controller assigned to the port. Bindings that cannot be
// resolved are omitted until they can.
void set_action_bindings(uint32_t port, std::vector<binding::Binding> bindings);
[[nodiscard]] const std::vector<binding::Binding>& action_bindings(uint32_t port);

} // namespace aurora::pad

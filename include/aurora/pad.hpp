#pragma once

#include <aurora/binding.hpp>
#include <dolphin/pad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace aurora::pad {

struct Controls {
  // PADButton bits
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
  // [-1, 1], positive right and up
  binding::ControlId leftX = binding::kInvalidControlId;
  binding::ControlId leftY = binding::kInvalidControlId;
  binding::ControlId rightX = binding::kInvalidControlId;
  binding::ControlId rightY = binding::kInvalidControlId;
  // [0, 1]
  binding::ControlId triggerL = binding::kInvalidControlId;
  binding::ControlId triggerR = binding::kInvalidControlId;
  // PADExtButton bits
  std::array<binding::ControlId, PAD_EXT_BUTTON_COUNT> ext{};
};

[[nodiscard]] const Controls& controls();
[[nodiscard]] binding::ControlId control_for_button(PADButton button);
[[nodiscard]] binding::ControlId control_for_ext_button(PADExtButton button);

// Bindings for a port, including its controller mapping, keyboard and mouse when keyboard mode is on,
// and action bindings. A new set is published when any of those change, so compare pointers to notice.
[[nodiscard]] std::shared_ptr<const binding::BindingSet> binding_set(uint32_t port);

// Bindings that a controller would use if not assigned to a port. (Its own mapping or defaults)
[[nodiscard]] std::shared_ptr<const binding::BindingSet> controller_binding_set(input::SourceId controller);

// Extra per-port bindings for game-defined controls, e.g. "metaforce.open_menu".
void set_action_bindings(uint32_t port, std::vector<binding::Binding> bindings);

} // namespace aurora::pad

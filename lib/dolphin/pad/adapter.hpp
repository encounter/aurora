#pragma once

#include "../../gamepad.hpp"

#include <aurora/input.hpp>
#include <dolphin/pad.h>

#include <array>
#include <cstdint>

struct PADKeyboardState {
  std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> m_buttonMapping{};
  std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> m_axisMapping{};
  bool m_mappingsSet = false;
};

// PAD adapter for aurora::input
namespace aurora::pad::detail {

void initialize();
bool read(uint32_t port, PADStatus& status);
void set_virtual_status(uint32_t port, const PADStatus* status);
void set_blocked(bool blocked);
bool consume_cancellation(uint32_t port);
bool captured(uint32_t port);
input::SourceId controller_source(uint32_t port);

const PADKeyboardState& keyboard_state(uint32_t port);
void ensure_mapping_loaded(gamepad::GameController* controller);
const std::array<PADButtonMapping, PAD_BUTTON_COUNT>& default_buttons(const gamepad::GameController& controller);
const std::array<PADAxisMapping, PAD_AXIS_COUNT>& default_axes();

} // namespace aurora::pad::detail

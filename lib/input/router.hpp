#pragma once

#include <aurora/input.hpp>

union SDL_Event;

// Internal router entry points used by Aurora's SDL event pump, device
// management, and tests. Ports use the public aurora/input.hpp API.
namespace aurora::input::detail {

// Creates the process-wide keyboard, mouse, and touch sources if needed.
void ensure_initialized();

// Physical controller lifecycle. add_gamepad returns the existing source when the
// joystick is already known.
InputSource add_gamepad(SDL_JoystickID joystick, std::string_view label);
void remove_gamepad(SDL_JoystickID joystick);

// Updates raw source state without routing, e.g. the initial state of a newly
// connected controller.
void seed_button(SourceId source, SDL_GamepadButton button, bool pressed);
void seed_axis(SourceId source, SDL_GamepadAxis axis, float value);

// Routes an event from a physical or synthetic source. `raw` is the SDL event the
// input was normalized from; Aurora's own adapters (ImGui) read it through
// current_sdl_event() while their callback runs.
void dispatch(const InputSource& source, InputEvent event, const SDL_Event* raw = nullptr);
const SDL_Event* current_sdl_event();

// Cancels keyboard, mouse, and touch state in every layer (Reason::FocusLost).
void focus_lost();

// Drops all layers, sources, and routes. Tests only.
void reset();

} // namespace aurora::input::detail

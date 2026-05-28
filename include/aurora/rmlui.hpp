#pragma once

#ifdef AURORA_ENABLE_RMLUI

#include <RmlUi/Core/Context.h>

namespace aurora::rmlui {

enum class InputType {
  Text,
  Number,
};

inline constexpr const char* TouchStartEvent = "touchstart";
inline constexpr const char* TouchMoveEvent = "touchmove";
inline constexpr const char* TouchEndEvent = "touchend";
inline constexpr const char* TouchCancelEvent = "touchcancel";

Rml::Context* get_context() noexcept;
bool is_initialized() noexcept;
void set_input_type(InputType type) noexcept;
void set_ui_scale(float scale) noexcept;
float get_ui_scale() noexcept;

} // namespace aurora::rmlui

#endif

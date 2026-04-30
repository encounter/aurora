#pragma once

#ifdef AURORA_ENABLE_RMLUI

#include <RmlUi/Core/Context.h>

namespace aurora::rmlui {

enum class InputType {
  Text,
  Number,
};

Rml::Context* get_context() noexcept;
bool is_initialized() noexcept;
void set_input_type(InputType type) noexcept;

} // namespace aurora::rmlui

#endif

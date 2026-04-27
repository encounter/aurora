#pragma once

#ifdef AURORA_ENABLE_RMLUI

#include <RmlUi/Core/Context.h>

namespace aurora::rmlui {

Rml::Context* get_context() noexcept;
bool is_initialized() noexcept;

} // namespace aurora::rmlui

#endif

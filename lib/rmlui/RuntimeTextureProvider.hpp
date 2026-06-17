#pragma once

#include <aurora/rmlui.hpp>

#include <optional>
#include <string_view>

namespace aurora::rmlui {

std::optional<RuntimeTexture> load_runtime_texture(std::string_view source);

} // namespace aurora::rmlui

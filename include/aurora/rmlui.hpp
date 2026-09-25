#pragma once

#ifdef AURORA_ENABLE_RMLUI

#include <aurora/input.hpp>

#include <RmlUi/Core/Context.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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
void set_glass_light_dir(float x, float y) noexcept;

struct InputResult {
  // An RmlUi element stopped the event's propagation.
  bool handled = false;
  // Pointer-down only: the element under the pointer, or null outside any element
  // or the UI viewport.
  Rml::Element* target = nullptr;
};

// Processes routed keyboard, text, mouse, scroll, and touch input into the RmlUi
// context, mapping SDL window coordinates into the UI viewport. Aurora does not
// register an RmlUi layer: the owner of UI policy (e.g. borealis::ui) registers
// one, calls this, and decides consumption from the result and its document
// model. Controller input is ignored; UI adapters translate it to navigation.
// Cancelled clears mouse buttons and touches without activating clicks.
InputResult process_input(const input::InputSource& source, const input::InputEvent& event) noexcept;

struct RuntimeTexture {
  uint32_t width = 0;
  uint32_t height = 0;
  std::span<const std::byte> rgba8;
  bool premultipliedAlpha = false;
  bool generateMipmaps = false;
};

using TextureProvider = std::function<std::optional<RuntimeTexture>(std::string_view)>;

void register_texture_provider(std::string scheme, TextureProvider provider);
void unregister_texture_provider(std::string_view scheme) noexcept;

} // namespace aurora::rmlui

#endif

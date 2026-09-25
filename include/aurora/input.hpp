#pragma once

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Aurora's input routing system.
//
// Sources (keyboard, mouse, touch, each controller) emit events. Layers are registered at a specific priority. An event
// is processed until a layer consumes it. A layer can "capture" a source to block it for every layer below.
//
// The router keeps each layer's view of a source consistent: a layer that saw a press also sees its release, a layer
// that loses a source gets Cancelled, and a layer that regains a source doesn't see presses that are still held.

namespace aurora::input {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

template <typename... Ts>
struct Enum {
  std::variant<Ts...> value;

  constexpr Enum() = default;

  template <typename T>
    requires std::constructible_from<std::variant<Ts...>, T&&>
  constexpr Enum(T&& value) : value(std::forward<T>(value)) {}

  template <typename... Fs>
  constexpr decltype(auto) match(Fs&&... fs) & {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, value);
  }

  template <typename... Fs>
  constexpr decltype(auto) match(Fs&&... fs) const& {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, value);
  }

  template <typename... Fs>
  constexpr decltype(auto) match(Fs&&... fs) && {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, std::move(value));
  }

  template <typename T>
  [[nodiscard]] constexpr bool is() const {
    return std::holds_alternative<T>(value);
  }

  template <typename T>
  [[nodiscard]] constexpr const T* get_if() const {
    return std::get_if<T>(&value);
  }
};

// Runtime source ID.
using SourceId = uint64_t;
inline constexpr SourceId kInvalidSourceId = 0;

// Touch contact ID. Zero for mouse.
using PointerId = uint64_t;

struct InputSource {
  enum class Kind {
    Keyboard,
    Mouse,
    Controller,
    Touch,
  };

  enum class Origin {
    Physical,
    Synthetic,
  };

  SourceId id = kInvalidSourceId;
  Kind kind = Kind::Keyboard;
  Origin origin = Origin::Physical;

  bool operator==(const InputSource&) const = default;
};

struct InputEvent {
  struct KeyChanged {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN; // Used for bindings
    SDL_Keycode keycode = SDLK_UNKNOWN;           // Layout-dependent, e.g. for UI shortcuts
    SDL_Keymod modifiers = SDL_KMOD_NONE;
    bool pressed = false;
    bool repeat = false;
  };

  struct ButtonChanged {
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    bool pressed = false;
  };

  struct AxisChanged {
    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
    float value = 0.f; // sticks: [-1, 1], positive right/down, triggers: [0, 1]
  };

  // Mouse and touch, using SDL window coordinates.
  struct PointerChanged {
    enum class Phase {
      Move,
      Down,
      Up,
      Cancel,
    };

    PointerId pointer = 0;
    Phase phase = Phase::Move;
    SDL_FPoint position{};
    SDL_FPoint delta{};
    uint8_t button = 0;   // SDL_BUTTON_* for mouse Down/Up
    float pressure = 0.f; // Touch only
    SDL_Keymod modifiers = SDL_KMOD_NONE;
  };

  struct Scroll {
    SDL_FPoint position{};
    SDL_FPoint delta{}; // Positive right/down
    SDL_Keymod modifiers = SDL_KMOD_NONE;
  };

  struct TextInput {
    std::string text;
  };

  // IME composition. Empty text ends it.
  struct TextEditing {
    std::string text;
    int32_t start = -1;
    int32_t length = -1;
  };

  // Drop held state from the source.
  struct Cancelled {};

  struct SourceChanged {
    enum class Change {
      Connected,
      Disconnected,
      Remapped, // SDL updated the controller's mapping
    };

    Change change = Change::Connected;
  };

  using Payload = Enum<KeyChanged, ButtonChanged, AxisChanged, PointerChanged, Scroll, TextInput, TextEditing,
                       Cancelled, SourceChanged>;

  InputSource source;
  uint64_t timestampNs = 0;
  Payload payload = KeyChanged{};
};

enum class EventResult {
  Pass,
  Consume,
};

using LayerId = uint64_t;
inline constexpr LayerId kInvalidLayerId = 0;

// Highest runs first. Applications can use any priority in between.
inline constexpr int32_t kGameLayerPriority = 0;
inline constexpr int32_t kRmlUiLayerPriority = 100;
inline constexpr int32_t kImGuiLayerPriority = 200;

inline constexpr float kAxisNeutral = 0.25f;

using LayerCallback = EventResult (*)(const InputEvent& event, void* userdata);

// True blocks the source for every layer below. The router calls this repeatedly,
// so it must be cheap and have no side effects.
using LayerCaptureQuery = bool (*)(const InputSource& source, void* userdata);

enum class PointerMode {
  None,     // No preference
  Visible,  // Always visible
  AutoHide, // Hidden after two seconds without mouse input
  Hidden,   // Always hidden
  Relative, // Hidden and locked to the window (use PointerChanged::delta)
};

// Same rules as LayerCaptureQuery.
using LayerPointerQuery = PointerMode (*)(void* userdata);

struct LayerDescriptor {
  const char* label = nullptr;
  int32_t priority = kGameLayerPriority;
  LayerCallback onEvent = nullptr;
  LayerCaptureQuery capturesSource = nullptr;
  LayerPointerQuery pointerMode = nullptr;
  void* userdata = nullptr;
  bool enabled = true;
};

[[nodiscard]] LayerId register_layer(const LayerDescriptor& desc);
void unregister_layer(LayerId layer);
void set_layer_enabled(LayerId layer, bool enabled);

// Re-evaluates captures and sends any cancellations.
void reconcile();

// Whether a layer above `priority` captures the source.
[[nodiscard]] bool captured_above(SourceId source, int32_t priority);

// The preference of the highest layer that still receives the mouse, or Visible.
[[nodiscard]] PointerMode pointer_mode();

[[nodiscard]] InputSource keyboard_source();
[[nodiscard]] InputSource mouse_source();
[[nodiscard]] InputSource touch_source();
[[nodiscard]] std::vector<InputSource> sources();
[[nodiscard]] std::optional<InputSource> find_source(SourceId source);
// The controller's name, or the name given to create_source. Kept after disconnect.
[[nodiscard]] std::string source_name(SourceId source);
[[nodiscard]] SourceId source_for_gamepad(SDL_JoystickID joystick);
[[nodiscard]] SDL_JoystickID gamepad_for_source(SourceId source);

// A synthetic source, e.g. a virtual controller. For input that's already logical (e.g.
// a touch button for PAD A), use a binding::State producer instead.
[[nodiscard]] InputSource create_source(InputSource::Kind kind, std::string_view name);
void destroy_source(SourceId source);
// Routes an event from the synthetic source event.source.id. Ignored inside a callback.
void inject(InputEvent event);

// The device's actual state, regardless of routing. For compatibility purposes; layers
// should collect events into a binding::State instead of polling.
[[nodiscard]] bool raw_button_pressed(SourceId source, SDL_GamepadButton button);
[[nodiscard]] float raw_axis(SourceId source, SDL_GamepadAxis axis);

} // namespace aurora::input

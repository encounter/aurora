#pragma once

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_video.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace aurora::binding {
struct PhysicalInput;
} // namespace aurora::binding

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

// IDs are allocated by Aurora independently of SDL device IDs. Each connection or
// synthetic producer gets a distinct ID; zero is invalid and IDs are not reused.
using SourceId = uint64_t;
inline constexpr SourceId kInvalidSourceId = 0;

// Scoped to a source. Mouse events use zero; touch contacts retain their own IDs.
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
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN; // Physical identity used for bindings and ownership.
    SDL_Keycode keycode = SDLK_UNKNOWN;           // Layout-dependent key used for UI shortcuts.
    SDL_Keymod modifiers = SDL_KMOD_NONE;
    bool pressed = false;
    bool repeat = false; // Repeats do not create another held press.
  };

  struct ButtonChanged {
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    bool pressed = false;
  };

  struct AxisChanged {
    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
    // Sticks: [-1, 1], positive right/down. Triggers: [0, 1]. Before gameplay dead zones.
    float value = 0.f;
  };

  struct PointerChanged {
    enum class Phase {
      Move,
      Down,
      Up,
      Cancel,
    };

    PointerId pointer = 0;
    Phase phase = Phase::Move;
    // SDL window coordinates, positive right/down; consumers apply their viewport/DPI mapping.
    SDL_FPoint position{};
    SDL_FPoint delta{};   // Per-event motion, also used for relative mouse input.
    uint8_t button = 0;   // SDL_BUTTON_* for mouse down/up; zero for motion and touch.
    float pressure = 0.f; // Touch pressure in [0, 1]; zero when unavailable.
    SDL_Keymod modifiers = SDL_KMOD_NONE;
  };

  struct Scroll {
    SDL_FPoint position{}; // SDL window coordinates.
    SDL_FPoint delta{};    // Scroll units, positive right/down, with SDL's flipped direction resolved.
    SDL_Keymod modifiers = SDL_KMOD_NONE;
  };

  // Own UTF-8 text so queued events do not borrow SDL's event storage.
  struct TextInput {
    std::string text;
  };

  struct TextEditing {
    std::string text; // Uncommitted IME composition; empty text clears the composition.
    int32_t start = -1;
    int32_t length = -1; // Selection offsets are in characters, not UTF-8 bytes; -1 means unset.
  };

  // Delivered directly to affected consumers, bypassing consumption. Clear the
  // indicated state without activating release-triggered actions.
  struct Cancelled {
    enum class Reason {
      FocusLost,
      SourceRemoved,
      LayerChanged,
    };

    struct All {}; // All state belonging to event.source.
    struct Key {
      SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    };
    struct Button {
      SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    };
    struct Axis {
      SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
    };
    struct Pointer {
      PointerId pointer = 0;
    };

    Reason reason = Reason::LayerChanged;
    Enum<All, Key, Button, Axis, Pointer> target = All{};
  };

  SourceId source = kInvalidSourceId;
  SDL_WindowID window = 0;  // Zero for input without a target window, such as controller input.
  uint64_t timestampNs = 0; // Monotonic nanoseconds in the SDL_GetTicksNS time base.
  Enum<KeyChanged, ButtonChanged, AxisChanged, PointerChanged, Scroll, TextInput, TextEditing, Cancelled> payload =
      KeyChanged{};
};

// For a new interaction, visit enabled layers in priority order until Consume.
// The router remembers every layer that received the initial press/down. Those
// layers receive its repeats, motion, and release/cancel; their later return
// values do not change the route. A new layer cannot inherit an ongoing press.
// Mouse buttons share a pointer route until all buttons are up; each touch has
// its own route. Hover, scroll, text, and axis samples are routed individually.
// If an axis stops reaching a layer, that layer receives Cancelled::Axis so its
// last nonzero value cannot get stuck. Other controls from that source survive.
enum class EventResult {
  Pass,
  Consume,
};

using LayerId = uint64_t;
inline constexpr LayerId kInvalidLayerId = 0;

// Ordering conventions, not a closed set of layers. Applications can insert
// layers between these priorities. Higher values run first; ties retain
// registration order. Aurora's adapters use the same registration API.
inline constexpr int32_t kGameLayerPriority = 0;
inline constexpr int32_t kRmlUiLayerPriority = 100;
inline constexpr int32_t kImGuiLayerPriority = 200;

// Called on the event thread with borrowed arguments; source.id == event.source.
// Cancelled is delivered directly to affected layers, bypassing capture and
// consumption, and its return value is ignored. Handlers must clear their cached
// state without performing release actions (e.g. activating a clicked button).
using LayerCallback = EventResult (*)(const InputSource& source, const InputEvent& event, void* userdata);

// Query the layer's current policy directly (e.g. ImGui IO flags or RmlUi document
// state). True blocks this source below the layer, even if onEvent returns Pass;
// higher layers are unaffected. Null means no source-wide capture. Inspect kind,
// id, or origin to choose which sources to capture. Individual keys or touches
// can instead be consumed by onEvent without capturing the entire source.
//
// Called on the event thread for enabled layers. Must be a side-effect-free query;
// do not dispatch events or change registrations here. The router reevaluates
// policy between events and before exposing polled input state, including when
// no events arrive. Layers do not have to push or synchronize capture flags.
using LayerCaptureQuery = bool (*)(const InputSource& source, void* userdata);

struct LayerDescriptor {
  const char* label = nullptr;
  int32_t priority = kGameLayerPriority;
  LayerCallback onEvent = nullptr;
  LayerCaptureQuery capturesSource = nullptr;
  void* userdata = nullptr;
  bool enabled = true;
};

// All operations run on the event thread. Registrations and changes to enabled
// state made inside an event callback take effect after the current event.
// Policy is also reevaluated then, so opening a modal in a callback can cancel
// lower layers before the next event or poll. Cancellation is delivered outside
// active callbacks, without recursively invoking handlers.
//
// Capture transitions:
// - Capture begins: every layer newly below the barrier receives
//   Cancelled::All (Reason::LayerChanged) for the captured source, which covers
//   held keys, buttons, nonzero axes, active pointers, and accumulated motion.
// - Capture ends: held keys, buttons, and contacts are not replayed; a newly
//   unblocked layer sees them again only after a fresh press. Stick axes (left/right
//   X/Y) are resampled from the last raw value as synthetic AxisChanged events.
//   Triggers are treated like buttons: they reach the layer again only after a
//   sample at or near neutral.
// - Registering or enabling a layer follows the capture-end rules.
// - Focus loss and source removal deliver Cancelled::All with the matching reason.
//
// Copies the descriptor and label; userdata is borrowed until unregistration.
// Null onEvent returns kInvalidLayerId. IDs are never reused.
[[nodiscard]] LayerId register_layer(const LayerDescriptor& desc);

// Retires both callbacks before returning, including when called during dispatch;
// no later callback may use its userdata. Removing a layer never transfers its
// ongoing interactions to lower layers, and the removed layer receives no final
// cancellation. Invalid/stale IDs are a no-op here and in set_layer_enabled.
void unregister_layer(LayerId layer);

// Disabling delivers Cancelled::All to this layer for every source where it holds
// routed presses, contacts, or nonzero axes, and removes its capture barrier.
// Re-enabling waits for fresh presses; held buttons and contacts are not replayed.
void set_layer_enabled(LayerId layer, bool enabled);

// Reevaluates every capture query and delivers resulting transitions. Aurora
// calls this after each dispatched event, at the end of aurora_update(), and at
// the start of PADRead. Consumers polling their own binding::State should call it
// before reading so a UI change without new events is observed. No-op when called
// from inside a layer callback.
void reconcile();

// True when an enabled layer with a priority above `priority` currently captures
// the source. For example, a camera controller can ask whether the mouse is
// captured above kGameLayerPriority instead of enumerating open menus.
[[nodiscard]] bool captured_above(SourceId source, int32_t priority);

// --- Sources ---

// The process-wide keyboard, mouse, and touch sources. They exist for the life of
// the process regardless of how many physical devices SDL reports.
[[nodiscard]] InputSource keyboard_source();
[[nodiscard]] InputSource mouse_source();
[[nodiscard]] InputSource touch_source();

// Live sources, including synthetic ones. Removed sources are not returned.
[[nodiscard]] std::vector<InputSource> sources();
[[nodiscard]] std::optional<InputSource> find_source(SourceId source);
[[nodiscard]] bool source_connected(SourceId source);
// Label supplied at creation (controller name for physical controllers).
[[nodiscard]] std::string source_label(SourceId source);

// Bridges SDL gamepad identities for the PAD port adapter and diagnostics.
// Return kInvalidSourceId / 0 when there is no live mapping.
[[nodiscard]] SourceId source_for_gamepad(SDL_JoystickID joystick);
[[nodiscard]] SDL_JoystickID gamepad_for_source(SourceId source);

// Creates a synthetic source (Origin::Synthetic). Events for it enter the router
// through inject(). A virtual controller emits ButtonChanged/AxisChanged; already
// logical output (e.g. a touch control producing PAD A) should use a
// binding::State producer instead.
[[nodiscard]] InputSource create_source(InputSource::Kind kind, std::string_view label);

// Removes a synthetic source. Layers holding its state receive
// Cancelled::All (Reason::SourceRemoved). Physical sources are ignored.
void destroy_source(SourceId source);

// Routes an event from a synthetic source as if it had come from SDL. The
// event's source field is overwritten with source.id. Cancelled payloads and
// events for physical or unknown sources are ignored. Must not be called from
// inside a layer callback; such calls are ignored.
void inject(const InputSource& source, const InputEvent& event);

// --- Raw source state ---
// Follows the source regardless of routing or consumption. Intended for
// diagnostics and compatibility shims; consumers must use their admitted input.

[[nodiscard]] bool raw_key_pressed(SDL_Scancode scancode);
[[nodiscard]] bool raw_mouse_button_pressed(uint8_t button);
[[nodiscard]] bool raw_button_pressed(SourceId source, SDL_GamepadButton button);
// Normalized like AxisChanged::value.
[[nodiscard]] float raw_axis(SourceId source, SDL_GamepadAxis axis);

// --- Rebinding ---

// Called once with the first press or axis pull from any physical source.
using PhysicalInputCallback = void (*)(const binding::PhysicalInput& input, void* userdata);

// Registers a temporary top-priority layer that consumes the first key press,
// mouse button press, gamepad button press, or axis pull (|value| >= 0.5; triggers
// positive only) from any physical source, reports it, and unregisters. An axis
// pull is consumed until it returns near neutral so settings UI below does not
// react to it. Returns the watcher's layer ID; unregister_layer cancels it.
[[nodiscard]] LayerId watch_next_physical(PhysicalInputCallback callback, void* userdata);

} // namespace aurora::input

#include "router.hpp"

#include "source_state.hpp"

#include <aurora/binding.hpp>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cmath>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aurora::input {
namespace {

using Cancelled = InputEvent::Cancelled;

// Matches the PAD trigger clamp floor (30/255) that previously released
// analog-trigger suppression after PADBlockInput.
constexpr float kTriggerNeutral = 30.f / 255.f;
constexpr float kWatchAxisPull = 0.5f;
constexpr float kWatchAxisRelease = 0.25f;

constexpr std::array kStickAxes{
    SDL_GAMEPAD_AXIS_LEFTX,
    SDL_GAMEPAD_AXIS_LEFTY,
    SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,
};
constexpr std::array kTriggerAxes{
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

struct Layer {
  LayerId id = kInvalidLayerId;
  std::string label;
  int32_t priority = kGameLayerPriority;
  LayerCallback onEvent = nullptr;
  LayerCaptureQuery capturesSource = nullptr;
  void* userdata = nullptr;
  bool enabled = true;
  bool retired = false;
  // Router-owned userdata (rebinding watchers), released with the layer.
  std::shared_ptr<void> owned;

  [[nodiscard]] bool active() const { return enabled && !retired; }
  [[nodiscard]] bool captures(const InputSource& source) const {
    return capturesSource != nullptr && capturesSource(source, userdata);
  }
};

// How one layer currently relates to one source.
struct LayerState {
  bool blocked = false;                            // Below a capture barrier.
  std::bitset<SDL_GAMEPAD_AXIS_COUNT> heldAxes;    // Last received a nonzero sample.
  std::bitset<SDL_GAMEPAD_AXIS_COUNT> neutralAxes; // Skips the trigger until a near-neutral sample.
};

struct Source {
  InputSource info;
  std::string label;
  SDL_JoystickID joystick = 0;
  bool live = true;
  // Follows the device regardless of routing.
  SourceState raw;
  // An entry appears when a layer is first reconciled against this source.
  std::unordered_map<LayerId, LayerState> layers;

  [[nodiscard]] bool blocked(LayerId layer) const {
    const auto it = layers.find(layer);
    return it != layers.end() && it->second.blocked;
  }
};

enum class RouteKind : uint8_t {
  Key,
  Button,
  Pointer,
};

// The layers that received an interaction's initial press, in delivery order.
struct Route {
  SourceId source = kInvalidSourceId;
  RouteKind kind = RouteKind::Key;
  uint64_t code = 0;
  std::vector<LayerId> layers;
  uint32_t mouseButtons = 0; // Mouse pointer routes last until every button is up.

  [[nodiscard]] bool is(SourceId otherSource, RouteKind otherKind, uint64_t otherCode) const {
    return source == otherSource && kind == otherKind && code == otherCode;
  }
};

struct Delivery {
  LayerId layer = kInvalidLayerId;
  SourceId source = kInvalidSourceId;
  InputEvent event;
  bool resample = false;
};

InputEvent cancel_event(SourceId source, Cancelled::Reason reason,
                        decltype(Cancelled::target) target = Cancelled::All{}) {
  return {
      .source = source,
      .timestampNs = SDL_GetTicksNS(),
      .payload = Cancelled{.reason = reason, .target = std::move(target)},
  };
}

constexpr auto kVisitAll = [](const Layer&) { return false; };

struct Router {
  std::vector<Layer> layers; // Sorted by priority (descending); ties keep registration order.
  std::vector<std::unique_ptr<Source>> sources;
  std::vector<Route> routes;
  std::vector<Delivery> deliveries;
  // Changes requested from callbacks, applied after the current event.
  std::vector<Layer> pendingLayers;
  std::vector<std::pair<LayerId, bool>> pendingEnable;
  bool pendingRemoval = false;
  std::vector<SourceId> pendingSourceRemovals;
  LayerId nextLayer = 1;
  SourceId nextSource = 1;
  int depth = 0;
  bool draining = false;
  const SDL_Event* currentRaw = nullptr;
  SourceId keyboard = kInvalidSourceId;
  SourceId mouse = kInvalidSourceId;
  SourceId touch = kInvalidSourceId;

  [[nodiscard]] bool deferring() const { return depth > 0 || draining; }
  [[nodiscard]] bool has_pending() const {
    return !pendingLayers.empty() || !pendingEnable.empty() || pendingRemoval || !pendingSourceRemovals.empty();
  }

  Source* find_source(SourceId id) {
    const auto it = std::ranges::find(sources, id, [](const auto& source) { return source->info.id; });
    return it != sources.end() ? it->get() : nullptr;
  }

  Source* find_gamepad(SDL_JoystickID joystick) {
    const auto it = std::ranges::find_if(sources, [joystick](const auto& source) {
      return source->live && source->info.kind == InputSource::Kind::Controller &&
             source->info.origin == InputSource::Origin::Physical && source->joystick == joystick;
    });
    return it != sources.end() ? it->get() : nullptr;
  }

  Layer* find_layer(LayerId id) {
    const auto it = std::ranges::find(layers, id, &Layer::id);
    return it != layers.end() ? &*it : nullptr;
  }

  Layer* find_pending_layer(LayerId id) {
    const auto it = std::ranges::find(pendingLayers, id, &Layer::id);
    return it != pendingLayers.end() ? &*it : nullptr;
  }

  Source& add_source(InputSource::Kind kind, InputSource::Origin origin, std::string_view label) {
    auto& source = *sources.emplace_back(std::make_unique<Source>());
    source.info = {.id = nextSource++, .kind = kind, .origin = origin};
    source.label = label;
    return source;
  }

  void ensure_initialized() {
    if (keyboard != kInvalidSourceId) {
      return;
    }
    keyboard = add_source(InputSource::Kind::Keyboard, InputSource::Origin::Physical, "Keyboard").info.id;
    mouse = add_source(InputSource::Kind::Mouse, InputSource::Origin::Physical, "Mouse").info.id;
    touch = add_source(InputSource::Kind::Touch, InputSource::Origin::Physical, "Touch").info.id;
  }

  // --- Layer registration ---

  LayerId add_layer(Layer layer) {
    ensure_initialized();
    layer.id = nextLayer++;
    const LayerId id = layer.id;
    if (deferring()) {
      pendingLayers.push_back(std::move(layer));
    } else {
      insert(std::move(layer));
      drain();
    }
    return id;
  }

  void insert(Layer layer) {
    const auto it =
        std::ranges::find_if(layers, [&](const Layer& existing) { return existing.priority < layer.priority; });
    layers.insert(it, std::move(layer));
  }

  void remove_layer(LayerId id) {
    if (std::erase_if(pendingLayers, [id](const Layer& layer) { return layer.id == id; }) != 0) {
      return;
    }
    Layer* layer = find_layer(id);
    if (layer == nullptr || layer->retired) {
      return;
    }
    // Retiring stops callbacks immediately, even mid-dispatch; the entry is erased later.
    layer->retired = true;
    pendingRemoval = true;
    std::erase_if(pendingEnable, [id](const auto& change) { return change.first == id; });
    drain();
  }

  void set_layer_enabled(LayerId id, bool enabled) {
    if (Layer* pending = find_pending_layer(id)) {
      pending->enabled = enabled;
      return;
    }
    if (deferring()) {
      pendingEnable.emplace_back(id, enabled);
      return;
    }
    apply_enabled(id, enabled);
    drain();
  }

  void apply_enabled(LayerId id, bool enabled) {
    Layer* layer = find_layer(id);
    if (layer == nullptr || layer->retired || layer->enabled == enabled) {
      return;
    }
    if (!enabled) {
      for (const auto& source : sources) {
        if (source->live && holds_state(*source, id)) {
          queue(id, *source, cancel_event(source->info.id, Cancelled::Reason::LayerChanged));
        }
      }
      forget(id);
    }
    // Re-enabled layers have no per-source state, so reconciliation admits them afresh.
    layer->enabled = enabled;
  }

  void apply_pending() {
    for (const auto id : std::exchange(pendingSourceRemovals, {})) {
      if (Source* source = find_source(id); source != nullptr && source->live) {
        cancel_source(*source, Cancelled::Reason::SourceRemoved);
        source->live = false;
        source->layers.clear();
      }
    }
    if (std::exchange(pendingRemoval, false)) {
      for (const auto& layer : layers) {
        if (layer.retired) {
          forget(layer.id);
        }
      }
      std::erase_if(layers, [](const Layer& layer) { return layer.retired; });
    }
    for (auto& layer : std::exchange(pendingLayers, {})) {
      insert(std::move(layer));
    }
    for (const auto& [id, enabled] : std::exchange(pendingEnable, {})) {
      apply_enabled(id, enabled);
    }
  }

  // Removes every trace of a layer's interaction state.
  void forget(LayerId layer) {
    for (auto& source : sources) {
      strip_routes(source->info.id, layer);
      source->layers.erase(layer);
    }
  }

  void strip_routes(SourceId source, LayerId layer) {
    for (auto& route : routes) {
      if (route.source == source) {
        std::erase(route.layers, layer);
      }
    }
    std::erase_if(routes, [](const Route& route) { return route.layers.empty(); });
  }

  [[nodiscard]] bool holds_state(const Source& source, LayerId layer) const {
    if (const auto it = source.layers.find(layer); it != source.layers.end() && it->second.heldAxes.any()) {
      return true;
    }
    return std::ranges::any_of(routes, [&](const Route& route) {
      return route.source == source.info.id && std::ranges::find(route.layers, layer) != route.layers.end();
    });
  }

  // --- Reconciliation ---

  void drain() {
    if (deferring()) {
      return;
    }
    draining = true;
    // Bounded in case layers keep toggling state from their callbacks.
    for (int iteration = 0; iteration < 32; ++iteration) {
      apply_pending();
      for (auto& source : sources) {
        reconcile(*source);
      }
      if (deliveries.empty() && !has_pending()) {
        break;
      }
      deliver_pending();
    }
    draining = false;
  }

  void reconcile(Source& source) {
    if (!source.live) {
      return;
    }
    bool barrier = false;
    for (const auto& layer : layers) {
      if (!layer.active()) {
        continue;
      }
      const bool blocked = barrier;
      barrier = barrier || layer.captures(source.info);
      const auto it = source.layers.find(layer.id);
      if (it == source.layers.end()) {
        // First evaluation: the layer holds nothing for this source yet.
        if (blocked) {
          source.layers[layer.id].blocked = true;
        } else {
          admit(source, layer.id);
        }
      } else if (blocked && !it->second.blocked) {
        block(source, layer.id);
      } else if (!blocked && it->second.blocked) {
        admit(source, layer.id);
      }
    }
  }

  void block(Source& source, LayerId layer) {
    queue(layer, source, cancel_event(source.info.id, Cancelled::Reason::LayerChanged));
    strip_routes(source.info.id, layer);
    source.layers[layer] = {.blocked = true};
  }

  // The layer newly receives this source (capture ended, or the layer is new or
  // re-enabled): held buttons are not replayed, deflected sticks are resampled,
  // and triggers must return near neutral first.
  void admit(Source& source, LayerId layer) {
    auto& state = source.layers[layer];
    state = {};
    for (const auto axis : kStickAxes) {
      if (source.raw.axis(axis) != 0.f) {
        queue(layer, source,
              {
                  .source = source.info.id,
                  .timestampNs = SDL_GetTicksNS(),
                  .payload = InputEvent::AxisChanged{.axis = axis},
              },
              true);
      }
    }
    for (const auto axis : kTriggerAxes) {
      state.neutralAxes[axis] = source.raw.axis(axis) > kTriggerNeutral;
    }
  }

  void queue(LayerId layer, const Source& source, InputEvent event, bool resample = false) {
    deliveries.push_back({.layer = layer, .source = source.info.id, .event = std::move(event), .resample = resample});
  }

  void deliver_pending() {
    for (auto& delivery : std::exchange(deliveries, {})) {
      const Layer* layer = find_layer(delivery.layer);
      Source* source = find_source(delivery.source);
      if (layer == nullptr || layer->retired || source == nullptr) {
        continue;
      }
      if (!delivery.resample) {
        // Cancellations also reach layers that were just disabled.
        call(*layer, *source, delivery.event);
        continue;
      }
      // Resample with the current raw value, unless the layer stopped receiving the source.
      auto& axis = std::get<InputEvent::AxisChanged>(delivery.event.payload.value);
      axis.value = source->raw.axis(axis.axis);
      if (!layer->enabled || !source->live || source->blocked(layer->id) || axis.value == 0.f) {
        continue;
      }
      call(*layer, *source, delivery.event);
      source->layers[layer->id].heldAxes.set(axis.axis);
    }
  }

  // --- Routing ---

  EventResult call(const Layer& layer, const Source& source, const InputEvent& event) {
    // The layer vector is not mutated while callbacks run; retiring only sets a flag.
    ++depth;
    const EventResult result = layer.onEvent(source.info, event, layer.userdata);
    --depth;
    return result;
  }

  // Visits enabled layers above the capture barrier until one consumes.
  template <typename Skip = decltype(kVisitAll)>
  std::vector<LayerId> visit(const Source& source, const InputEvent& event, const Skip& skip = kVisitAll) {
    std::vector<LayerId> received;
    for (const auto& layer : layers) {
      if (!layer.active()) {
        continue;
      }
      if (source.blocked(layer.id)) {
        break;
      }
      if (skip(layer)) {
        continue;
      }
      received.push_back(layer.id);
      if (call(layer, source, event) == EventResult::Consume) {
        break;
      }
    }
    return received;
  }

  void deliver(const std::vector<LayerId>& receivers, const Source& source, const InputEvent& event) {
    for (const auto id : receivers) {
      if (const Layer* layer = find_layer(id); layer != nullptr && layer->active()) {
        call(*layer, source, event);
      }
    }
  }

  Route* find_route(SourceId source, RouteKind kind, uint64_t code) {
    const auto it = std::ranges::find_if(routes, [&](const Route& route) { return route.is(source, kind, code); });
    return it != routes.end() ? &*it : nullptr;
  }

  void end_route(SourceId source, RouteKind kind, uint64_t code) {
    std::erase_if(routes, [&](const Route& route) { return route.is(source, kind, code); });
  }

  void start_route(const Source& source, const InputEvent& event, RouteKind kind, uint64_t code,
                   uint32_t mouseButtons = 0) {
    if (auto receivers = visit(source, event); !receivers.empty()) {
      routes.push_back({
          .source = source.info.id,
          .kind = kind,
          .code = code,
          .layers = std::move(receivers),
          .mouseButtons = mouseButtons,
      });
    }
  }

  // Keys and gamepad buttons: presses create routes; repeats and releases follow them.
  void route_press(const Source& source, const InputEvent& event, RouteKind kind, uint64_t code, bool pressed,
                   bool repeat) {
    if (Route* route = find_route(source.info.id, kind, code)) {
      if (pressed) {
        deliver(route->layers, source, event);
      } else {
        const auto receivers = std::move(route->layers);
        end_route(source.info.id, kind, code);
        deliver(receivers, source, event);
      }
    } else if (pressed && !repeat) {
      // Releases and repeats without a route belong to no layer, e.g. a press that
      // happened while captured or before the layer existed.
      start_route(source, event, kind, code);
    }
  }

  void route_axis(Source& source, const InputEvent& event, const InputEvent::AxisChanged& axis) {
    if (!valid(axis.axis)) {
      return;
    }
    const bool neutral = std::abs(axis.value) <= kTriggerNeutral;
    const auto received = visit(source, event, [&](const Layer& layer) {
      auto& state = source.layers[layer.id];
      if (neutral) {
        state.neutralAxes.reset(axis.axis);
      }
      return state.neutralAxes.test(axis.axis);
    });
    // A layer that held a nonzero value but no longer receives the axis is
    // cancelled so the value cannot get stuck.
    for (auto& [id, state] : source.layers) {
      const bool receives = std::ranges::find(received, id) != received.end();
      if (state.heldAxes.test(axis.axis) && !receives) {
        queue(id, source, cancel_event(source.info.id, Cancelled::Reason::LayerChanged, Cancelled::Axis{axis.axis}));
      }
      state.heldAxes[axis.axis] = receives && axis.value != 0.f;
    }
  }

  void route_pointer(const Source& source, const InputEvent& event, const InputEvent::PointerChanged& pointer) {
    using Phase = InputEvent::PointerChanged::Phase;
    const bool mouse = source.info.kind == InputSource::Kind::Mouse;
    const uint64_t code = mouse ? 0 : pointer.pointer;
    Route* route = find_route(source.info.id, RouteKind::Pointer, code);
    switch (pointer.phase) {
    case Phase::Down:
      if (route == nullptr) {
        start_route(source, event, RouteKind::Pointer, code, mouse ? mouse_button_mask(pointer.button) : 0u);
      } else {
        route->mouseButtons |= mouse_button_mask(pointer.button);
        deliver(route->layers, source, event);
      }
      break;
    case Phase::Move:
      if (route != nullptr) {
        deliver(route->layers, source, event);
      } else if (mouse) {
        // Hover motion is routed individually.
        visit(source, event);
      }
      break;
    case Phase::Up:
    case Phase::Cancel:
      if (route != nullptr) {
        if (mouse && pointer.phase == Phase::Up) {
          route->mouseButtons &= ~mouse_button_mask(pointer.button);
        }
        if (!mouse || pointer.phase == Phase::Cancel || route->mouseButtons == 0) {
          const auto receivers = std::move(route->layers);
          end_route(source.info.id, RouteKind::Pointer, code);
          deliver(receivers, source, event);
        } else {
          deliver(route->layers, source, event);
        }
      }
      break;
    }
  }

  void route_event(Source& source, const InputEvent& event) {
    event.payload.match(
        [&](const InputEvent::KeyChanged& key) {
          if (valid(key.scancode)) {
            route_press(source, event, RouteKind::Key, key.scancode, key.pressed, key.repeat);
          }
        },
        [&](const InputEvent::ButtonChanged& button) {
          if (valid(button.button)) {
            route_press(source, event, RouteKind::Button, button.button, button.pressed, false);
          }
        },
        [&](const InputEvent::AxisChanged& axis) { route_axis(source, event, axis); },
        [&](const InputEvent::PointerChanged& pointer) { route_pointer(source, event, pointer); },
        [](const Cancelled&) {},
        // Scroll and text are routed individually.
        [&](const auto&) { visit(source, event); });
  }

  void dispatch(Source& source, InputEvent event, const SDL_Event* raw) {
    if (deferring() || !source.live || event.payload.is<Cancelled>()) {
      return;
    }
    event.source = source.info.id;
    if (event.timestampNs == 0) {
      event.timestampNs = SDL_GetTicksNS();
    }
    // Reconcile first so routing reflects UI changes made since the last event.
    drain();
    source.raw.apply(source.info.kind, event);
    currentRaw = raw;
    route_event(source, event);
    currentRaw = nullptr;
    drain();
  }

  // Cancels a whole source in every layer currently receiving it.
  void cancel_source(Source& source, Cancelled::Reason reason) {
    for (const auto& layer : layers) {
      if (layer.active() && !source.blocked(layer.id)) {
        queue(layer.id, source, cancel_event(source.info.id, reason));
      }
    }
    std::erase_if(routes, [&](const Route& route) { return route.source == source.info.id; });
    for (auto& state : source.layers | std::views::values) {
      state.heldAxes.reset();
      state.neutralAxes.reset();
    }
    source.raw = {};
  }

  // Deferred like layer removal: a callback may destroy a source while its routes
  // are being delivered.
  void remove_source(const Source& source) {
    pendingSourceRemovals.push_back(source.info.id);
    drain();
  }

  [[nodiscard]] bool captured_above(const Source& source, int32_t priority) const {
    for (const auto& layer : layers) {
      if (layer.priority <= priority) {
        break;
      }
      if (layer.active() && layer.captures(source.info)) {
        return true;
      }
    }
    return false;
  }
};

Router g_router;

struct Watcher {
  PhysicalInputCallback callback = nullptr;
  void* userdata = nullptr;
  LayerId layer = kInvalidLayerId;
  // After reporting an axis pull, consume that axis until it returns near neutral.
  SourceId drainSource = kInvalidSourceId;
  SDL_GamepadAxis drainAxis = SDL_GAMEPAD_AXIS_INVALID;

  [[nodiscard]] std::optional<binding::PhysicalInput> first_press(const InputSource& source,
                                                                  const InputEvent& event) const {
    using Physical = binding::PhysicalInput;
    return event.payload.match(
        [&](const InputEvent::KeyChanged& key) -> std::optional<Physical> {
          if (!key.pressed || key.repeat) {
            return std::nullopt;
          }
          return Physical{.source = source.id, .control = Physical::Key{.scancode = key.scancode}};
        },
        [&](const InputEvent::ButtonChanged& button) -> std::optional<Physical> {
          if (!button.pressed) {
            return std::nullopt;
          }
          return Physical{.source = source.id, .control = Physical::GamepadButton{.button = button.button}};
        },
        [&](const InputEvent::PointerChanged& pointer) -> std::optional<Physical> {
          if (source.kind != InputSource::Kind::Mouse || pointer.phase != InputEvent::PointerChanged::Phase::Down ||
              pointer.button == 0) {
            return std::nullopt;
          }
          return Physical{.source = source.id, .control = Physical::MouseButton{.button = pointer.button}};
        },
        [&](const InputEvent::AxisChanged& axis) -> std::optional<Physical> {
          using Direction = Physical::GamepadAxis::Direction;
          const bool trigger =
              axis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
          if (axis.value >= kWatchAxisPull) {
            return Physical{.source = source.id,
                            .control = Physical::GamepadAxis{.axis = axis.axis, .direction = Direction::Positive}};
          }
          if (axis.value <= -kWatchAxisPull && !trigger) {
            return Physical{.source = source.id,
                            .control = Physical::GamepadAxis{.axis = axis.axis, .direction = Direction::Negative}};
          }
          return std::nullopt;
        },
        [](const auto&) -> std::optional<Physical> { return std::nullopt; });
  }

  static EventResult on_event(const InputSource& source, const InputEvent& event, void* userdata) {
    auto& watcher = *static_cast<Watcher*>(userdata);
    // Cancellations and resampled stick positions arrive while the router drains;
    // only input routed after registration counts.
    if (g_router.draining || event.payload.is<Cancelled>()) {
      return EventResult::Pass;
    }
    if (watcher.drainSource != kInvalidSourceId) {
      const auto* axis = event.payload.get_if<InputEvent::AxisChanged>();
      if (axis == nullptr || source.id != watcher.drainSource || axis->axis != watcher.drainAxis) {
        return EventResult::Pass;
      }
      if (std::abs(axis->value) < kWatchAxisRelease) {
        unregister_layer(watcher.layer);
      }
      return EventResult::Consume;
    }
    if (source.origin != InputSource::Origin::Physical) {
      return EventResult::Pass;
    }
    const auto found = watcher.first_press(source, event);
    if (!found) {
      return EventResult::Pass;
    }
    if (const auto* axis = found->control.get_if<binding::PhysicalInput::GamepadAxis>()) {
      watcher.drainSource = source.id;
      watcher.drainAxis = axis->axis;
    } else {
      unregister_layer(watcher.layer);
    }
    watcher.callback(*found, watcher.userdata);
    return EventResult::Consume;
  }
};

} // namespace

LayerId register_layer(const LayerDescriptor& desc) {
  if (desc.onEvent == nullptr) {
    return kInvalidLayerId;
  }
  return g_router.add_layer({
      .label = desc.label != nullptr ? desc.label : "",
      .priority = desc.priority,
      .onEvent = desc.onEvent,
      .capturesSource = desc.capturesSource,
      .userdata = desc.userdata,
      .enabled = desc.enabled,
  });
}

void unregister_layer(LayerId layer) { g_router.remove_layer(layer); }

void set_layer_enabled(LayerId layer, bool enabled) { g_router.set_layer_enabled(layer, enabled); }

void reconcile() {
  if (g_router.depth == 0) {
    g_router.ensure_initialized();
    g_router.drain();
  }
}

bool captured_above(SourceId id, int32_t priority) {
  const Source* source = g_router.find_source(id);
  return source != nullptr && source->live && g_router.captured_above(*source, priority);
}

InputSource keyboard_source() {
  g_router.ensure_initialized();
  return g_router.find_source(g_router.keyboard)->info;
}

InputSource mouse_source() {
  g_router.ensure_initialized();
  return g_router.find_source(g_router.mouse)->info;
}

InputSource touch_source() {
  g_router.ensure_initialized();
  return g_router.find_source(g_router.touch)->info;
}

std::vector<InputSource> sources() {
  g_router.ensure_initialized();
  std::vector<InputSource> result;
  for (const auto& source : g_router.sources) {
    if (source->live) {
      result.push_back(source->info);
    }
  }
  return result;
}

std::optional<InputSource> find_source(SourceId id) {
  const Source* source = g_router.find_source(id);
  if (source == nullptr || !source->live) {
    return std::nullopt;
  }
  return source->info;
}

bool source_connected(SourceId id) { return find_source(id).has_value(); }

std::string source_label(SourceId id) {
  const Source* source = g_router.find_source(id);
  return source != nullptr ? source->label : std::string{};
}

SourceId source_for_gamepad(SDL_JoystickID joystick) {
  const Source* source = g_router.find_gamepad(joystick);
  return source != nullptr ? source->info.id : kInvalidSourceId;
}

SDL_JoystickID gamepad_for_source(SourceId id) {
  const Source* source = g_router.find_source(id);
  return source != nullptr && source->live ? source->joystick : 0;
}

InputSource create_source(InputSource::Kind kind, std::string_view label) {
  g_router.ensure_initialized();
  return g_router.add_source(kind, InputSource::Origin::Synthetic, label).info;
}

void destroy_source(SourceId id) {
  if (Source* source = g_router.find_source(id);
      source != nullptr && source->info.origin == InputSource::Origin::Synthetic) {
    g_router.remove_source(*source);
  }
}

void inject(const InputSource& source, const InputEvent& event) {
  if (Source* record = g_router.find_source(source.id);
      record != nullptr && record->info.origin == InputSource::Origin::Synthetic) {
    g_router.dispatch(*record, event, nullptr);
  }
}

bool raw_key_pressed(SDL_Scancode scancode) {
  const Source* source = g_router.find_source(g_router.keyboard);
  return source != nullptr && source->raw.key(scancode);
}

bool raw_mouse_button_pressed(uint8_t button) {
  const Source* source = g_router.find_source(g_router.mouse);
  return source != nullptr && source->raw.mouse_button(button);
}

bool raw_button_pressed(SourceId id, SDL_GamepadButton button) {
  const Source* source = g_router.find_source(id);
  return source != nullptr && source->raw.button(button);
}

float raw_axis(SourceId id, SDL_GamepadAxis axis) {
  const Source* source = g_router.find_source(id);
  return source != nullptr ? source->raw.axis(axis) : 0.f;
}

LayerId watch_next_physical(PhysicalInputCallback callback, void* userdata) {
  if (callback == nullptr) {
    return kInvalidLayerId;
  }
  auto watcher = std::make_shared<Watcher>(Watcher{.callback = callback, .userdata = userdata});
  watcher->layer = g_router.add_layer({
      .label = "aurora.rebinding_watcher",
      .priority = INT32_MAX,
      .onEvent = Watcher::on_event,
      .userdata = watcher.get(),
      .owned = watcher,
  });
  return watcher->layer;
}

namespace detail {

void ensure_initialized() { g_router.ensure_initialized(); }

InputSource add_gamepad(SDL_JoystickID joystick, std::string_view label) {
  g_router.ensure_initialized();
  if (const Source* existing = g_router.find_gamepad(joystick)) {
    return existing->info;
  }
  auto& source = g_router.add_source(InputSource::Kind::Controller, InputSource::Origin::Physical, label);
  source.joystick = joystick;
  return source.info;
}

void remove_gamepad(SDL_JoystickID joystick) {
  if (Source* source = g_router.find_gamepad(joystick)) {
    g_router.remove_source(*source);
  }
}

void seed_button(SourceId id, SDL_GamepadButton button, bool pressed) {
  if (Source* source = g_router.find_source(id)) {
    source->raw.set_button(button, pressed);
  }
}

void seed_axis(SourceId id, SDL_GamepadAxis axis, float value) {
  if (Source* source = g_router.find_source(id)) {
    source->raw.set_axis(axis, value);
  }
}

void dispatch(const InputSource& source, InputEvent event, const SDL_Event* raw) {
  if (Source* record = g_router.find_source(source.id)) {
    g_router.dispatch(*record, std::move(event), raw);
  }
}

const SDL_Event* current_sdl_event() { return g_router.currentRaw; }

void focus_lost() {
  g_router.ensure_initialized();
  for (const SourceId id : {g_router.keyboard, g_router.mouse, g_router.touch}) {
    if (Source* source = g_router.find_source(id)) {
      g_router.cancel_source(*source, Cancelled::Reason::FocusLost);
    }
  }
  g_router.drain();
}

void reset() {
  // IDs keep increasing so stale IDs from before the reset stay invalid.
  g_router = {.nextLayer = g_router.nextLayer, .nextSource = g_router.nextSource};
}

} // namespace detail
} // namespace aurora::input

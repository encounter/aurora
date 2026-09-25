#include "router.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <functional>
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
using SourceChanged = InputEvent::SourceChanged;

constexpr std::array kStickAxes{
    SDL_GAMEPAD_AXIS_LEFTX,
    SDL_GAMEPAD_AXIS_LEFTY,
    SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,
};

struct Layer {
  LayerId id = kInvalidLayerId;
  std::string label;
  int32_t priority = kGameLayerPriority;
  LayerCallback onEvent = nullptr;
  LayerCaptureQuery capturesSource = nullptr;
  LayerPointerQuery pointerMode = nullptr;
  void* userdata = nullptr;
  bool enabled = true;
  bool retired = false; // Unregistered, but a callback may still be running

  [[nodiscard]] bool active() const { return enabled && !retired; }
  [[nodiscard]] bool captures(const InputSource& source) const {
    return capturesSource != nullptr && capturesSource(source, userdata);
  }
  [[nodiscard]] PointerMode pointer_preference() const {
    return pointerMode != nullptr ? pointerMode(userdata) : PointerMode::None;
  }
};

struct LayerState {
  bool blocked = false;
  bool capturing = false;
};

struct Source {
  InputSource info;
  std::string name;
  SDL_JoystickID joystick = 0;
  bool live = true;
  SourceState raw;
  std::bitset<SDL_GAMEPAD_AXIS_COUNT> engaged; // Outside neutral, whether or not it still has a route
  std::unordered_map<LayerId, LayerState> layers;

  [[nodiscard]] bool blocked(LayerId layer) const {
    const auto it = layers.find(layer);
    return it != layers.end() && it->second.blocked;
  }
};

enum class RouteKind : uint8_t {
  Key,
  Button,
  Axis,
  Pointer,
};

struct Route {
  SourceId source = kInvalidSourceId;
  RouteKind kind = RouteKind::Key;
  uint64_t code = 0;
  std::vector<LayerId> layers;
  uint32_t mouseButtons = 0;

  [[nodiscard]] bool is(SourceId otherSource, RouteKind otherKind, uint64_t otherCode) const {
    return source == otherSource && kind == otherKind && code == otherCode;
  }
};

struct Delivery {
  LayerId layer = kInvalidLayerId;
  InputEvent event;
  bool resample = false; // A stick resample; dropped if the layer is blocked again first
};

InputEvent make_event(const InputSource& source, InputEvent::Payload payload) {
  return {.source = source, .timestampNs = SDL_GetTicksNS(), .payload = std::move(payload)};
}

struct Router {
  std::vector<Layer> layers; // Descending priority
  std::vector<std::unique_ptr<Source>> sources;
  std::vector<Route> routes;
  std::vector<Delivery> deliveries;
  // Pending changes from callbacks; applied after
  std::vector<std::function<void()>> pending;
  LayerId nextLayer;
  SourceId nextSource;
  int depth = 0;
  bool draining = false;
  const SDL_Event* currentRaw = nullptr;
  SourceId keyboard = kInvalidSourceId;
  SourceId mouse = kInvalidSourceId;
  SourceId touch = kInvalidSourceId;

  explicit Router(LayerId firstLayer = 1, SourceId firstSource = 1) : nextLayer(firstLayer), nextSource(firstSource) {
    keyboard = add_source(InputSource::Kind::Keyboard, InputSource::Origin::Physical, "Keyboard").info.id;
    mouse = add_source(InputSource::Kind::Mouse, InputSource::Origin::Physical, "Mouse").info.id;
    touch = add_source(InputSource::Kind::Touch, InputSource::Origin::Physical, "Touch").info.id;
  }

  [[nodiscard]] bool deferring() const { return depth > 0 || draining; }

  [[nodiscard]] Source* find_source(SourceId id) const {
    const auto it = std::ranges::find(sources, id, [](const auto& source) { return source->info.id; });
    return it != sources.end() ? it->get() : nullptr;
  }

  [[nodiscard]] Source* find_gamepad(SDL_JoystickID joystick) const {
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

  [[nodiscard]] size_t layer_index(LayerId id) const {
    return std::ranges::find(layers, id, &Layer::id) - layers.begin();
  }

  Source& add_source(InputSource::Kind kind, InputSource::Origin origin, std::string_view name) {
    auto& source = *sources.emplace_back(std::make_unique<Source>());
    source.info = {.id = nextSource++, .kind = kind, .origin = origin};
    source.name = name;
    return source;
  }

  void mutate(std::function<void()> change) {
    if (deferring()) {
      pending.push_back(std::move(change));
    } else {
      change();
      drain();
    }
  }

  // --- Layers ---

  LayerId add_layer(Layer layer) {
    layer.id = nextLayer++;
    const LayerId id = layer.id;
    mutate([this, layer = std::move(layer)]() mutable {
      const auto it = std::ranges::find_if(layers, [&](const Layer& other) { return other.priority < layer.priority; });
      layers.insert(it, std::move(layer));
    });
    return id;
  }

  void remove_layer(LayerId id) {
    if (Layer* layer = find_layer(id)) {
      layer->retired = true;
    }
    mutate([this, id] {
      forget(id);
      std::erase_if(layers, [id](const Layer& layer) { return layer.id == id; });
    });
  }

  void set_layer_enabled(LayerId id, bool enabled) {
    mutate([this, id, enabled] {
      Layer* layer = find_layer(id);
      if (layer == nullptr || layer->retired || layer->enabled == enabled) {
        return;
      }
      if (!enabled) {
        // Treated as blocked, so re-enabling resumes sticks like the end of a capture.
        for (auto& source : sources) {
          if (source->live && !source->blocked(id)) {
            block(*source, id);
          }
        }
      }
      layer->enabled = enabled;
    });
  }

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
    return std::ranges::any_of(routes, [&](const Route& route) {
      return route.source == source.info.id && std::ranges::find(route.layers, layer) != route.layers.end();
    });
  }

  // --- Sources ---

  void connect(const Source& source) {
    broadcast(source, SourceChanged::Change::Connected);
    drain();
  }

  void remove_source(SourceId id) {
    mutate([this, id] {
      Source* source = find_source(id);
      if (source == nullptr || !source->live) {
        return;
      }
      cancel_source(*source);
      broadcast(*source, SourceChanged::Change::Disconnected);
      source->live = false;
      source->layers.clear();
    });
  }

  void broadcast(const Source& source, SourceChanged::Change change) {
    for (const auto& layer : layers) {
      if (layer.active()) {
        queue(layer.id, make_event(source.info, SourceChanged{.change = change}));
      }
    }
  }

  void cancel_source(Source& source) {
    for (const auto& layer : layers) {
      if (layer.active() && !source.blocked(layer.id)) {
        queue(layer.id, make_event(source.info, Cancelled{}));
      }
    }
    std::erase_if(routes, [&](const Route& route) { return route.source == source.info.id; });
    source.engaged.reset();
    source.raw = {};
  }

  // --- Reconciliation ---

  void drain() {
    if (deferring()) {
      return;
    }
    draining = true;
    // Bounded in case layers keep changing state from their callbacks.
    for (int iteration = 0; iteration < 32; ++iteration) {
      for (auto& change : std::exchange(pending, {})) {
        change();
      }
      for (auto& source : sources) {
        reconcile(*source);
      }
      if (deliveries.empty() && pending.empty()) {
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
      const bool captures = layer.captures(source.info);
      barrier = barrier || captures;
      const auto [it, added] = source.layers.try_emplace(layer.id, LayerState{.blocked = blocked});
      auto& state = it->second;
      if (added) {
        // New to this source
      } else if (blocked && !state.blocked) {
        block(source, layer.id);
      } else if (!blocked && state.blocked) {
        admit(source, layer.id);
      } else if (!blocked && captures && !state.capturing && holds_state(source, layer.id)) {
        cancel(source, layer.id);
      }
      state.capturing = captures;
    }
  }

  void cancel(Source& source, LayerId layer) {
    queue(layer, make_event(source.info, Cancelled{}));
    strip_routes(source.info.id, layer);
  }

  void block(Source& source, LayerId layer) {
    cancel(source, layer);
    source.layers[layer] = {.blocked = true};
  }

  void admit(Source& source, LayerId layer) {
    source.layers[layer] = {};
    for (const auto axis : kStickAxes) {
      const float value = source.raw.axis(axis);
      if (value == 0.f) {
        continue;
      }
      if (source.engaged.test(axis)) {
        join_route(source, RouteKind::Axis, axis, layer);
      }
      queue(layer, make_event(source.info, InputEvent::AxisChanged{.axis = axis, .value = value}), true);
    }
  }

  void queue(LayerId layer, InputEvent event, bool resample = false) {
    deliveries.push_back({.layer = layer, .event = std::move(event), .resample = resample});
  }

  void deliver_pending() {
    for (auto& delivery : std::exchange(deliveries, {})) {
      const Layer* layer = find_layer(delivery.layer);
      Source* source = find_source(delivery.event.source.id);
      if (layer == nullptr || layer->retired || source == nullptr) {
        continue;
      }
      if (!delivery.resample) {
        // Cancellations also reach layers that were just disabled
        call(*layer, delivery.event);
        continue;
      }
      if (!layer->enabled || !source->live || source->blocked(layer->id)) {
        continue;
      }
      call(*layer, delivery.event);
    }
  }

  // --- Routing ---

  EventResult call(const Layer& layer, const InputEvent& event) {
    ++depth;
    const EventResult result = layer.onEvent(event, layer.userdata);
    --depth;
    return result;
  }

  std::vector<LayerId> visit(const Source& source, const InputEvent& event) {
    std::vector<LayerId> received;
    for (const auto& layer : layers) {
      if (!layer.active()) {
        continue;
      }
      if (source.blocked(layer.id)) {
        break;
      }
      received.push_back(layer.id);
      if (call(layer, event) == EventResult::Consume) {
        break;
      }
    }
    return received;
  }

  void deliver(const std::vector<LayerId>& receivers, const InputEvent& event) {
    for (const auto id : receivers) {
      if (const Layer* layer = find_layer(id); layer != nullptr && layer->active()) {
        call(*layer, event);
      }
    }
  }

  Route* find_route(SourceId source, RouteKind kind, uint64_t code) {
    const auto it = std::ranges::find_if(routes, [&](const Route& route) { return route.is(source, kind, code); });
    return it != routes.end() ? &*it : nullptr;
  }

  void finish_route(Route& route, const InputEvent& event) {
    const auto receivers = std::move(route.layers);
    std::erase_if(routes, [](const Route& other) { return other.layers.empty(); });
    deliver(receivers, event);
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

  void join_route(const Source& source, RouteKind kind, uint64_t code, LayerId layer) {
    Route* route = find_route(source.info.id, kind, code);
    if (route == nullptr) {
      routes.push_back({.source = source.info.id, .kind = kind, .code = code, .layers = {layer}});
    } else if (std::ranges::find(route->layers, layer) == route->layers.end()) {
      route->layers.push_back(layer);
      std::ranges::sort(route->layers, {}, [this](LayerId id) { return layer_index(id); });
    }
  }

  void route_press(const Source& source, const InputEvent& event, RouteKind kind, uint64_t code, bool pressed,
                   bool repeat) {
    if (Route* route = find_route(source.info.id, kind, code)) {
      if (pressed) {
        deliver(route->layers, event);
      } else {
        finish_route(*route, event);
      }
    } else if (pressed && !repeat) {
      start_route(source, event, kind, code);
    }
  }

  void route_axis(Source& source, const InputEvent& event, const InputEvent::AxisChanged& axis) {
    if (!valid(axis.axis)) {
      return;
    }
    const bool engaged = std::abs(axis.value) > kAxisNeutral;
    const bool wasEngaged = source.engaged.test(axis.axis);
    source.engaged[axis.axis] = engaged;
    if (Route* route = find_route(source.info.id, RouteKind::Axis, axis.axis)) {
      if (engaged) {
        deliver(route->layers, event);
      } else {
        finish_route(*route, event);
      }
    } else if (!engaged) {
      visit(source, event);
    } else if (!wasEngaged) {
      start_route(source, event, RouteKind::Axis, axis.axis);
    }
  }

  void route_pointer(const Source& source, const InputEvent& event, const InputEvent::PointerChanged& pointer) {
    using Phase = InputEvent::PointerChanged::Phase;
    const bool mouse = source.info.kind == InputSource::Kind::Mouse;
    const uint64_t code = mouse ? 0 : pointer.pointer;
    const uint32_t button = mouse ? mouse_button_mask(pointer.button) : 0u;
    Route* route = find_route(source.info.id, RouteKind::Pointer, code);
    switch (pointer.phase) {
    case Phase::Down:
      if (route == nullptr) {
        start_route(source, event, RouteKind::Pointer, code, button);
      } else {
        route->mouseButtons |= button;
        deliver(route->layers, event);
      }
      break;
    case Phase::Move:
      if (route != nullptr) {
        deliver(route->layers, event);
      } else if (mouse) {
        visit(source, event); // Hover
      }
      break;
    case Phase::Up:
    case Phase::Cancel:
      if (route == nullptr) {
        break;
      }
      if (pointer.phase == Phase::Up) {
        route->mouseButtons &= ~button;
      }
      if (pointer.phase == Phase::Cancel || route->mouseButtons == 0) {
        finish_route(*route, event);
      } else {
        deliver(route->layers, event);
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
        [](const Cancelled&) {}, [](const SourceChanged&) {}, [&](const auto&) { visit(source, event); });
  }

  void dispatch(InputEvent event, const SDL_Event* raw) {
    Source* source = find_source(event.source.id);
    if (deferring() || source == nullptr || !source->live || event.payload.is<Cancelled>() ||
        event.payload.is<SourceChanged>()) {
      return;
    }
    event.source = source->info;
    if (event.timestampNs == 0) {
      event.timestampNs = SDL_GetTicksNS();
    }
    // Pick up changes made since the last event before routing.
    drain();
    source->raw.apply(event);
    currentRaw = raw;
    route_event(*source, event);
    currentRaw = nullptr;
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

  [[nodiscard]] PointerMode pointer_mode() const {
    const Source* pointer = find_source(mouse);
    for (const auto& layer : layers) {
      if (!layer.active()) {
        continue;
      }
      if (pointer->blocked(layer.id)) {
        break;
      }
      if (const auto mode = layer.pointer_preference(); mode != PointerMode::None) {
        return mode;
      }
    }
    return PointerMode::Visible;
  }
};

Router g_router;

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
      .pointerMode = desc.pointerMode,
      .userdata = desc.userdata,
      .enabled = desc.enabled,
  });
}

void unregister_layer(LayerId layer) { g_router.remove_layer(layer); }

void set_layer_enabled(LayerId layer, bool enabled) { g_router.set_layer_enabled(layer, enabled); }

void reconcile() {
  if (g_router.depth == 0) {
    g_router.drain();
  }
}

bool captured_above(SourceId id, int32_t priority) {
  const Source* source = g_router.find_source(id);
  return source != nullptr && source->live && g_router.captured_above(*source, priority);
}

PointerMode pointer_mode() { return g_router.pointer_mode(); }

InputSource keyboard_source() { return g_router.find_source(g_router.keyboard)->info; }

InputSource mouse_source() { return g_router.find_source(g_router.mouse)->info; }

InputSource touch_source() { return g_router.find_source(g_router.touch)->info; }

std::vector<InputSource> sources() {
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

std::string source_name(SourceId id) {
  const Source* source = g_router.find_source(id);
  return source != nullptr ? source->name : std::string{};
}

SourceId source_for_gamepad(SDL_JoystickID joystick) {
  const Source* source = g_router.find_gamepad(joystick);
  return source != nullptr ? source->info.id : kInvalidSourceId;
}

SDL_JoystickID gamepad_for_source(SourceId id) {
  const Source* source = g_router.find_source(id);
  return source != nullptr && source->live ? source->joystick : 0;
}

InputSource create_source(InputSource::Kind kind, std::string_view name) {
  const auto& source = g_router.add_source(kind, InputSource::Origin::Synthetic, name);
  g_router.connect(source);
  return source.info;
}

void destroy_source(SourceId id) {
  if (const Source* source = g_router.find_source(id);
      source != nullptr && source->info.origin == InputSource::Origin::Synthetic) {
    g_router.remove_source(id);
  }
}

void inject(InputEvent event) {
  if (const Source* source = g_router.find_source(event.source.id);
      source != nullptr && source->info.origin == InputSource::Origin::Synthetic) {
    g_router.dispatch(std::move(event), nullptr);
  }
}

bool raw_button_pressed(SourceId id, SDL_GamepadButton button) {
  const Source* source = g_router.find_source(id);
  return source != nullptr && source->raw.button(button);
}

float raw_axis(SourceId id, SDL_GamepadAxis axis) {
  const Source* source = g_router.find_source(id);
  return source != nullptr ? source->raw.axis(axis) : 0.f;
}

namespace detail {

InputSource add_gamepad(SDL_JoystickID joystick, std::string_view name, const SourceState& initial) {
  if (const Source* existing = g_router.find_gamepad(joystick)) {
    return existing->info;
  }
  auto& source = g_router.add_source(InputSource::Kind::Controller, InputSource::Origin::Physical, name);
  source.joystick = joystick;
  source.raw = initial;
  g_router.connect(source);
  return source.info;
}

void remove_gamepad(SDL_JoystickID joystick) {
  if (const Source* source = g_router.find_gamepad(joystick)) {
    g_router.remove_source(source->info.id);
  }
}

void remap_gamepad(SDL_JoystickID joystick) {
  if (const Source* source = g_router.find_gamepad(joystick)) {
    g_router.broadcast(*source, SourceChanged::Change::Remapped);
    g_router.drain();
  }
}

void dispatch(InputEvent event, const SDL_Event* raw) { g_router.dispatch(std::move(event), raw); }

const SDL_Event* current_sdl_event() { return g_router.currentRaw; }

void focus_lost() {
  g_router.mutate([] {
    for (const SourceId id : {g_router.keyboard, g_router.mouse, g_router.touch}) {
      g_router.cancel_source(*g_router.find_source(id));
    }
  });
}

void reset() { g_router = Router{g_router.nextLayer, g_router.nextSource}; }

} // namespace detail
} // namespace aurora::input

#include "../gamepad.hpp"
#include "port.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aurora::pad {
namespace {

using binding::Binding;
using binding::BindingSet;
using binding::ControlId;
using binding::kInvalidControlId;
using binding::PhysicalInput;
using input::kInvalidSourceId;
using input::SourceId;

constexpr std::array<std::pair<SDL_GamepadButton, PADExtButton>, PAD_EXT_BUTTON_COUNT> kExtButtons{{
    {SDL_GAMEPAD_BUTTON_BACK, PAD_BUTTON_BACK},
    {SDL_GAMEPAD_BUTTON_GUIDE, PAD_BUTTON_GUIDE},
    {SDL_GAMEPAD_BUTTON_MISC1, PAD_BUTTON_MISC1},
    {SDL_GAMEPAD_BUTTON_MISC2, PAD_BUTTON_MISC2},
    {SDL_GAMEPAD_BUTTON_MISC3, PAD_BUTTON_MISC3},
    {SDL_GAMEPAD_BUTTON_MISC4, PAD_BUTTON_MISC4},
    {SDL_GAMEPAD_BUTTON_MISC5, PAD_BUTTON_MISC5},
    {SDL_GAMEPAD_BUTTON_MISC6, PAD_BUTTON_MISC6},
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, PAD_BUTTON_RIGHT_PADDLE1},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, PAD_BUTTON_LEFT_PADDLE1},
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, PAD_BUTTON_RIGHT_PADDLE2},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, PAD_BUTTON_LEFT_PADDLE2},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_BUTTON_RIGHT_STICK},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, PAD_BUTTON_LEFT_STICK},
    {SDL_GAMEPAD_BUTTON_TOUCHPAD, PAD_BUTTON_TOUCHPAD},
}};

constexpr std::array<const char*, PAD_EXT_BUTTON_COUNT> kExtButtonNames{
    "aurora.pad.ext.back",         "aurora.pad.ext.guide",         "aurora.pad.ext.misc1",
    "aurora.pad.ext.misc2",        "aurora.pad.ext.misc3",         "aurora.pad.ext.misc4",
    "aurora.pad.ext.misc5",        "aurora.pad.ext.misc6",         "aurora.pad.ext.right_paddle1",
    "aurora.pad.ext.left_paddle1", "aurora.pad.ext.right_paddle2", "aurora.pad.ext.left_paddle2",
    "aurora.pad.ext.right_stick",  "aurora.pad.ext.left_stick",    "aurora.pad.ext.touchpad",
};

constexpr std::array<std::pair<PADButton, ControlId Controls::*>, PAD_BUTTON_COUNT> kButtons{{
    {PAD_BUTTON_A, &Controls::a},
    {PAD_BUTTON_B, &Controls::b},
    {PAD_BUTTON_X, &Controls::x},
    {PAD_BUTTON_Y, &Controls::y},
    {PAD_TRIGGER_Z, &Controls::z},
    {PAD_BUTTON_START, &Controls::start},
    {PAD_TRIGGER_L, &Controls::l},
    {PAD_TRIGGER_R, &Controls::r},
    {PAD_BUTTON_UP, &Controls::up},
    {PAD_BUTTON_DOWN, &Controls::down},
    {PAD_BUTTON_LEFT, &Controls::left},
    {PAD_BUTTON_RIGHT, &Controls::right},
}};

s8 to_stick(float value) { return static_cast<s8>(std::clamp(std::lround(value * 127.f), -127l, 127l)); }

u8 to_trigger(float value) { return static_cast<u8>(std::clamp(std::lround(value * 255.f), 0l, 255l)); }

bool g_blocked = false;

struct PortState {
  uint64_t generation = 0;
  SourceId controller = 0;
  std::shared_ptr<const BindingSet> set;
  binding::State gameplay;
  std::vector<Binding> actions;
  binding::ProducerId virtualProducer = binding::kInvalidProducerId;
  std::optional<PADStatus> virtualStatus;
  bool cancelled = false;

  void sync(u32 port) {
    const auto next = detail::profile_generation();
    controller = port_controller(port);
    if (generation == next && set) {
      return;
    }

    auto resolved = std::make_shared<BindingSet>(*detail::resolve_port(port));
    for (auto b : actions) {
      auto resolve = [&](PhysicalInput& physical) {
        physical.source = physical.control.is<PhysicalInput::Key>()           ? input::keyboard_source().id
                          : physical.control.is<PhysicalInput::MouseButton>() ? input::mouse_source().id
                                                                              : controller;
        return physical.source != 0;
      };
      bool valid = resolve(b.input);
      for (auto& held : b.held) {
        valid &= resolve(held.input);
      }

      if (valid) {
        resolved->bindings.push_back(std::move(b));
      }
    }

    set = resolved;
    generation = detail::profile_generation();
    note(gameplay.set_bindings(set));
  }

  void process(const input::InputEvent& event) {
    if (event.payload.is<input::InputEvent::Cancelled>() && captured()) {
      virtualStatus.reset();
      note(gameplay.cancel_producers());
      cancelled = true;
    }

    if (set->references(event.source.id)) {
      note(gameplay.process(event).changes);
    }
  }

  void note(const std::vector<binding::ControlChange>& changes) {
    cancelled = cancelled || std::ranges::any_of(changes, [](const auto& change) {
                  return change.reason == binding::ControlChange::Reason::Cancelled;
                });
  }

  [[nodiscard]] bool captured() const {
    return g_blocked || input::captured_above(controller, input::kGameLayerPriority) ||
           input::captured_above(input::touch_source().id, input::kGameLayerPriority) ||
           input::captured_above(input::device_source().id, input::kGameLayerPriority) ||
           std::ranges::any_of(input::sources(), [&](const input::InputSource& source) {
             return set->references(source.id) && input::captured_above(source.id, input::kGameLayerPriority);
           });
  }

  void apply_virtual_status() {
    if (captured()) {
      virtualStatus.reset();
      if (gameplay.has_active_producers()) {
        note(gameplay.cancel_producers());
        cancelled = true;
      }

      return;
    }

    if (virtualProducer == binding::kInvalidProducerId) {
      virtualProducer = gameplay.add_producer();
    }

    if (!virtualStatus) {
      gameplay.clear_producer(virtualProducer);
      return;
    }

    const auto& c = controls();
    const auto& status = *virtualStatus;
    for (const auto& [bit, member] : kButtons) {
      gameplay.set_value(virtualProducer, c.*member, (status.button & bit) != 0 ? 1.f : 0.f);
    }

    for (size_t i = 0; i < c.ext.size(); ++i) {
      gameplay.set_value(virtualProducer, c.ext[i], (status.extButton & kExtButtons[i].second) != 0 ? 1.f : 0.f);
    }

    gameplay.set_value(virtualProducer, c.leftX, static_cast<float>(status.stickX) / 127.f);
    gameplay.set_value(virtualProducer, c.leftY, static_cast<float>(status.stickY) / 127.f);
    gameplay.set_value(virtualProducer, c.rightX, static_cast<float>(status.substickX) / 127.f);
    gameplay.set_value(virtualProducer, c.rightY, static_cast<float>(status.substickY) / 127.f);
    gameplay.set_value(virtualProducer, c.triggerL, static_cast<float>(status.triggerLeft) / 255.f);
    gameplay.set_value(virtualProducer, c.triggerR, static_cast<float>(status.triggerRight) / 255.f);
  }

  void read(PADStatus& status) const {
    const auto& c = controls();
    for (const auto& [bit, member] : kButtons) {
      if (gameplay.value(c.*member) >= 0.5f) {
        status.button |= bit;
      }
    }

    for (size_t i = 0; i < c.ext.size(); ++i) {
      if (gameplay.value(c.ext[i]) >= 0.5f) {
        status.extButton |= kExtButtons[i].second;
      }
    }

    status.stickX = to_stick(gameplay.value(c.leftX));
    status.stickY = to_stick(gameplay.value(c.leftY));
    status.substickX = to_stick(gameplay.value(c.rightX));
    status.substickY = to_stick(gameplay.value(c.rightY));
    status.triggerLeft = to_trigger(gameplay.value(c.triggerL));
    status.triggerRight = to_trigger(gameplay.value(c.triggerR));

    // A digital L/R press reads as a full analog press.
    if ((status.button & PAD_TRIGGER_L) != 0) {
      status.triggerLeft = 180;
    }

    if ((status.button & PAD_TRIGGER_R) != 0) {
      status.triggerRight = 180;
    }
  }
};

std::array<PortState, PAD_MAX_CONTROLLERS> g_ports;
input::LayerId g_layer = input::kInvalidLayerId;

PortState& synced_port(u32 port) {
  auto& state = g_ports[port];
  state.sync(port);
  return state;
}

input::EventResult on_event(const input::InputEvent& event, void*) {
  for (u32 port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
    synced_port(port).process(event);
  }

  return input::EventResult::Pass;
}

} // namespace

const Controls& controls() {
  static const Controls sControls = [] {
    const auto button = [](const char* name) {
      return binding::register_control({.name = name, .kind = binding::ControlKind::Button});
    };

    const auto axis = [](const char* name) {
      return binding::register_control({.name = name, .kind = binding::ControlKind::Axis});
    };
    Controls c;
    c.a = button("aurora.pad.a");
    c.b = button("aurora.pad.b");
    c.x = button("aurora.pad.x");
    c.y = button("aurora.pad.y");
    c.z = button("aurora.pad.z");
    c.start = button("aurora.pad.start");
    c.l = button("aurora.pad.l");
    c.r = button("aurora.pad.r");
    c.up = button("aurora.pad.up");
    c.down = button("aurora.pad.down");
    c.left = button("aurora.pad.left");
    c.right = button("aurora.pad.right");
    c.leftX = axis("aurora.pad.left_x");
    c.leftY = axis("aurora.pad.left_y");
    c.rightX = axis("aurora.pad.right_x");
    c.rightY = axis("aurora.pad.right_y");
    c.triggerL = axis("aurora.pad.trigger_l");
    c.triggerR = axis("aurora.pad.trigger_r");
    for (size_t i = 0; i < c.ext.size(); ++i) {
      c.ext[i] = button(kExtButtonNames[i]);
    }

    return c;
  }();
  return sControls;
}

ControlId control_for_button(PADButton button) {
  const auto& c = controls();
  const auto it = std::ranges::find(kButtons, button, &std::pair<PADButton, ControlId Controls::*>::first);
  return it != kButtons.end() ? c.*(it->second) : kInvalidControlId;
}

ControlId control_for_ext_button(PADExtButton button) {
  const auto it = std::ranges::find(kExtButtons, button, &std::pair<SDL_GamepadButton, PADExtButton>::second);
  return it != kExtButtons.end() ? controls().ext[it - kExtButtons.begin()] : kInvalidControlId;
}

std::shared_ptr<const BindingSet> binding_set(uint32_t port) {
  return port < PAD_MAX_CONTROLLERS ? synced_port(port).set : nullptr;
}

std::shared_ptr<const BindingSet> controller_binding_set(SourceId controller) {
  const auto it = gamepad::g_GameControllers.find(input::gamepad_for_source(controller));
  if (it == gamepad::g_GameControllers.end()) {
    return nullptr;
  }

  return detail::resolve_controller(controller);
}

void set_action_bindings(uint32_t port, std::vector<Binding> bindings) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }

  g_ports[port].actions = std::move(bindings);
  g_ports[port].generation = 0;
}

bool connected(uint32_t port) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return false;
  }

  auto& state = synced_port(port);
  return state.controller != 0 || !state.set->bindings.empty() ||
         (!state.captured() && (state.virtualStatus || state.gameplay.has_active_producers()));
}

binding::State& gameplay_state(uint32_t port) {
  auto& state = synced_port(port);
  state.apply_virtual_status();
  return state.gameplay;
}

namespace detail {

void initialize() {
  if (g_layer == input::kInvalidLayerId) {
    (void)input::register_layer({
        .label = "aurora.input.block",
        .priority = input::kGameLayerPriority + 1,
        .onEvent = [](const input::InputEvent&, void*) { return input::EventResult::Pass; },
        .capturesSource = [](const input::InputSource&, void*) { return g_blocked; },
    });
    g_layer = input::register_layer({
        .label = "aurora.pad",
        .priority = input::kGameLayerPriority,
        .onEvent = on_event,
    });
  }
}

bool read(uint32_t port, PADStatus& status) {
  auto& state = synced_port(port);
  state.apply_virtual_status();
  if (state.controller == kInvalidSourceId && state.set->bindings.empty() && !state.virtualStatus &&
      !state.gameplay.has_active_producers()) {
    return false;
  }

  state.read(status);
  return true;
}

void set_virtual_status(uint32_t port, const PADStatus* status) {
  if (status != nullptr) {
    g_ports[port].virtualStatus = *status;
  } else {
    g_ports[port].virtualStatus.reset();
  }
}

void set_blocked(bool blocked) {
  g_blocked = blocked;
  if (blocked) {
    for (auto& port : g_ports) {
      port.virtualStatus.reset();
      port.note(port.gameplay.reset());
      port.cancelled = true;
    }
  }

  input::reconcile();
}

bool blocked() { return g_blocked; }

bool consume_cancellation(uint32_t port) { return std::exchange(synced_port(port).cancelled, false); }

bool captured(uint32_t port) { return synced_port(port).captured(); }

SourceId controller_source(uint32_t port) { return synced_port(port).controller; }

} // namespace detail
} // namespace aurora::pad

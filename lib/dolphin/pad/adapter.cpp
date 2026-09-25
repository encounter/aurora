#include "adapter.hpp"

#include <aurora/binding.hpp>
#include <aurora/input.hpp>
#include <aurora/pad.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

static bool operator==(const PADSignedNativeAxis& lhs, const PADSignedNativeAxis& rhs) {
  return lhs.nativeAxis == rhs.nativeAxis && lhs.sign == rhs.sign;
}
static bool operator==(const PADButtonMapping& lhs, const PADButtonMapping& rhs) {
  return lhs.nativeButton == rhs.nativeButton && lhs.padButton == rhs.padButton;
}
static bool operator==(const PADAxisMapping& lhs, const PADAxisMapping& rhs) {
  return lhs.nativeAxis == rhs.nativeAxis && lhs.nativeButton == rhs.nativeButton && lhs.padAxis == rhs.padAxis;
}
static bool operator==(const PADDeadZones& lhs, const PADDeadZones& rhs) {
  return lhs.emulateTriggers == rhs.emulateTriggers && lhs.useDeadzones == rhs.useDeadzones &&
         lhs.stickDeadZone == rhs.stickDeadZone && lhs.substickDeadZone == rhs.substickDeadZone &&
         lhs.leftTriggerActivationZone == rhs.leftTriggerActivationZone &&
         lhs.rightTriggerActivationZone == rhs.rightTriggerActivationZone;
}
static bool operator==(const PADKeyButtonBinding& lhs, const PADKeyButtonBinding& rhs) {
  return lhs.scancode == rhs.scancode && lhs.padButton == rhs.padButton;
}
static bool operator==(const PADKeyAxisBinding& lhs, const PADKeyAxisBinding& rhs) {
  return lhs.scancode == rhs.scancode && lhs.padAxis == rhs.padAxis && lhs.influence == rhs.influence;
}

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

struct AxisTarget {
  ControlId control = kInvalidControlId;
  float scale = 0.f;
  float deadZone = 0.f;
};

AxisTarget axis_target(u32 padAxis, float stickDeadZone, float substickDeadZone) {
  const auto& c = controls();
  switch (padAxis) {
  case PAD_AXIS_LEFT_X_POS:
    return {c.leftX, 1.f, stickDeadZone};
  case PAD_AXIS_LEFT_X_NEG:
    return {c.leftX, -1.f, stickDeadZone};
  case PAD_AXIS_LEFT_Y_POS:
    return {c.leftY, 1.f, stickDeadZone};
  case PAD_AXIS_LEFT_Y_NEG:
    return {c.leftY, -1.f, stickDeadZone};
  case PAD_AXIS_RIGHT_X_POS:
    return {c.rightX, 1.f, substickDeadZone};
  case PAD_AXIS_RIGHT_X_NEG:
    return {c.rightX, -1.f, substickDeadZone};
  case PAD_AXIS_RIGHT_Y_POS:
    return {c.rightY, 1.f, substickDeadZone};
  case PAD_AXIS_RIGHT_Y_NEG:
    return {c.rightY, -1.f, substickDeadZone};
  case PAD_AXIS_TRIGGER_L:
    return {c.triggerL, 1.f, 0.f};
  case PAD_AXIS_TRIGGER_R:
    return {c.triggerR, 1.f, 0.f};
  default:
    return {};
  }
}

std::optional<PhysicalInput> controller_input(const PADAxisMapping& mapping, SourceId source) {
  using Direction = PhysicalInput::GamepadAxis::Direction;
  if (mapping.nativeAxis.nativeAxis >= 0 && mapping.nativeAxis.nativeAxis < SDL_GAMEPAD_AXIS_COUNT) {
    return PhysicalInput{
        .source = source,
        .control = PhysicalInput::GamepadAxis{
            .axis = static_cast<SDL_GamepadAxis>(mapping.nativeAxis.nativeAxis),
            .direction = mapping.nativeAxis.sign == AXIS_SIGN_NEGATIVE ? Direction::Negative : Direction::Positive,
        }};
  }
  if (mapping.nativeButton >= 0 && mapping.nativeButton < SDL_GAMEPAD_BUTTON_COUNT) {
    return PhysicalInput{
        .source = source,
        .control = PhysicalInput::GamepadButton{.button = static_cast<SDL_GamepadButton>(mapping.nativeButton)}};
  }
  return std::nullopt;
}

// Keyboard bindings store mouse buttons as negative scancodes (-2 is SDL_BUTTON_LEFT).
std::optional<PhysicalInput> keyboard_input(s32 scancode) {
  if (scancode > PAD_KEY_INVALID && scancode < SDL_SCANCODE_COUNT) {
    return PhysicalInput{
        .source = input::keyboard_source().id,
        .control = PhysicalInput::Key{.scancode = static_cast<SDL_Scancode>(scancode)},
    };
  }
  if (const int32_t button = -(scancode + 1); scancode < PAD_KEY_INVALID && button >= 1 && button <= 5) {
    return PhysicalInput{
        .source = input::mouse_source().id,
        .control = PhysicalInput::MouseButton{.button = static_cast<uint8_t>(button)},
    };
  }
  return std::nullopt;
}

struct PortInputs {
  SourceId controller = kInvalidSourceId;
  bool keyboardActive = false;
  std::array<PADButtonMapping, PAD_BUTTON_COUNT> buttons{};
  std::array<PADAxisMapping, PAD_AXIS_COUNT> axes{};
  PADDeadZones deadZones{};
  std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> keys{};
  std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> keyAxes{};
  uint64_t actionGeneration = 0;

  bool operator==(const PortInputs&) const = default;

  static PortInputs for_port(u32 port, uint64_t actionGeneration) {
    PortInputs inputs{.actionGeneration = actionGeneration};
    if (auto* controller = gamepad::get_controller_for_player(port)) {
      detail::ensure_mapping_loaded(controller);
      inputs.controller = input::source_for_gamepad(gamepad::get_instance_for_player(port));
      inputs.buttons = controller->m_buttonMapping;
      inputs.axes = controller->m_axisMapping;
      inputs.deadZones = controller->m_deadZones;
    }
    const auto& keyboard = detail::keyboard_state(port);
    inputs.keyboardActive = keyboard.m_mappingsSet;
    if (inputs.keyboardActive) {
      inputs.keys = keyboard.m_buttonMapping;
      inputs.keyAxes = keyboard.m_axisMapping;
    }
    return inputs;
  }

  static PortInputs for_controller(const gamepad::GameController& controller, SourceId source) {
    PortInputs inputs{.controller = source, .deadZones = controller.m_deadZones};
    if (controller.m_mappingLoaded) {
      inputs.buttons = controller.m_buttonMapping;
      inputs.axes = controller.m_axisMapping;
    } else {
      inputs.buttons = detail::default_buttons(controller);
      inputs.axes = detail::default_axes();
    }
    return inputs;
  }

  [[nodiscard]] SourceId source_for(const PhysicalInput& input) const {
    if (input.control.is<PhysicalInput::Key>()) {
      return keyboardActive ? input::keyboard_source().id : kInvalidSourceId;
    }
    if (input.control.is<PhysicalInput::MouseButton>()) {
      return keyboardActive ? input::mouse_source().id : kInvalidSourceId;
    }
    return controller;
  }

  [[nodiscard]] std::shared_ptr<const BindingSet> resolve(const std::vector<Binding>& actions) const {
    auto set = std::make_shared<BindingSet>();
    auto& out = set->bindings;
    if (controller != kInvalidSourceId) {
      add_controller_bindings(out);
    }
    if (keyboardActive) {
      add_keyboard_bindings(out);
    }
    for (auto binding : actions) {
      binding.input.source = source_for(binding.input);
      bool resolved = binding.input.source != kInvalidSourceId;
      for (auto& held : binding.held) {
        held.input.source = source_for(held.input);
        resolved = resolved && held.input.source != kInvalidSourceId;
      }
      if (resolved) {
        out.push_back(std::move(binding));
      }
    }
    return set;
  }

private:
  void add_controller_bindings(std::vector<Binding>& out) const {
    const auto& c = controls();
    bool leftTriggerSet = false;
    bool rightTriggerSet = false;
    for (const auto& mapping : buttons) {
      const ControlId target = control_for_button(mapping.padButton);
      if (target == kInvalidControlId || mapping.nativeButton >= SDL_GAMEPAD_BUTTON_COUNT) {
        continue;
      }
      out.push_back({
          .input =
              {
                  .source = controller,
                  .control = PhysicalInput::GamepadButton{static_cast<SDL_GamepadButton>(mapping.nativeButton)},
              },
          .target = target,
      });
      leftTriggerSet = leftTriggerSet || mapping.padButton == PAD_TRIGGER_L;
      rightTriggerSet = rightTriggerSet || mapping.padButton == PAD_TRIGGER_R;
    }

    const float stickDeadZone = deadZones.useDeadzones ? deadZones.stickDeadZone / 32767.f : 0.f;
    const float substickDeadZone = deadZones.useDeadzones ? deadZones.substickDeadZone / 32767.f : 0.f;
    for (const auto& mapping : axes) {
      const auto target = axis_target(mapping.padAxis, stickDeadZone, substickDeadZone);
      const auto input = controller_input(mapping, controller);
      if (target.control == kInvalidControlId || !input) {
        continue;
      }
      out.push_back({
          .input = *input,
          .target = target.control,
          .deadZone = input->control.is<PhysicalInput::GamepadAxis>() ? target.deadZone : 0.f,
          .scale = target.scale,
      });
    }

    if (deadZones.emulateTriggers) {
      const auto emulate = [&](PADAxis padAxis, ControlId target, u16 zone) {
        const auto mapping = std::ranges::find(axes, padAxis, &PADAxisMapping::padAxis);
        if (const auto input = mapping != axes.end() ? controller_input(*mapping, controller) : std::nullopt) {
          out.push_back({
              .input = *input,
              .target = target,
              .threshold = std::clamp(static_cast<float>(zone + 1) / 32767.f, 1.f / 32767.f, 1.f),
          });
        }
      };
      if (!leftTriggerSet) {
        emulate(PAD_AXIS_TRIGGER_L, c.l, deadZones.leftTriggerActivationZone);
      }
      if (!rightTriggerSet) {
        emulate(PAD_AXIS_TRIGGER_R, c.r, deadZones.rightTriggerActivationZone);
      }
    }

    for (size_t i = 0; i < kExtButtons.size(); ++i) {
      out.push_back({
          .input =
              {
                  .source = controller,
                  .control = PhysicalInput::GamepadButton{.button = kExtButtons[i].first},
              },
          .target = c.ext[i],
      });
    }
  }

  void add_keyboard_bindings(std::vector<Binding>& out) const {
    for (const auto& binding : keys) {
      const ControlId target = control_for_button(binding.padButton);
      if (const auto input = keyboard_input(binding.scancode); target != kInvalidControlId && input) {
        out.push_back({.input = *input, .target = target});
      }
    }
    for (const auto& binding : keyAxes) {
      const auto target = axis_target(binding.padAxis, 0.f, 0.f);
      if (const auto input = keyboard_input(binding.scancode); target.control != kInvalidControlId && input) {
        out.push_back({.input = *input, .target = target.control, .scale = target.scale});
      }
    }
  }
};

s8 to_stick(float value) { return static_cast<s8>(std::clamp(std::lround(value * 127.f), -127l, 127l)); }

u8 to_trigger(float value) { return static_cast<u8>(std::clamp(std::lround(value * 255.f), 0l, 255l)); }

bool g_blocked = false;

struct PortState {
  PortInputs inputs;
  bool built = false;
  std::shared_ptr<const BindingSet> set;
  binding::State gameplay;
  std::vector<Binding> actions;
  uint64_t actionGeneration = 0;
  binding::ProducerId virtualProducer = binding::kInvalidProducerId;
  std::optional<PADStatus> virtualStatus;
  bool cancelled = false;

  void sync(u32 port) {
    auto next = PortInputs::for_port(port, actionGeneration);
    if (built && next == inputs) {
      return;
    }
    inputs = next;
    built = true;
    set = inputs.resolve(actions);
    note(gameplay.set_bindings(set));
  }

  void process(const input::InputEvent& event) {
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
    return g_blocked || std::ranges::any_of(input::sources(), [&](const input::InputSource& source) {
             return set->references(source.id) && input::captured_above(source.id, input::kGameLayerPriority);
           });
  }

  void apply_virtual_status() {
    if (virtualProducer == binding::kInvalidProducerId) {
      virtualProducer = gameplay.add_producer();
    }
    if (!virtualStatus) {
      gameplay.clear_producer(virtualProducer);
      return;
    }
    if (g_blocked) {
      if (!gameplay.clear_producer(virtualProducer).empty()) {
        cancelled = true;
      }
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
  return PortInputs::for_controller(it->second, controller).resolve({});
}

void set_action_bindings(uint32_t port, std::vector<Binding> bindings) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_ports[port].actions = std::move(bindings);
  ++g_ports[port].actionGeneration;
}

namespace detail {

void initialize() {
  if (g_layer == input::kInvalidLayerId) {
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
  if (state.inputs.controller == kInvalidSourceId && !state.inputs.keyboardActive && !state.virtualStatus) {
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
  input::set_layer_enabled(g_layer, !blocked);
}

bool consume_cancellation(uint32_t port) { return std::exchange(synced_port(port).cancelled, false); }

bool captured(uint32_t port) { return synced_port(port).captured(); }

SourceId controller_source(uint32_t port) { return synced_port(port).inputs.controller; }

} // namespace detail
} // namespace aurora::pad

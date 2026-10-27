#include "backend.hpp"

#include "../../input/lifecycle.hpp"
#include "../../input/port.hpp"
#include "../../window.hpp"
#include <aurora/pad.hpp>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <numbers>
#include <optional>

namespace aurora::wpad {
namespace {

using binding::Binding;
using binding::PhysicalInput;
using input::InputEvent;
using input::SensorKind;

struct Reading {
  int64_t hardware = 0;
  uint64_t event = 0;
  std::array<float, 3> values{};
};

struct SensorHistory {
  std::deque<Reading> readings;
  std::deque<std::pair<uint64_t, int64_t>> offsets;

  void push(const InputEvent& event, const InputEvent::SensorChanged& sample) {
    if (std::ranges::any_of(sample.values, [](float f) { return !std::isfinite(f); })) {
      return;
    }

    if (sample.sensorTimestampNs) {
      const int64_t offset = static_cast<int64_t>(event.timestampNs) - static_cast<int64_t>(sample.sensorTimestampNs);
      offsets.emplace_back(event.timestampNs, offset);
      while (!offsets.empty() && offsets.front().first + 1'000'000'000 < event.timestampNs) {
        offsets.pop_front();
      }
    }

    readings.push_back({static_cast<int64_t>(sample.sensorTimestampNs), event.timestampNs, sample.values});
    while (readings.size() > 256) {
      readings.pop_front();
    }
  }

  std::optional<std::array<float, 3>> at(uint64_t time) {
    if (readings.empty()) {
      return std::nullopt;
    }

    int64_t offset = 0;
    if (!offsets.empty()) {
      offset = std::ranges::min(offsets, {}, &std::pair<uint64_t, int64_t>::second).second;
    }

    auto timestamp = [&](const Reading& r) { return r.hardware ? r.hardware + offset : static_cast<int64_t>(r.event); };
    const auto target = static_cast<int64_t>(time);
    while (readings.size() > 2 && timestamp(readings[1]) <= target) {
      readings.pop_front();
    }

    const auto& first = readings.front();
    if (target <= timestamp(first)) {
      return first.values;
    }

    for (size_t i = 1; i < readings.size(); ++i) {
      const auto& b = readings[i];
      const auto& a = readings[i - 1];
      if (target <= timestamp(b)) {
        const auto duration = timestamp(b) - timestamp(a);
        const float fraction = duration > 0 ? static_cast<float>(target - timestamp(a)) / duration : 1.f;
        auto values = a.values;
        for (size_t axis = 0; axis < 3; ++axis) {
          values[axis] += (b.values[axis] - values[axis]) * fraction;
        }

        return values;
      }
    }

    return readings.back().values;
  }
};

class SimulatedChannelProvider final : public ChannelProvider {
public:
  ChannelSample sample(uint32_t port, uint64_t timestampNs) override;
  input::EventResult event(const InputEvent& event);

private:
  std::map<std::pair<input::SourceId, SensorKind>, SensorHistory> sensors;
  std::array<AuroraWpadPointer, 4> pointers{};
  std::array<input::SourceId, 4> pointerSources{};
  std::array<input::SourceId, 4> controllerSources{};
  Vec acceleration(input::SourceId source, SensorKind sensor, uint64_t time);
};

std::shared_ptr<SimulatedChannelProvider> simulated;
std::array<std::shared_ptr<ChannelProvider>, 4> providers;
input::LayerId layer = input::kInvalidLayerId;
float distance = 1.f;
uint64_t (*clockFn)() = SDL_GetTicksNS;

s16 raw(float g, float unit, float limit) {
  return static_cast<s16>(std::lround(std::clamp(std::isfinite(g) ? g * unit : 0.f, -limit, limit)));
}

AuroraWpadPointer map_pointer(SDL_FPoint position, const AuroraWindowSize& size) {
  if (!size.width || !size.height || !size.fb_width || !size.fb_height || !size.native_fb_width ||
      !size.native_fb_height) {
    return {};
  }
  // Match the integer viewport used by the EFB/XFB presentation pass.
  uint32_t width = size.native_fb_width;
  uint32_t height =
      std::min(size.native_fb_height,
               std::max(1u, static_cast<uint32_t>(std::lround(double(width) * size.fb_height / size.fb_width))));
  if (height == size.native_fb_height) {
    width = std::min(size.native_fb_width,
                     std::max(1u, static_cast<uint32_t>(std::lround(double(height) * size.fb_width / size.fb_height))));
  }

  const float left = static_cast<float>(size.native_fb_width - width) / 2.f;
  const float top = static_cast<float>(size.native_fb_height - height) / 2.f;
  return {
      std::clamp(2.f * (position.x * size.native_fb_width / size.width - left) / width - 1.f, -1.f, 1.f),
      std::clamp(2.f * (position.y * size.native_fb_height / size.height - top) / height - 1.f, -1.f, 1.f),
      TRUE,
  };
}

input::EventResult SimulatedChannelProvider::event(const InputEvent& event) {
  if (event.payload.is<InputEvent::Cancelled>()) {
    std::erase_if(sensors, [&](const auto& pair) { return pair.first.first == event.source.id; });
    for (size_t port = 0; port < 4; ++port) {
      if (pointerSources[port] == event.source.id) {
        pointers[port].valid = FALSE;
      }
    }
  } else if (const auto* sensor = event.payload.get_if<InputEvent::SensorChanged>()) {
    sensors[{event.source.id, sensor->sensor}].push(event, *sensor);
  } else if (const auto* pointer = event.payload.get_if<InputEvent::PointerChanged>()) {
    using Phase = InputEvent::PointerChanged::Phase;
    if (pointer->phase == Phase::Down || pointer->phase == Phase::Move) {
      pointers[0] = map_pointer(pointer->position, window::get_window_size());
      pointerSources[0] = event.source.id;
    } else if (pointer->phase == Phase::Cancel) {
      pointers[0].valid = FALSE;
    }
  }

  return input::EventResult::Pass;
}

Vec SimulatedChannelProvider::acceleration(input::SourceId source, SensorKind kind, uint64_t time) {
  if (input::captured_above(source, input::kGameLayerPriority)) {
    sensors.erase({source, kind});
    return {0, -1, 0};
  }

  auto it = sensors.find({source, kind});
  const auto reading = it == sensors.end() ? std::nullopt : it->second.at(time);
  if (!reading) {
    return {0, -1, 0};
  }

  constexpr float gravity = 9.80665f;
  return {(*reading)[0] / gravity, -(*reading)[1] / gravity, -(*reading)[2] / gravity};
}

ChannelSample SimulatedChannelProvider::sample(uint32_t port, uint64_t time) {
  ChannelSample result;
  result.core.err = pad::connected(port) ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
  const auto source = pad::port_controller(port);
  auto target = source ? pad::ProfileTarget::controller(source) : pad::ProfileTarget::keyboard(port);
  result.core.dev = static_cast<u8>(pad::profile(target)->settings.wpadExtension);
  result.distance = distance;
  result.synthesizedPointer = true;
  auto& state = pad::gameplay_state(port);
  if (pad::detail::blocked()) {
    for (auto id : {source, input::device_source().id}) {
      std::erase_if(sensors, [&](const auto& pair) { return pair.first.first == id; });
    }

    pointers[port].valid = FALSE;
    result.core.accZ = 106;
    result.fs.fsAccZ = 204;
    return result;
  }

  if (controllerSources[port] != source) {
    const auto old = controllerSources[port];
    std::erase_if(sensors, [&](const auto& pair) { return pair.first.first == old; });
    controllerSources[port] = source;
  }

  const auto& c = controls();
  const std::pair<binding::ControlId, u16> coreButtons[]{
      {c.a, WPAD_BUTTON_A},       {c.b, WPAD_BUTTON_B},         {c.one, WPAD_BUTTON_1},       {c.two, WPAD_BUTTON_2},
      {c.plus, WPAD_BUTTON_PLUS}, {c.minus, WPAD_BUTTON_MINUS}, {c.home, WPAD_BUTTON_HOME},   {c.up, WPAD_BUTTON_UP},
      {c.down, WPAD_BUTTON_DOWN}, {c.left, WPAD_BUTTON_LEFT},   {c.right, WPAD_BUTTON_RIGHT}, {c.fs.c, WPAD_BUTTON_C},
      {c.fs.z, WPAD_BUTTON_Z},
  };

  for (auto [control, bit] : coreButtons) {
    if (state.value(control) >= 0.5f &&
        (result.core.dev == WPAD_DEV_FREESTYLE || (control != c.fs.c && control != c.fs.z))) {
      result.core.button |= bit;
    }
  }

  const auto& cl = c.cl;
  const std::pair<binding::ControlId, u16> classicButtons[]{
      {cl.a, WPAD_CL_BUTTON_A},       {cl.b, WPAD_CL_BUTTON_B},       {cl.x, WPAD_CL_BUTTON_X},
      {cl.y, WPAD_CL_BUTTON_Y},       {cl.plus, WPAD_CL_BUTTON_PLUS}, {cl.minus, WPAD_CL_BUTTON_MINUS},
      {cl.home, WPAD_CL_BUTTON_HOME}, {cl.l, WPAD_CL_TRIGGER_L},      {cl.r, WPAD_CL_TRIGGER_R},
      {cl.zl, WPAD_CL_TRIGGER_ZL},    {cl.zr, WPAD_CL_TRIGGER_ZR},    {cl.up, WPAD_CL_BUTTON_UP},
      {cl.down, WPAD_CL_BUTTON_DOWN}, {cl.left, WPAD_CL_BUTTON_LEFT}, {cl.right, WPAD_CL_BUTTON_RIGHT},
  };

  for (auto [control, bit] : classicButtons) {
    if (state.value(control) >= 0.5f) {
      result.cl.clButton |= bit;
    }
  }

  auto stick = [&](binding::ControlId id, float range) {
    return static_cast<s16>(std::lround(state.value(id) * range));
  };

  result.fs.fsStickX = static_cast<s8>(stick(c.fs.stickX, 71));
  result.fs.fsStickY = static_cast<s8>(stick(c.fs.stickY, 71));
  result.cl.clLStickX = stick(cl.leftX, 308);
  result.cl.clLStickY = stick(cl.leftY, 308);
  result.cl.clRStickX = stick(cl.rightX, 308);
  result.cl.clRStickY = stick(cl.rightY, 308);
  result.cl.clTriggerL = static_cast<u8>(std::clamp(state.value(cl.triggerL), 0.f, 1.f) * 180.f);
  result.cl.clTriggerR = static_cast<u8>(std::clamp(state.value(cl.triggerR), 0.f, 1.f) * 180.f);

  const float x = state.value(c.irX);
  const float y = state.value(c.irY);
  if (x || y) {
    pointers[port].x = std::clamp(pointers[port].x + x * 1.8f * 0.005f, -1.f, 1.f);
    pointers[port].y = std::clamp(pointers[port].y - y * 1.8f * 0.005f, -1.f, 1.f);
    pointers[port].valid = TRUE;
    pointerSources[port] = source;
  }

  if (state.value(c.recenter) >= 0.5f) {
    pointers[port] = {0, 0, TRUE};
    pointerSources[port] = source;
  }

  result.pointer = pointers[port];

  auto coreKind =
      input::has_sensor(source, SensorKind::AccelRight) || sensors.contains({source, SensorKind::AccelRight})
          ? SensorKind::AccelRight
          : SensorKind::Accel;
  auto coreSource = input::has_sensor(source, coreKind) || sensors.contains({source, coreKind}) ? source
                    : port == 0 ? input::device_source().id
                                : 0;
  for (auto [id, kind] : {std::pair{coreSource, coreKind}, std::pair{source, SensorKind::AccelLeft}}) {
    if (input::has_sensor(id, kind)) {
      input::set_sensor_enabled(id, kind, true);
    }
  }

  const auto eval = time > kSensorResampleDelay ? time - kSensorResampleDelay : 0;
  Vec core = acceleration(coreSource, coreKind, eval);
  Vec fs = acceleration(source, SensorKind::AccelLeft, eval);
  const auto wave =
      static_cast<float>(std::sin(static_cast<double>(time % 50'000'000) * (2.0 * std::numbers::pi / 50'000'000)));
  if (state.value(c.shake) >= 0.5f) {
    core.x += wave * 1.5f;
    core.y += wave * 1.5f;
    core.z += wave * 1.5f;
  }

  if (state.value(c.fs.shake) >= 0.5f) {
    fs.x += wave;
    fs.y += wave;
    fs.z += wave;
  }

  result.core.accX = raw(-core.x, 106, 360);
  result.core.accY = raw(core.z, 106, 360);
  result.core.accZ = raw(-core.y, 106, 360);
  result.fs.fsAccX = raw(-fs.x, 204, 428);
  result.fs.fsAccY = raw(fs.z, 204, 428);
  result.fs.fsAccZ = raw(-fs.y, 204, 428);
  return result;
}
} // namespace

const Controls& controls() {
  static const Controls c = [] {
    auto button = [](const char* name) { return binding::register_control({.name = name}); };

    auto axis = [](const char* name) {
      return binding::register_control({.name = name, .kind = binding::ControlKind::Axis});
    };

    Controls c{};
    c.a = button("aurora.wpad.a");
    c.b = button("aurora.wpad.b");
    c.one = button("aurora.wpad.one");
    c.two = button("aurora.wpad.two");
    c.plus = button("aurora.wpad.plus");
    c.minus = button("aurora.wpad.minus");
    c.home = button("aurora.wpad.home");
    c.up = button("aurora.wpad.up");
    c.down = button("aurora.wpad.down");
    c.left = button("aurora.wpad.left");
    c.right = button("aurora.wpad.right");
    c.recenter = button("aurora.wpad.recenter");
    c.shake = button("aurora.wpad.shake");

    c.fs.c = button("aurora.wpad.fs.c");
    c.fs.z = button("aurora.wpad.fs.z");
    c.fs.shake = button("aurora.wpad.fs.shake");

    c.cl.a = button("aurora.wpad.cl.a");
    c.cl.b = button("aurora.wpad.cl.b");
    c.cl.x = button("aurora.wpad.cl.x");
    c.cl.y = button("aurora.wpad.cl.y");
    c.cl.plus = button("aurora.wpad.cl.plus");
    c.cl.minus = button("aurora.wpad.cl.minus");
    c.cl.home = button("aurora.wpad.cl.home");
    c.cl.l = button("aurora.wpad.cl.l");
    c.cl.r = button("aurora.wpad.cl.r");
    c.cl.zl = button("aurora.wpad.cl.zl");
    c.cl.zr = button("aurora.wpad.cl.zr");
    c.cl.up = button("aurora.wpad.cl.up");
    c.cl.down = button("aurora.wpad.cl.down");
    c.cl.left = button("aurora.wpad.cl.left");
    c.cl.right = button("aurora.wpad.cl.right");
    c.irX = axis("aurora.wpad.ir_x");
    c.irY = axis("aurora.wpad.ir_y");
    c.fs.stickX = axis("aurora.wpad.fs.stick_x");
    c.fs.stickY = axis("aurora.wpad.fs.stick_y");
    c.cl.leftX = axis("aurora.wpad.cl.left_x");
    c.cl.leftY = axis("aurora.wpad.cl.left_y");
    c.cl.rightX = axis("aurora.wpad.cl.right_x");
    c.cl.rightY = axis("aurora.wpad.cl.right_y");
    c.cl.triggerL = axis("aurora.wpad.cl.trigger_l");
    c.cl.triggerR = axis("aurora.wpad.cl.trigger_r");
    std::vector<Binding> gamepad, keys;
    auto bindButton = [&](binding::ControlId target, SDL_GamepadButton button) {
      gamepad.push_back({.input = {.control = PhysicalInput::GamepadButton{button}}, .target = target});
    };

    auto bindAxis = [&](binding::ControlId target, SDL_GamepadAxis axis, float scale = 1.f) {
      gamepad.push_back({.input = {.control = PhysicalInput::GamepadAxis{axis}}, .target = target, .scale = scale});
    };

    auto bindKey = [&](binding::ControlId target, SDL_Scancode key, float scale = 1.f) {
      keys.push_back({.input = {.control = PhysicalInput::Key{key}}, .target = target, .scale = scale});
    };

    auto bindMouse = [&](binding::ControlId target, uint8_t button) {
      keys.push_back({.input = {.control = PhysicalInput::MouseButton{button}}, .target = target});
    };
    bindButton(c.a, SDL_GAMEPAD_BUTTON_SOUTH);
    bindButton(c.one, SDL_GAMEPAD_BUTTON_WEST);
    bindButton(c.two, SDL_GAMEPAD_BUTTON_NORTH);
    bindButton(c.plus, SDL_GAMEPAD_BUTTON_START);
    bindButton(c.minus, SDL_GAMEPAD_BUTTON_BACK);
    bindButton(c.home, SDL_GAMEPAD_BUTTON_GUIDE);
    bindButton(c.up, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bindButton(c.down, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    bindButton(c.left, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bindButton(c.right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    bindButton(c.recenter, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    bindButton(c.shake, SDL_GAMEPAD_BUTTON_EAST);
    bindButton(c.fs.c, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    bindButton(c.fs.shake, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    bindButton(c.cl.a, SDL_GAMEPAD_BUTTON_EAST);
    bindButton(c.cl.b, SDL_GAMEPAD_BUTTON_SOUTH);
    bindButton(c.cl.x, SDL_GAMEPAD_BUTTON_NORTH);
    bindButton(c.cl.y, SDL_GAMEPAD_BUTTON_WEST);
    bindButton(c.cl.zl, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    bindButton(c.cl.zr, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    bindButton(c.cl.plus, SDL_GAMEPAD_BUTTON_START);
    bindButton(c.cl.minus, SDL_GAMEPAD_BUTTON_BACK);
    bindButton(c.cl.home, SDL_GAMEPAD_BUTTON_GUIDE);
    bindButton(c.cl.up, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bindButton(c.cl.down, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    bindButton(c.cl.left, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bindButton(c.cl.right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    bindAxis(c.b, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.f);
    bindAxis(c.fs.z, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.f);
    bindAxis(c.irX, SDL_GAMEPAD_AXIS_RIGHTX, 1.f);
    bindAxis(c.irY, SDL_GAMEPAD_AXIS_RIGHTY, -1.f);
    bindAxis(c.fs.stickX, SDL_GAMEPAD_AXIS_LEFTX, 1.f);
    bindAxis(c.fs.stickY, SDL_GAMEPAD_AXIS_LEFTY, -1.f);
    bindAxis(c.cl.leftX, SDL_GAMEPAD_AXIS_LEFTX, 1.f);
    bindAxis(c.cl.leftY, SDL_GAMEPAD_AXIS_LEFTY, -1.f);
    bindAxis(c.cl.rightX, SDL_GAMEPAD_AXIS_RIGHTX, 1.f);
    bindAxis(c.cl.rightY, SDL_GAMEPAD_AXIS_RIGHTY, -1.f);
    bindAxis(c.cl.l, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    bindAxis(c.cl.r, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    bindAxis(c.cl.triggerL, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.f);
    bindAxis(c.cl.triggerR, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.f);
    bindKey(c.one, SDL_SCANCODE_1);
    bindKey(c.two, SDL_SCANCODE_2);
    bindKey(c.plus, SDL_SCANCODE_RETURN);
    bindKey(c.minus, SDL_SCANCODE_TAB);
    bindKey(c.home, SDL_SCANCODE_BACKSPACE);
    bindKey(c.up, SDL_SCANCODE_UP);
    bindKey(c.down, SDL_SCANCODE_DOWN);
    bindKey(c.left, SDL_SCANCODE_LEFT);
    bindKey(c.right, SDL_SCANCODE_RIGHT);
    bindKey(c.fs.z, SDL_SCANCODE_SPACE);
    bindKey(c.fs.c, SDL_SCANCODE_LSHIFT);
    bindKey(c.fs.shake, SDL_SCANCODE_Q);
    bindKey(c.fs.stickX, SDL_SCANCODE_D, 1.f);
    bindKey(c.fs.stickX, SDL_SCANCODE_A, -1.f);
    bindKey(c.fs.stickY, SDL_SCANCODE_W, 1.f);
    bindKey(c.fs.stickY, SDL_SCANCODE_S, -1.f);
    bindMouse(c.a, SDL_BUTTON_LEFT);
    bindMouse(c.b, SDL_BUTTON_RIGHT);
    bindMouse(c.shake, SDL_BUTTON_MIDDLE);
    pad::set_default_controller_bindings(std::move(gamepad));
    pad::set_default_keyboard_bindings(0, std::move(keys));
    return c;
  }();
  return c;
}

void initialize_provider() {
  (void)controls();
  if (simulated) {
    return;
  }

  simulated = std::make_shared<SimulatedChannelProvider>();
  layer = input::register_layer({
      .label = "aurora.wpad.sensors",
      .priority = input::kGameLayerPriority,
      .onEvent = [](const InputEvent& event, void*) { return simulated->event(event); },
  });
}

void shutdown_provider() {
  if (layer != input::kInvalidLayerId) {
    input::unregister_layer(layer);
  }

  layer = input::kInvalidLayerId;
  simulated.reset();
  providers = {};
}

void set_channel_provider(uint32_t port, std::shared_ptr<ChannelProvider> provider) {
  if (port < 4) {
    providers[port] = std::move(provider);
  }
}

bool is_simulated_channel(uint32_t port) { return !providers[port]; }

ChannelSample read_channel(uint32_t port, uint64_t timestampNs) {
  return providers[port] ? providers[port]->sample(port, timestampNs) : simulated->sample(port, timestampNs);
}

void install_hooks(void (*update)(), void (*shutdown)()) {
  input::detail::wpad_update = update;
  input::detail::wpad_shutdown = shutdown;
}

uint64_t timestamp() { return clockFn(); }
double now() { return static_cast<double>(timestamp()) / 1e9; }
float sensor_bar_distance() { return distance; }
void update() { aurora_wpad_update(); }
void shutdown() { aurora_wpad_shutdown(); }
namespace detail {

AuroraWpadPointer map_window_pointer(SDL_FPoint position, const AuroraWindowSize& size) {
  return map_pointer(position, size);
}

void set_clock(uint64_t (*clock)()) { clockFn = clock ? clock : SDL_GetTicksNS; }
} // namespace detail
} // namespace aurora::wpad

void aurora_wpad_set_sensor_bar_distance(f32 value) {
  if (std::isfinite(value) && value > 0) {
    aurora::wpad::distance = value;
  }
}

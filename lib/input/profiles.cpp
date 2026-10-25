#include "port.hpp"

#include "../gamepad.hpp"
#include "../internal.hpp"
#include "../io.hpp"

#include <nlohmann/json.hpp>
#include <SDL3/SDL_guid.h>
#include <algorithm>
#include <cmath>
#include <list>
#include <set>

namespace aurora::pad {
namespace {

using binding::Binding;
using binding::PhysicalInput;
using Direction = PhysicalInput::GamepadAxis::Direction;
using Json = nlohmann::json;

std::array<Profile, 4> keyboards;
std::list<Profile> controllers;
std::array<PortPreference, 4> preferences{{{PortAssignment::Auto}, {}, {}, {}}};
std::array<input::SourceId, 4> assigned{};
std::array<std::vector<Binding>, 4> keyboardDefaults;
std::vector<Binding> controllerDefaults;
std::filesystem::path directory;
std::optional<LegacyMigration> migration;
uint64_t generation = 1;
bool loaded = false;

std::string normalize_serial(std::string serial) {
  std::erase_if(serial, [](char c) { return c == ':' || c == '-'; });
  for (char& c : serial) {
    if (c >= 'A' && c <= 'Z') {
      c += 'a' - 'A';
    }
  }

  return serial;
}

bool adapter(const ControllerIdentity& id) { return id.vid == 0x057e && id.pid == 0x0337; }

int match(const ControllerIdentity& saved, const ControllerIdentity& current) {
  if (saved.guid.empty() || (adapter(saved) && saved.port != current.port)) {
    return 0;
  }

  const bool serial = !saved.serial.empty() && normalize_serial(saved.serial) == normalize_serial(current.serial);
  if (saved.guid == current.guid && (saved.serial.empty() || serial)) {
    return 2;
  }

  return serial && saved.vid == current.vid && saved.pid == current.pid ? 1 : 0;
}

void assign() {
  std::vector<input::SourceId> available;
  for (const auto& source : input::sources()) {
    if (source.kind == input::InputSource::Kind::Controller && controller_identity(source.id)) {
      available.push_back(source.id);
    }
  }

  std::ranges::sort(available);
  std::array<input::SourceId, 4> next{};
  std::set<input::SourceId> claimed;
  // Pinned controllers have priority over automatic assignments.
  for (uint32_t port = 0; port < 4; ++port) {
    if (preferences[port].state != PortAssignment::Controller) {
      continue;
    }

    int best = 0;
    for (const auto source : available) {
      auto id = *controller_identity(source);
      if (adapter(id)) {
        id.port = port;
      }

      const int score = match(preferences[port].identity, id);
      if (!claimed.contains(source) && score > 0 && (score > best || (score == best && source == assigned[port]))) {
        next[port] = source;
        best = score;
      }
    }

    if (next[port]) {
      claimed.insert(next[port]);
    }
  }

  for (uint32_t port = 0; port < 4; ++port) {
    if (preferences[port].state == PortAssignment::Auto) {
      // Keep the automatic assignment stable when another controller connects.
      if (std::ranges::find(available, assigned[port]) != available.end() && !claimed.contains(assigned[port])) {
        next[port] = assigned[port];
      } else {
        for (auto source : available) {
          if (!claimed.contains(source)) {
            next[port] = source;
            break;
          }
        }
      }

      if (next[port]) {
        claimed.insert(next[port]);
      }
    }
  }

  if (assigned != next) {
    assigned = next;
    ++generation;
  }

  for (const auto source : available) {
    auto* gamepad = SDL_GetGamepadFromID(input::gamepad_for_source(source));
    const auto it = std::ranges::find(assigned, source);
    const int player = it == assigned.end() ? -1 : static_cast<int>(it - assigned.begin());
    if (SDL_GetGamepadPlayerIndex(gamepad) != player) {
      SDL_SetGamepadPlayerIndex(gamepad, player);
    }
  }
}

Profile* get(ProfileTarget target) {
  if (target.kind == ProfileTarget::Kind::Keyboard) {
    return target.id < 4 ? &keyboards[target.id] : nullptr;
  }

  auto identity = controller_identity(target.id);
  if (!identity) {
    return nullptr;
  }

  assign();
  if (adapter(*identity)) {
    const auto it = std::ranges::find(assigned, target.id);
    identity->port = it == assigned.end() ? 0 : static_cast<uint32_t>(it - assigned.begin());
  }

  auto found = controllers.end();
  int best = 0;
  for (auto it = controllers.begin(); it != controllers.end(); ++it) {
    if (int score = match(it->identity, *identity); score > best) {
      found = it;
      best = score;
    }
  }

  if (found != controllers.end()) {
    // Preserve fallback identities and remember the most recently used profile.
    if (found != controllers.begin()) {
      controllers.splice(controllers.begin(), controllers, found);
    }

    return &controllers.front();
  }

  Profile profile;
  profile.settings.emulateTriggers = !(adapter(*identity) || identity->pid == 0x2073);
  for (const auto& candidate : controllers) {
    if (candidate.identity.vid == identity->vid && candidate.identity.pid == identity->pid &&
        (!adapter(*identity) || candidate.identity.port == identity->port)) {
      profile = candidate;
      break;
    }
  }

  profile.identity = *identity;
  controllers.insert(controllers.begin(), std::move(profile));
  ++generation;
  return &controllers.front();
}

bool accepts(ProfileTarget target, const PhysicalInput& input) {
  const bool keyboard = input.control.is<PhysicalInput::Key>() || input.control.is<PhysicalInput::MouseButton>();
  if (keyboard != (target.kind == ProfileTarget::Kind::Keyboard)) {
    return false;
  }

  return input.control.match(
      [](PhysicalInput::Key k) { return k.scancode > SDL_SCANCODE_UNKNOWN && k.scancode < SDL_SCANCODE_COUNT; },
      [](PhysicalInput::MouseButton m) { return m.button >= 1 && m.button <= 32; },
      [](PhysicalInput::GamepadButton b) { return b.button >= 0 && b.button < SDL_GAMEPAD_BUTTON_COUNT; },
      [](PhysicalInput::GamepadAxis a) {
        return a.axis >= 0 && a.axis < SDL_GAMEPAD_AXIS_COUNT &&
               (a.direction == Direction::Full || a.direction == Direction::Positive ||
                a.direction == Direction::Negative);
      });
}

bool valid_settings(const ProfileSettings& settings) {
  const float values[]{
      settings.leftStickDeadZone,     settings.rightStickDeadZone, settings.leftTriggerThreshold,
      settings.rightTriggerThreshold, settings.rumbleIntensityLow, settings.rumbleIntensityHigh,
  };

  return settings.wpadExtension <= 2 && settings.leftStickDeadZone < 1 && settings.rightStickDeadZone < 1 &&
         std::ranges::all_of(values, [](float f) { return std::isfinite(f) && f >= 0 && f <= 1; });
}

bool valid_binding(ProfileTarget target, const Binding& b) {
  return accepts(target, b.input) && std::isfinite(b.scale) && std::isfinite(b.deadZone) && b.deadZone >= 0.f &&
         b.deadZone < 1.f && std::isfinite(b.threshold) && b.threshold >= 0.f && b.threshold <= 1.f &&
         std::ranges::all_of(
             b.held,
             [&](const auto& held) {
               return accepts(target, held.input) && std::isfinite(held.threshold) && held.threshold >= 0.f &&
                      held.threshold <= 1.f;
             });
}

std::vector<Binding> defaults(ProfileTarget target) {
  if (target.kind == ProfileTarget::Kind::Keyboard) {
    return target.id < 4 ? keyboardDefaults[target.id] : std::vector<Binding>{};
  }

  auto result = detail::default_pad_bindings(target.id);
  std::erase_if(result, [&](const auto& old) {
    return std::ranges::any_of(controllerDefaults, [&](const auto& b) { return b.target == old.target; });
  });

  result.insert(result.end(), controllerDefaults.begin(), controllerDefaults.end());
  return result;
}

std::map<std::string, std::vector<Binding>, std::less<>> effective(ProfileTarget target, const Profile& profile) {
  std::map<std::string, std::vector<Binding>, std::less<>> result;
  for (auto binding : defaults(target)) {
    if (const auto* desc = binding::describe_control(binding.target)) {
      result[desc->name].push_back(std::move(binding));
    }
  }

  for (const auto& [name, bindings] : profile.bindings) {
    result[name] = bindings;
  }

  return result;
}

void append(binding::BindingSet& set, ProfileTarget target) {
  const auto* p = get(target);
  if (!p) {
    return;
  }

  const auto settings = p->settings;
  auto bindings = effective(target, *p);
  if (target.kind == ProfileTarget::Kind::Controller && settings.emulateTriggers) {
    for (const auto prefix : {"aurora.pad.", "aurora.wpad.cl."}) {
      for (const bool left : {true, false}) {
        const std::string digital = std::string(prefix) + (left ? "l" : "r");
        const std::string analog = std::string(prefix) + (left ? "trigger_l" : "trigger_r");
        if (!bindings[digital].empty()) {
          continue;
        }

        for (auto b : bindings[analog]) {
          if (b.input.control.is<PhysicalInput::GamepadAxis>()) {
            b.threshold = left ? settings.leftTriggerThreshold : settings.rightTriggerThreshold;
            bindings[digital].push_back(std::move(b));
          }
        }
      }
    }
  }

  for (auto& [name, list] : bindings) {
    const auto control = binding::find_control(name);
    if (!control) {
      continue;
    }

    for (auto b : list) {
      if (!accepts(target, b.input)) {
        continue;
      }

      auto resolve = [&](PhysicalInput& input) {
        input.source = input.control.is<PhysicalInput::Key>()           ? input::keyboard_source().id
                       : input.control.is<PhysicalInput::MouseButton>() ? input::mouse_source().id
                                                                        : target.id;
      };
      resolve(b.input);
      for (auto& held : b.held) {
        resolve(held.input);
      }

      b.target = control;
      if (const auto* axis = b.input.control.get_if<PhysicalInput::GamepadAxis>()) {
        if (axis->axis == SDL_GAMEPAD_AXIS_LEFTX || axis->axis == SDL_GAMEPAD_AXIS_LEFTY) {
          b.deadZone = settings.useDeadzones ? settings.leftStickDeadZone : 0.f;
        } else if (axis->axis == SDL_GAMEPAD_AXIS_RIGHTX || axis->axis == SDL_GAMEPAD_AXIS_RIGHTY) {
          b.deadZone = settings.useDeadzones ? settings.rightStickDeadZone : 0.f;
        }
      }

      set.bindings.push_back(std::move(b));
    }
  }
}

std::string group(const binding::ControlDescriptor& desc) {
  if (!desc.conflictGroup.empty()) {
    return desc.conflictGroup;
  }

  if (desc.name.starts_with("aurora.wpad.cl.")) {
    return "aurora.wpad.cl";
  }

  if (desc.name.starts_with("aurora.wpad.")) {
    return "aurora.wpad";
  }

  return desc.name.substr(0, desc.name.starts_with("aurora.pad.") ? 10 : desc.name.find('.'));
}

bool linked(std::string_view a, std::string_view b) {
  for (std::string_view prefix : {"aurora.pad.", "aurora.wpad.cl."}) {
    if (a.starts_with(prefix) && b.starts_with(prefix)) {
      a.remove_prefix(prefix.size());
      b.remove_prefix(prefix.size());
      return (a == "l" && b == "trigger_l") || (b == "l" && a == "trigger_l") || (a == "r" && b == "trigger_r") ||
             (b == "r" && a == "trigger_r");
    }
  }

  return false;
}

// Stable serialization names are independent of the active keyboard layout.
constexpr std::pair<SDL_Scancode, std::string_view> keyTokens[]{
    {SDL_SCANCODE_SPACE, "space"},         {SDL_SCANCODE_LSHIFT, "lshift"}, {SDL_SCANCODE_RSHIFT, "rshift"},
    {SDL_SCANCODE_RETURN, "return"},       {SDL_SCANCODE_ESCAPE, "escape"}, {SDL_SCANCODE_TAB, "tab"},
    {SDL_SCANCODE_BACKSPACE, "backspace"}, {SDL_SCANCODE_UP, "up"},         {SDL_SCANCODE_DOWN, "down"},
    {SDL_SCANCODE_LEFT, "left"},           {SDL_SCANCODE_RIGHT, "right"},   {SDL_SCANCODE_LCTRL, "lctrl"},
    {SDL_SCANCODE_RCTRL, "rctrl"},         {SDL_SCANCODE_LALT, "lalt"},     {SDL_SCANCODE_RALT, "ralt"},
};
constexpr std::string_view buttonTokens[]{
    "south",     "east",      "west",     "north", "back",  "guide", "start", "lstick",   "rstick",
    "lshoulder", "rshoulder", "up",       "down",  "left",  "right", "misc1", "rpaddle1", "lpaddle1",
    "rpaddle2",  "lpaddle2",  "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6",
};
constexpr std::string_view axisTokens[]{"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
constexpr std::string_view mouseTokens[]{"", "left", "middle", "right", "x1", "x2"};

Json key_token(SDL_Scancode key) {
  if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
    return std::string(1, 'a' + key - SDL_SCANCODE_A);
  }

  if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) {
    return std::string(1, key == SDL_SCANCODE_0 ? '0' : '1' + key - SDL_SCANCODE_1);
  }

  if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) {
    return "f" + std::to_string(key - SDL_SCANCODE_F1 + 1);
  }

  for (auto [code, token] : keyTokens) {
    if (code == key) {
      return token;
    }
  }

  return static_cast<int>(key);
}

template <size_t N>
int token_index(const Json& value, const std::string_view (&tokens)[N]) {
  if (!value.is_string()) {
    return -1;
  }

  const auto token = value.get<std::string>();
  const auto it = std::ranges::find(tokens, token);
  return it == std::end(tokens) ? -1 : static_cast<int>(it - std::begin(tokens));
}

Json encode_binding(const Binding& b) {
  Json j = b.input.control.match(
      [](PhysicalInput::Key k) {
        return Json{
            {"type", "key"},
            {"key", key_token(k.scancode)},
        };
      },
      [](PhysicalInput::MouseButton m) {
        return Json{
            {"type", "mouse_button"},
            {"button", m.button < 6 ? Json(mouseTokens[m.button]) : Json(m.button)},
        };
      },
      [](PhysicalInput::GamepadButton b) {
        return Json{
            {"type", "gamepad_button"},
            {"button", buttonTokens[b.button]},
        };
      },
      [](PhysicalInput::GamepadAxis a) {
        return Json{
            {"type", "gamepad_axis"},
            {"axis", axisTokens[a.axis]},
            {
                "direction",
                a.direction == Direction::Full       ? "full"
                : a.direction == Direction::Positive ? "positive"
                                                     : "negative",
            },
        };
      });
  if (b.scale != 1.f) {
    j["scale"] = b.scale;
  }

  if (b.threshold != 0.5f) {
    j["threshold"] = b.threshold;
  }

  if (b.deadZone != 0.f) {
    j["deadZone"] = b.deadZone;
  }

  if (!b.held.empty()) {
    j["held"] = Json::array();
    for (const auto& h : b.held) {
      j["held"].push_back(encode_binding({.input = h.input, .threshold = h.threshold}));
    }
  }

  return j;
}

Binding decode_binding(const Json& j) {
  Binding b;
  const auto type = j.at("type").get<std::string>();
  if (type == "key") {
    int code = -1;
    for (int i = 1; i < SDL_SCANCODE_COUNT; ++i) {
      if (key_token(static_cast<SDL_Scancode>(i)) == j.at("key")) {
        code = i;
        break;
      }
    }

    if (code <= 0) {
      throw std::runtime_error("invalid scancode");
    }

    b.input.control = PhysicalInput::Key{static_cast<SDL_Scancode>(code)};
  } else if (type == "mouse_button") {
    int code =
        j.at("button").is_number_integer() ? j.at("button").get<int>() : token_index(j.at("button"), mouseTokens);
    if (code <= 0 || code > 32) {
      throw std::runtime_error("invalid mouse button");
    }

    b.input.control = PhysicalInput::MouseButton{static_cast<uint8_t>(code)};
  } else if (type == "gamepad_button") {
    const int code = token_index(j.at("button"), buttonTokens);
    if (code < 0) {
      throw std::runtime_error("invalid gamepad button");
    }

    b.input.control = PhysicalInput::GamepadButton{static_cast<SDL_GamepadButton>(code)};
  } else if (type == "gamepad_axis") {
    const int code = token_index(j.at("axis"), axisTokens);
    const auto direction = j.value("direction", "full");
    if (code < 0 || (direction != "full" && direction != "positive" && direction != "negative")) {
      throw std::runtime_error("invalid gamepad axis");
    }

    b.input.control = PhysicalInput::GamepadAxis{
        static_cast<SDL_GamepadAxis>(code),
        direction == "full"       ? Direction::Full
        : direction == "positive" ? Direction::Positive
                                  : Direction::Negative,
    };
  } else {
    throw std::runtime_error("invalid input type");
  }

  b.scale = j.value("scale", 1.f);
  b.threshold = j.value("threshold", 0.5f);
  b.deadZone = j.value("deadZone", 0.f);
  if (!std::isfinite(b.scale) || !std::isfinite(b.threshold) || !std::isfinite(b.deadZone)) {
    throw std::runtime_error("nonfinite binding");
  }

  if (j.contains("held")) {
    for (const auto& item : j.at("held")) {
      auto held = decode_binding(item);
      b.held.push_back({held.input, held.threshold});
    }
  }

  return b;
}

Json encode_identity(const ControllerIdentity& id) {
  Json j{
      {"guid", id.guid},
      {"serial", id.serial},
      {"vid", fmt::format("{:04X}", id.vid)},
      {"pid", fmt::format("{:04X}", id.pid)},
      {"name", id.name},
  };

  if (id.port) {
    j["port"] = *id.port;
  }

  return j;
}

ControllerIdentity decode_identity(const Json& j) {
  ControllerIdentity id;
  id.guid = j.value("guid", "");
  id.serial = normalize_serial(j.value("serial", ""));
  auto number = [&](const char* key) -> uint16_t {
    if (!j.contains(key)) {
      return 0;
    }

    return j.at(key).is_string() ? static_cast<uint16_t>(std::stoul(j.at(key).get<std::string>(), nullptr, 16))
                                 : j.at(key).get<uint16_t>();
  };

  id.vid = number("vid");
  id.pid = number("pid");
  if (!id.guid.empty() && (!id.vid || !id.pid)) {
    SDL_GetJoystickGUIDInfo(SDL_StringToGUID(id.guid.c_str()), &id.vid, &id.pid, nullptr, nullptr);
  }

  id.name = j.value("name", "");
  if (j.contains("port")) {
    id.port = j.at("port").get<uint32_t>();
  }

  return id;
}

Json encode_profile(const Profile& p) {
  const char* extension = "freestyle";
  if (p.settings.wpadExtension == 0) {
    extension = "core";
  } else if (p.settings.wpadExtension == 2) {
    extension = "classic";
  }

  Json j{
      {"bindings", Json::object()},
      {"settings",
       {
           {"useDeadzones", p.settings.useDeadzones},
           {"leftStickDeadZone", p.settings.leftStickDeadZone},
           {"rightStickDeadZone", p.settings.rightStickDeadZone},
           {"emulateTriggers", p.settings.emulateTriggers},
           {"leftTriggerThreshold", p.settings.leftTriggerThreshold},
           {"rightTriggerThreshold", p.settings.rightTriggerThreshold},
           {"rumbleIntensityLow", p.settings.rumbleIntensityLow},
           {"rumbleIntensityHigh", p.settings.rumbleIntensityHigh},
           {"forceDeviceRumble", p.settings.forceDeviceRumble},
           {"wpadExtension", extension},
       }},
  };

  for (const auto& [name, list] : p.bindings) {
    Json values = Json::array();
    for (const auto& b : list) {
      values.push_back(encode_binding(b));
    }

    j["bindings"][name] = values.empty() ? Json() : values.size() == 1 ? values.front() : values;
  }

  return j;
}

Profile decode_profile(const Json& j, ProfileTarget target) {
  Profile p;
  if (j.contains("identity")) {
    p.identity = decode_identity(j.at("identity"));
    p.settings.emulateTriggers = !(adapter(p.identity) || p.identity.pid == 0x2073);
  }

  if (j.contains("settings")) {
    const auto& settings = j.at("settings");
    p.settings.useDeadzones = settings.value("useDeadzones", p.settings.useDeadzones);
    p.settings.leftStickDeadZone = settings.value("leftStickDeadZone", p.settings.leftStickDeadZone);
    p.settings.rightStickDeadZone = settings.value("rightStickDeadZone", p.settings.rightStickDeadZone);
    p.settings.emulateTriggers = settings.value("emulateTriggers", p.settings.emulateTriggers);
    p.settings.leftTriggerThreshold = settings.value("leftTriggerThreshold", p.settings.leftTriggerThreshold);
    p.settings.rightTriggerThreshold = settings.value("rightTriggerThreshold", p.settings.rightTriggerThreshold);
    p.settings.rumbleIntensityLow = settings.value("rumbleIntensityLow", p.settings.rumbleIntensityLow);
    p.settings.rumbleIntensityHigh = settings.value("rumbleIntensityHigh", p.settings.rumbleIntensityHigh);
    p.settings.forceDeviceRumble = settings.value("forceDeviceRumble", p.settings.forceDeviceRumble);

    auto ext = settings.value("wpadExtension", "freestyle");
    if (ext != "core" && ext != "classic" && ext != "freestyle") {
      throw std::runtime_error("invalid extension");
    }

    p.settings.wpadExtension = ext == "core" ? 0 : ext == "classic" ? 2 : 1;
  }

  if (!valid_settings(p.settings)) {
    throw std::runtime_error("invalid profile settings");
  }

  if (j.contains("bindings")) {
    for (const auto& [name, item] : j.at("bindings").items()) {
      auto& list = p.bindings[name];
      if (!item.is_null()) {
        auto add = [&](const Json& value) {
          auto b = decode_binding(value);
          if (!valid_binding(target, b)) {
            throw std::runtime_error("input does not belong to profile");
          }

          list.push_back(std::move(b));
        };

        if (item.is_array()) {
          for (const auto& value : item) {
            add(value);
          }
        } else {
          add(item);
        }
      }
    }
  }

  return p;
}

} // namespace

std::optional<ControllerIdentity> controller_identity(input::SourceId source) {
  auto* gamepad = SDL_GetGamepadFromID(input::gamepad_for_source(source));
  if (!gamepad) {
    return std::nullopt;
  }

  ControllerIdentity id;
  char guid[33]{};
  SDL_GUIDToString(SDL_GetGamepadGUIDForID(SDL_GetGamepadID(gamepad)), guid, sizeof(guid));
  id.guid = guid;
  const char* serial = SDL_GetGamepadSerial(gamepad);
  id.serial = normalize_serial(serial ? serial : "");
  id.vid = SDL_GetGamepadVendor(gamepad);
  id.pid = SDL_GetGamepadProduct(gamepad);
  const char* name = SDL_GetGamepadName(gamepad);
  id.name = name ? name : "Controller";
  return id;
}

const Profile* profile(ProfileTarget target) {
  detail::initialize_profiles();
  return get(target);
}

std::vector<Binding> bindings_for_control(ProfileTarget target, binding::ControlId control) {
  detail::initialize_profiles();
  const auto* p = get(target);
  const auto* desc = binding::describe_control(control);
  if (!p || !desc) {
    return {};
  }

  auto all = effective(target, *p);
  auto result = all[desc->name];
  for (auto& b : result) {
    b.target = control;
  }

  return result;
}

std::vector<Binding> bindings_for_control(uint32_t port, binding::ControlId control) {
  std::vector<Binding> result;
  if (auto set = binding_set(port)) {
    for (const auto& b : set->bindings) {
      if (b.target == control) {
        result.push_back(b);
      }
    }
  }

  return result;
}

bool set_binding(ProfileTarget target, std::string_view control, std::vector<Binding> bindings) {
  detail::initialize_profiles();
  auto* p = get(target);
  if (!p || std::ranges::any_of(bindings, [&](const auto& b) { return !valid_binding(target, b); })) {
    return false;
  }

  for (auto& b : bindings) {
    b.input.source = 0;
    for (auto& h : b.held) {
      h.input.source = 0;
    }
  }

  p->bindings[std::string(control)] = std::move(bindings);
  ++generation;
  return true;
}

bool set_binding(ProfileTarget target, binding::ControlId control, std::vector<Binding> bindings) {
  const auto* desc = binding::describe_control(control);
  return desc && set_binding(target, desc->name, std::move(bindings));
}

bool set_settings(ProfileTarget target, ProfileSettings settings) {
  detail::initialize_profiles();
  auto* p = get(target);
  if (!p || !valid_settings(settings)) {
    return false;
  }

  p->settings = settings;
  ++generation;
  return true;
}

void reset_to_defaults(ProfileTarget target) {
  detail::initialize_profiles();
  if (auto* p = get(target)) {
    p->bindings.clear();
    ++generation;
  }
}

void clear_bindings(ProfileTarget target) {
  detail::initialize_profiles();
  if (auto* p = get(target)) {
    for (auto& [name, list] : effective(target, *p)) {
      p->bindings[name] = {};
    }

    ++generation;
  }
}

void set_default_keyboard_bindings(uint32_t port, std::vector<Binding> bindings) {
  if (port < 4) {
    std::erase_if(bindings, [&](const auto& b) { return !accepts(ProfileTarget::keyboard(port), b.input); });
    std::erase_if(keyboardDefaults[port], [&](const auto& old) {
      return std::ranges::any_of(bindings, [&](const auto& b) { return b.target == old.target; });
    });

    keyboardDefaults[port].insert(keyboardDefaults[port].end(), bindings.begin(), bindings.end());
    ++generation;
  }
}

void set_default_controller_bindings(std::vector<Binding> bindings) {
  std::erase_if(bindings, [&](const auto& b) { return !accepts(ProfileTarget::controller(0), b.input); });
  std::erase_if(controllerDefaults, [&](const auto& old) {
    return std::ranges::any_of(bindings, [&](const auto& b) { return b.target == old.target; });
  });

  controllerDefaults.insert(controllerDefaults.end(), bindings.begin(), bindings.end());
  ++generation;
}

std::vector<binding::ControlId> find_conflicts(ProfileTarget target, binding::ControlId control,
                                               const PhysicalInput& input) {
  detail::initialize_profiles();
  std::vector<binding::ControlId> result;
  const auto* p = get(target);
  const auto* desc = binding::describe_control(control);
  if (!p || !desc) {
    return result;
  }

  for (const auto& [name, list] : effective(target, *p)) {
    const auto other = binding::find_control(name);
    const auto* od = binding::describe_control(other);
    if (other != control && od && group(*od) == group(*desc) && !linked(name, desc->name) &&
        std::ranges::any_of(list, [&](const auto& b) { return binding::same_input(b.input, input); })) {
      result.push_back(other);
    }
  }

  return result;
}

bool rebind(ProfileTarget target, binding::ControlId control, Binding b, ConflictPolicy policy) {
  if (!valid_binding(target, b) || !profile(target) || !binding::describe_control(control)) {
    return false;
  }

  if (policy == ConflictPolicy::Unbind) {
    for (auto other : find_conflicts(target, control, b.input)) {
      auto list = bindings_for_control(target, other);
      std::erase_if(list, [&](const auto& old) { return binding::same_input(old.input, b.input); });
      set_binding(target, other, std::move(list));
    }
  }

  return set_binding(target, control, {std::move(b)});
}

bool rebind(ProfileTarget target, binding::ControlId control, PhysicalInput input, ConflictPolicy policy) {
  return rebind(target, control, Binding{.input = input}, policy);
}

PortPreference port_preference(uint32_t port) {
  detail::initialize_profiles();
  return port < 4 ? preferences[port] : PortPreference{};
}

input::SourceId port_controller(uint32_t port) {
  detail::initialize_profiles();
  assign();
  return port < 4 ? assigned[port] : 0;
}

bool set_port_controller(uint32_t port, input::SourceId source) {
  detail::initialize_profiles();
  assign();
  auto id = controller_identity(source);
  if (port >= 4 || !id) {
    return false;
  }

  if (adapter(*id)) {
    id->port = port;
  }

  for (uint32_t other = 0; other < 4; ++other) {
    if (other != port && assigned[other] == source) {
      preferences[other] = {};
    }
  }

  preferences[port] = {PortAssignment::Controller, *id};
  assigned[port] = source;
  assign();
  ++generation;
  return true;
}

void set_port_auto(uint32_t port) {
  detail::initialize_profiles();
  if (port < 4) {
    preferences[port] = {PortAssignment::Auto};
    assign();
    ++generation;
  }
}

void clear_port(uint32_t port) {
  detail::initialize_profiles();
  if (port < 4) {
    preferences[port] = {};
    assign();
    ++generation;
  }
}

const std::optional<LegacyMigration>& legacy_migration() { return migration; }

bool save_profiles() {
  if (directory.empty()) {
    return false;
  }

  Json j{
      {"$version", 1},
      {"ports", Json::array()},
      {"keyboard", Json::array()},
      {"controllers", Json::array()},
  };
  uint16_t low;
  uint16_t high;
  gamepad::get_device_rumble_intensity(&low, &high);
  j["device"] = {
      {"rumbleIntensityLow", static_cast<float>(low) / 65535.f},
      {"rumbleIntensityHigh", static_cast<float>(high) / 65535.f},
  };
  for (const auto& pref : preferences) {
    Json port = pref.state == PortAssignment::Controller ? encode_identity(pref.identity) : Json::object();
    port["state"] = pref.state == PortAssignment::Auto   ? "auto"
                    : pref.state == PortAssignment::None ? "none"
                                                         : "controller";
    j["ports"].push_back(port);
  }

  for (const auto& p : keyboards) {
    j["keyboard"].push_back(encode_profile(p));
  }

  for (const auto& p : controllers) {
    auto item = encode_profile(p);
    item["identity"] = encode_identity(p.identity);
    j["controllers"].push_back(item);
  }

  auto file = io::open_atomic_file(directory / "input.json");
  const auto data = j.dump(2) + "\n";
  return file && io::write_exact(file.get(), data.data(), data.size()) && file.commit();
}

bool load_profiles(const std::filesystem::path& path) {
  directory = path;
  gamepad::resolve_ports = [] { assign(); };
  loaded = true;
  migration.reset();
  keyboards = {};
  controllers.clear();
  preferences = {{{PortAssignment::Auto}, {}, {}, {}}};
  assigned = {};
  ++generation;
  if (!std::filesystem::exists(path / "input.json")) {
    std::vector<std::filesystem::path> migrated;
    migration.emplace();
    detail::migrate_legacy(path, migrated, *migration);
    if (migrated.empty()) {
      migration.reset();
    }

    if (!save_profiles()) {
      return false;
    }

    for (const auto& file : migrated) {
      std::error_code ec;
      std::filesystem::remove(file, ec);
    }

    return true;
  }

  try {
    auto file = io::open_file(path / "input.json", "rb");
    if (!file) {
      return false;
    }

    const auto size = SDL_GetIOSize(file.get());
    if (size < 0 || size > 16 * 1024 * 1024) {
      return false;
    }

    std::string data(static_cast<size_t>(size), '\0');
    if (!io::read_exact(file.get(), data.data(), data.size())) {
      return false;
    }

    const auto j = Json::parse(data);
    if (j.at("$version") != 1) {
      return false;
    }

    auto newKeyboards = keyboards;
    auto newPreferences = preferences;
    std::list<Profile> newControllers;
    if (j.contains("ports")) {
      for (size_t port = 0; port < std::min<size_t>(4, j.at("ports").size()); ++port) {
        const auto& entry = j.at("ports")[port];
        auto state = entry.at("state").get<std::string>();
        if (state != "auto" && state != "none" && state != "controller") {
          return false;
        }

        newPreferences[port] = {
            state == "auto"   ? PortAssignment::Auto
            : state == "none" ? PortAssignment::None
                              : PortAssignment::Controller,
            decode_identity(entry),
        };
      }
    }

    if (j.contains("keyboard")) {
      for (size_t port = 0; port < std::min<size_t>(4, j.at("keyboard").size()); ++port) {
        newKeyboards[port] = decode_profile(j.at("keyboard")[port], ProfileTarget::keyboard(port));
      }
    }

    if (j.contains("controllers")) {
      for (const auto& item : j.at("controllers")) {
        newControllers.push_back(decode_profile(item, ProfileTarget::controller(0)));
      }
    }

    keyboards = std::move(newKeyboards);
    controllers = std::move(newControllers);
    preferences = std::move(newPreferences);
    if (j.contains("device")) {
      auto intensity = [&](const char* key) {
        return static_cast<uint16_t>(std::clamp(j.at("device").value(key, 0.5f), 0.f, 1.f) * 65535.f);
      };

      gamepad::set_device_rumble_intensity(intensity("rumbleIntensityLow"), intensity("rumbleIntensityHigh"));
    }

    return true;
  } catch (const std::exception& e) {
    Module{"aurora::pad"}.warn("Unable to load input.json: {}", e.what());
    return false;
  }
}

namespace detail {

void initialize_profiles() {
  if (!loaded) {
    loaded = true;
    gamepad::resolve_ports = [] { assign(); };
    (void)controls();
    if (g_config.userPath) {
      load_profiles(io::fs_path_from_string(g_config.userPath));
    }
  }
}

uint64_t profile_generation() {
  initialize_profiles();
  assign();
  return generation + binding::control_generation();
}

std::shared_ptr<const binding::BindingSet> resolve_port(uint32_t port) {
  auto set = std::make_shared<binding::BindingSet>();
  append(*set, ProfileTarget::keyboard(port));
  if (auto source = port_controller(port)) {
    append(*set, ProfileTarget::controller(source));
  }

  return set;
}

std::shared_ptr<const binding::BindingSet> resolve_controller(input::SourceId source) {
  initialize_profiles();
  auto set = std::make_shared<binding::BindingSet>();
  append(*set, ProfileTarget::controller(source));
  return set;
}

// Migration installs model templates without consuming them on first connection.
void add_legacy_controller(Profile p) {
  controllers.push_back(std::move(p));
  ++generation;
}

void set_legacy_preference(uint32_t port, PortPreference p) {
  preferences[port] = std::move(p);
  ++generation;
}

} // namespace detail
} // namespace aurora::pad

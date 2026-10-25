#include "port.hpp"

#include "legacy.hpp"
#include "../gamepad.hpp"
#include "../internal.hpp"
#include "../io.hpp"

#include <algorithm>
#include <fstream>
#include <set>

namespace aurora::pad::detail {

void add_legacy_controller(Profile profile);
void set_legacy_preference(uint32_t port, PortPreference preference);

namespace {

using binding::Binding;
using binding::PhysicalInput;
using Direction = PhysicalInput::GamepadAxis::Direction;

constexpr std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsStandard{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsXBox360{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsGamecube{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {SDL_GAMEPAD_BUTTON_MISC3, PAD_TRIGGER_L},
    {SDL_GAMEPAD_BUTTON_MISC4, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsNSOGamecube{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_BACK, PAD_TRIGGER_Z},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_TRIGGER_L},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsProCon{{
    {SDL_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {SDL_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> kUnboundKeys{{
    {PAD_KEY_INVALID, PAD_BUTTON_A},
    {PAD_KEY_INVALID, PAD_BUTTON_B},
    {PAD_KEY_INVALID, PAD_BUTTON_X},
    {PAD_KEY_INVALID, PAD_BUTTON_Y},
    {PAD_KEY_INVALID, PAD_BUTTON_START},
    {PAD_KEY_INVALID, PAD_TRIGGER_Z},
    {PAD_KEY_INVALID, PAD_TRIGGER_L},
    {PAD_KEY_INVALID, PAD_TRIGGER_R},
    {PAD_KEY_INVALID, PAD_BUTTON_UP},
    {PAD_KEY_INVALID, PAD_BUTTON_DOWN},
    {PAD_KEY_INVALID, PAD_BUTTON_LEFT},
    {PAD_KEY_INVALID, PAD_BUTTON_RIGHT},
}};

constexpr std::array<PADAxisMapping, PAD_AXIS_COUNT> g_defaultAxes{{
    {
        {SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_POSITIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_LEFT_X_POS,
    },
    {
        {SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_NEGATIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_LEFT_X_NEG,
    },
    // SDL's gamepad y-axis is inverted from GC's
    {
        {SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_NEGATIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_LEFT_Y_POS,
    },
    {
        {SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_POSITIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_LEFT_Y_NEG,
    },
    {
        {SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_POSITIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_RIGHT_X_POS,
    },
    {
        {SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_NEGATIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_RIGHT_X_NEG,
    },
    // see above
    {
        {SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_NEGATIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_RIGHT_Y_POS,
    },
    {
        {SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_POSITIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_RIGHT_Y_NEG,
    },
    {
        {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_SIGN_POSITIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_TRIGGER_L,
    },
    {
        {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_SIGN_POSITIVE},
        SDL_GAMEPAD_BUTTON_INVALID,
        PAD_AXIS_TRIGGER_R,
    },
}};

std::pair<binding::ControlId, float> axis_target(uint32_t axis) {
  const auto& c = controls();
  const binding::ControlId ids[]{
      c.leftX,
      c.leftX,
      c.leftY,
      c.leftY,
      c.rightX,
      c.rightX,
      c.rightY,
      c.rightY,
      c.triggerL,
      c.triggerR,
  };

  return axis < PAD_AXIS_COUNT ? std::pair{ids[axis], axis < 8 && axis % 2 ? -1.f : 1.f}
                               : std::pair{binding::kInvalidControlId, 0.f};
}

std::optional<PhysicalInput> key_input(int32_t code) {
  if (code > 0 && code < SDL_SCANCODE_COUNT) {
    return PhysicalInput{.control = PhysicalInput::Key{static_cast<SDL_Scancode>(code)}};
  }

  if (code >= PAD_KEY_MOUSE_X2 && code <= PAD_KEY_MOUSE_LEFT) {
    return PhysicalInput{.control = PhysicalInput::MouseButton{static_cast<uint8_t>(-code - 1)}};
  }

  return std::nullopt;
}

void add(Profile& profile, binding::ControlId target, std::optional<PhysicalInput> input, float scale = 1.f) {
  if (const auto* desc = binding::describe_control(target)) {
    auto& list = profile.bindings[desc->name];
    if (input) {
      list.push_back({.input = *input, .target = target, .scale = scale});
    }
  }
}

void convert(Profile& profile, const PADButtonMapping* buttons, const PADAxisMapping* axes, bool migrate) {
  for (size_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    const auto& b = buttons[i];
    if (profile.settings.emulateTriggers && b.nativeButton == PAD_NATIVE_BUTTON_INVALID &&
        (b.padButton == PAD_TRIGGER_L || b.padButton == PAD_TRIGGER_R)) {
      continue;
    }

    std::optional<PhysicalInput> input;
    if (b.nativeButton < SDL_GAMEPAD_BUTTON_COUNT) {
      input = PhysicalInput{
          .control = PhysicalInput::GamepadButton{static_cast<SDL_GamepadButton>(b.nativeButton)},
      };
    }

    if (migrate || input) {
      add(profile, control_for_button(b.padButton), input);
    }
  }

  for (size_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    const auto& a = axes[i];
    auto [target, scale] = axis_target(a.padAxis);
    std::optional<PhysicalInput> input;
    if (a.nativeAxis.nativeAxis >= 0 && a.nativeAxis.nativeAxis < SDL_GAMEPAD_AXIS_COUNT) {
      input = PhysicalInput{
          .control =
              PhysicalInput::GamepadAxis{
                  static_cast<SDL_GamepadAxis>(a.nativeAxis.nativeAxis),
                  a.nativeAxis.sign == AXIS_SIGN_NEGATIVE ? Direction::Negative : Direction::Positive,
              },
      };
    } else if (a.nativeButton >= 0 && a.nativeButton < SDL_GAMEPAD_BUTTON_COUNT) {
      input = PhysicalInput{
          .control = PhysicalInput::GamepadButton{static_cast<SDL_GamepadButton>(a.nativeButton)},
      };
    }

    add(profile, target, input, scale);
  }
}

template <typename T>
bool read(std::istream& stream, T& value) {
  return bool(stream.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

bool read_string(std::istream& stream, std::string& value) {
  uint32_t size;
  if (!read(stream, size) || size > 256) {
    return false;
  }

  value.resize(size);
  return bool(stream.read(value.data(), size));
}
} // namespace

std::vector<Binding> default_pad_bindings(input::SourceId source) {
  const auto it = gamepad::g_GameControllers.find(input::gamepad_for_source(source));
  if (it == gamepad::g_GameControllers.end()) {
    return {};
  }

  const auto& controller = it->second;
  const auto* buttons = &g_defaultButtonsStandard;
  switch (SDL_GetGamepadType(controller.m_controller)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
    buttons = &g_defaultButtonsXBox360;
    break;
  case SDL_GAMEPAD_TYPE_XBOXONE:
    buttons = &g_defaultButtonsXBox360;
    break;
  case SDL_GAMEPAD_TYPE_GAMECUBE:
    buttons = &g_defaultButtonsGamecube;
    break;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    buttons = controller.m_pid == 0x2073 ? &g_defaultButtonsNSOGamecube : &g_defaultButtonsProCon;
    break;
  default:
    break;
  }

  Profile p;
  p.settings.emulateTriggers = !(controller.m_isGameCube || controller.m_pid == 0x2073);
  convert(p, buttons->data(), g_defaultAxes.data(), false);
  std::vector<Binding> result;
  for (auto& [name, list] : p.bindings) {
    result.insert(result.end(), list.begin(), list.end());
  }

  const auto& c = controls();
  const SDL_GamepadButton extended[]{
      SDL_GAMEPAD_BUTTON_BACK,
      SDL_GAMEPAD_BUTTON_GUIDE,
      SDL_GAMEPAD_BUTTON_MISC1,
      SDL_GAMEPAD_BUTTON_MISC2,
      SDL_GAMEPAD_BUTTON_MISC3,
      SDL_GAMEPAD_BUTTON_MISC4,
      SDL_GAMEPAD_BUTTON_MISC5,
      SDL_GAMEPAD_BUTTON_MISC6,
      SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
      SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
      SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
      SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
      SDL_GAMEPAD_BUTTON_RIGHT_STICK,
      SDL_GAMEPAD_BUTTON_LEFT_STICK,
      SDL_GAMEPAD_BUTTON_TOUCHPAD,
  };

  for (size_t i = 0; i < c.ext.size(); ++i) {
    result.push_back({.input = {.control = PhysicalInput::GamepadButton{extended[i]}}, .target = c.ext[i]});
  }

  return result;
}

void migrate_legacy(const std::filesystem::path& path, std::vector<std::filesystem::path>& migrated,
                    LegacyMigration& report) {
  const auto portPath = path / "controller_ports.dat";
  {
    std::ifstream stream(portPath, std::ios::binary);
    uint32_t magic;
    uint32_t version;
    if (read(stream, magic) && read(stream, version) && magic == SBIG('CPRT') && (version == 2 || version == 3)) {
      std::array<PortPreference, 4> prefs;
      bool ok = true;
      for (size_t i = 0; i < prefs.size(); ++i) {
        uint8_t state;
        ok = ok && read(stream, state) && state <= 2 && read_string(stream, prefs[i].identity.guid) &&
             read_string(stream, prefs[i].identity.serial);
        if (!ok) {
          break;
        }

        prefs[i].state = state == 2 ? PortAssignment::Controller : i == 0 ? PortAssignment::Auto : PortAssignment::None;
        SDL_GetJoystickGUIDInfo(SDL_StringToGUID(prefs[i].identity.guid.c_str()),
                                &prefs[i].identity.vid,
                                &prefs[i].identity.pid,
                                nullptr,
                                nullptr);
        if (prefs[i].identity.vid == 0x057e && prefs[i].identity.pid == 0x0337) {
          prefs[i].identity.port = i;
        }
      }

      uint16_t low = 32768;
      uint16_t high = 32768;
      if (version == 3) {
        ok = ok && read(stream, low) && read(stream, high);
      }

      if (ok) {
        for (uint32_t i = 0; i < prefs.size(); ++i) {
          set_legacy_preference(i, prefs[i]);
          if (prefs[i].state == PortAssignment::Controller) {
            report.controllers[i] = prefs[i].identity;
          }
        }

        gamepad::set_device_rumble_intensity(low, high);
        migrated.push_back(portPath);
      }
    }
  }

  const auto keyPath = path / "keyboard_bindings.dat";
  {
    std::ifstream stream(keyPath, std::ios::binary);
    uint32_t magic;
    uint32_t version;
    if (read(stream, magic) && read(stream, version) && magic == SBIG('KBND') && version == 3) {
      stream.seekg(32);
      bool ok = true;
      std::array<Profile, 4> profiles;
      for (uint32_t port = 0; port < 4; ++port) {
        uint8_t active;
        std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> buttons;
        std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> axes;
        if (!read(stream, active) || !read(stream, buttons) || !read(stream, axes)) {
          ok = false;
          break;
        }

        report.keyboardActive[port] = active != 0;
        bool valid = true;
        for (size_t i = 0; i < buttons.size(); ++i) {
          valid &= buttons[i].padButton == kUnboundKeys[i].padButton;
        }

        for (size_t i = 0; i < axes.size(); ++i) {
          valid &= axes[i].padAxis == i;
        }

        if (!valid || (port != 0 && !active)) {
          continue;
        }

        for (const auto& b : buttons) {
          add(profiles[port], control_for_button(b.padButton), key_input(b.scancode));
        }

        for (const auto& a : axes) {
          auto [target, scale] = axis_target(a.padAxis);
          add(profiles[port], target, key_input(a.scancode), scale);
        }
      }

      if (ok) {
        for (uint32_t port = 0; port < 4; ++port) {
          for (auto& [name, list] : profiles[port].bindings) {
            set_binding(ProfileTarget::keyboard(port), name, std::move(list));
          }
        }

        migrated.push_back(keyPath);
      }
    }
  }

  std::error_code ec;
  for (const auto& file : std::filesystem::directory_iterator(path, ec)) {
    if (file.path().extension() != ".controller" || !file.is_regular_file()) {
      continue;
    }

    const auto stem = file.path().stem().string();
    const auto pidStart = stem.rfind('_');
    const auto vidStart = pidStart == std::string::npos ? std::string::npos : stem.rfind('_', pidStart - 1);
    if (vidStart == std::string::npos) {
      continue;
    }

    ControllerIdentity id;
    try {
      id.vid = static_cast<uint16_t>(std::stoul(stem.substr(vidStart + 1, pidStart - vidStart - 1), nullptr, 16));
      id.pid = static_cast<uint16_t>(std::stoul(stem.substr(pidStart + 1), nullptr, 16));
    } catch (...) {
      continue;
    }

    id.name = stem.substr(0, vidStart);
    std::ifstream stream(file.path(), std::ios::binary);
    uint32_t magic;
    uint32_t version;
    uint8_t isAdapter;
    if (!read(stream, magic) || !read(stream, version) || !read(stream, isAdapter) || magic != SBIG('CTRL') ||
        version < 3 || version > 4) {
      continue;
    }

    bool any = false;
    for (uint32_t port = 0; port < (isAdapter ? 4u : 1u); ++port) {
      constexpr size_t recordSize =
          sizeof(PADDeadZones) + sizeof(PADButtonMapping) * PAD_BUTTON_COUNT + sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
      stream.clear();
      stream.seekg(32 + port * recordSize);
      PADDeadZones zones{};
      std::array<unsigned char, sizeof(PADDeadZones)> zoneBytes{};
      std::array<PADButtonMapping, PAD_BUTTON_COUNT> buttons{};
      std::array<PADAxisMapping, PAD_AXIS_COUNT> axes{};
      if (!read(stream, zoneBytes) || !read(stream, buttons) || !read(stream, axes)) {
        continue;
      }

      if (zoneBytes[0] > 1 || zoneBytes[1] > 1) {
        continue;
      }

      std::memcpy(&zones, zoneBytes.data(), sizeof(zones));
      if (isAdapter && std::ranges::all_of(buttons, [](const auto& b) { return b.padButton == 0; })) {
        continue;
      }

      Profile profile;
      profile.identity = id;
      if (isAdapter) {
        profile.identity.port = port;
      }

      profile.settings.useDeadzones = zones.useDeadzones;
      profile.settings.emulateTriggers = zones.emulateTriggers;
      profile.settings.leftStickDeadZone = std::min(zones.stickDeadZone / 32767.f, 0.99f);
      profile.settings.rightStickDeadZone = std::min(zones.substickDeadZone / 32767.f, 0.99f);
      profile.settings.leftTriggerThreshold = std::min((zones.leftTriggerActivationZone + 1) / 32767.f, 1.f);
      profile.settings.rightTriggerThreshold = std::min((zones.rightTriggerActivationZone + 1) / 32767.f, 1.f);
      uint16_t low = 32767;
      uint16_t high = 32767;
      uint8_t force = 0;
      if (!isAdapter && (!read(stream, low) || !read(stream, high) || (version == 4 && !read(stream, force)))) {
        continue;
      }

      profile.settings.rumbleIntensityLow = low / 65535.f;
      profile.settings.rumbleIntensityHigh = high / 65535.f;
      profile.settings.forceDeviceRumble = force != 0;
      bool corrupt = false;
      for (uint32_t i = 0; i < axes.size(); ++i) {
        corrupt |= axes[i].padAxis != i;
      }

      if (corrupt) {
        axes = g_defaultAxes;
      }

      if (std::ranges::any_of(buttons, [](const auto& b) { return b.padButton == 0; })) {
        buttons = g_defaultButtonsStandard;
      }

      convert(profile, buttons.data(), axes.data(), true);
      add_legacy_controller(std::move(profile));
      any = true;
    }

    if (any) {
      migrated.push_back(file.path());
    }
  }
}
} // namespace aurora::pad::detail

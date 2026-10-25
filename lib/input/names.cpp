#include <aurora/binding.hpp>
#include <SDL3/SDL_keyboard.h>
#include <array>

namespace aurora::binding {

constexpr std::array kDefaultGamepadButtonNames{
    "South",
    "East",
    "West",
    "North",
    "Back",
    "Guide",
    "Start",
    "Left Stick",
    "Right Stick",
    "Left Shoulder",
    "Right Shoulder",
    "D-Pad Up",
    "D-Pad Down",
    "D-Pad Left",
    "D-Pad Right",
    "Misc Button 1",
    "Right Paddle 1",
    "Left Paddle 1",
    "Right Paddle 2",
    "Left Paddle 2",
    "Touchpad",
    "Misc Button 2",
    "Misc Button 3",
    "Misc Button 4",
    "Misc Button 5",
    "Misc Button 6",
};

SDL_GamepadType gamepad_type(SDL_Gamepad* gamepad) {
  if (gamepad) {
    return SDL_GetGamepadType(gamepad);
  }

  return SDL_GAMEPAD_TYPE_UNKNOWN;
}

std::string_view button_label(SDL_Gamepad* gamepad, SDL_GamepadButton nativeButton) {
  if (nativeButton < 0 || nativeButton >= SDL_GAMEPAD_BUTTON_COUNT) {
    return "Unbound";
  }

  const auto button = SDL_GamepadButton(nativeButton);
  if (gamepad) {
    switch (SDL_GetGamepadButtonLabel(gamepad, button)) {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
      return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B:
      return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X:
      return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
      return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
      return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
      return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
      return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
      return "Triangle";
    default:
      break;
    }
  }

  const auto type = gamepad_type(gamepad);
  switch (button) {
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK: {
    const bool left = button == SDL_GAMEPAD_BUTTON_LEFT_STICK;
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
      return left ? "L3" : "R3";
    case SDL_GAMEPAD_TYPE_GAMECUBE:
      return left ? "Control Stick" : "C Stick";
    default:
      break;
    }
    break;
  }
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: {
    const bool left = button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
      return left ? "L1" : "R1";
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
      return left ? "LB" : "RB";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
      return left ? "L" : "R";
    case SDL_GAMEPAD_TYPE_GAMECUBE:
      if (!left) {
        return "Z";
      }
      break;
    default:
      break;
    }
    break;
  }
  case SDL_GAMEPAD_BUTTON_BACK:
  case SDL_GAMEPAD_BUTTON_START: {
    const bool start = button == SDL_GAMEPAD_BUTTON_START;
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
      return start ? "Start" : "Select";
    case SDL_GAMEPAD_TYPE_PS4:
      return start ? "Options" : "Share";
    case SDL_GAMEPAD_TYPE_PS5:
      return start ? "Options" : "Create";
    case SDL_GAMEPAD_TYPE_XBOX360:
      return start ? "Start" : "Back";
    case SDL_GAMEPAD_TYPE_XBOXONE:
      return start ? "Menu" : "View";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
      return start ? "Plus" : "Minus";
    case SDL_GAMEPAD_TYPE_GAMECUBE:
      if (start) {
        return "Start/Pause";
      }
      break;
    default:
      break;
    }
    break;
  }
  case SDL_GAMEPAD_BUTTON_MISC3:
  case SDL_GAMEPAD_BUTTON_MISC4:
    if (type == SDL_GAMEPAD_TYPE_GAMECUBE) {
      return button == SDL_GAMEPAD_BUTTON_MISC3 ? "L" : "R";
    }
    break;
  default:
    break;
  }

  return kDefaultGamepadButtonNames[button];
}

std::string_view axis_label(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) {
  const auto type = gamepad_type(gamepad);
  const bool gamecube = type == SDL_GAMEPAD_TYPE_GAMECUBE;
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    return gamecube ? "Control Stick X" : "Left Stick X";
  case SDL_GAMEPAD_AXIS_LEFTY:
    return gamecube ? "Control Stick Y" : "Left Stick Y";
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return gamecube ? "C Stick X" : "Right Stick X";
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return gamecube ? "C Stick Y" : "Right Stick Y";
  case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
  case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: {
    const bool left = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
      return left ? "L2" : "R2";
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
      return left ? "LT" : "RT";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
      return left ? "ZL" : "ZR";
    case SDL_GAMEPAD_TYPE_GAMECUBE:
      return left ? "L" : "R";
    default:
      return left ? "Left Trigger" : "Right Trigger";
    }
  }
  default:
    return "Unknown Control";
  }
}

std::string input_label(const PhysicalInput& input, SDL_Gamepad* gamepad) {
  return input.control.match(
      [](PhysicalInput::Key key) { return std::string(SDL_GetScancodeName(key.scancode)); },
      [](PhysicalInput::MouseButton mouse) {
        constexpr const char* names[]{"Unbound", "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse X1", "Mouse X2"};
        return mouse.button < 6 ? std::string(names[mouse.button]) : "Mouse " + std::to_string(mouse.button);
      },
      [&](PhysicalInput::GamepadButton button) { return std::string(button_label(gamepad, button.button)); },
      [&](PhysicalInput::GamepadAxis axis) {
        std::string name(axis_label(gamepad, axis.axis));
        if (axis.direction != PhysicalInput::GamepadAxis::Direction::Full &&
            axis.axis < SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
          name += axis.direction == PhysicalInput::GamepadAxis::Direction::Negative ? "-" : "+";
        }

        return name;
      });
}

bool same_input(const PhysicalInput& a, const PhysicalInput& b) {
  return a.control.match(
      [&](PhysicalInput::Key k) {
        const auto* other = b.control.get_if<PhysicalInput::Key>();
        return other && k.scancode == other->scancode;
      },
      [&](PhysicalInput::MouseButton m) {
        const auto* other = b.control.get_if<PhysicalInput::MouseButton>();
        return other && m.button == other->button;
      },
      [&](PhysicalInput::GamepadButton g) {
        const auto* other = b.control.get_if<PhysicalInput::GamepadButton>();
        return other && g.button == other->button;
      },
      [&](PhysicalInput::GamepadAxis g) {
        const auto* other = b.control.get_if<PhysicalInput::GamepadAxis>();
        return other && g.axis == other->axis &&
               (g.direction == other->direction || g.direction == PhysicalInput::GamepadAxis::Direction::Full ||
                other->direction == PhysicalInput::GamepadAxis::Direction::Full);
      });
}
} // namespace aurora::binding

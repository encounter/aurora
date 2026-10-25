#pragma once

#include <aurora/binding.hpp>
#include <dolphin/pad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <filesystem>
#include <vector>

namespace aurora::pad {

struct Controls {
  // PADButton bits
  binding::ControlId a = binding::kInvalidControlId;
  binding::ControlId b = binding::kInvalidControlId;
  binding::ControlId x = binding::kInvalidControlId;
  binding::ControlId y = binding::kInvalidControlId;
  binding::ControlId z = binding::kInvalidControlId;
  binding::ControlId start = binding::kInvalidControlId;
  binding::ControlId l = binding::kInvalidControlId;
  binding::ControlId r = binding::kInvalidControlId;
  binding::ControlId up = binding::kInvalidControlId;
  binding::ControlId down = binding::kInvalidControlId;
  binding::ControlId left = binding::kInvalidControlId;
  binding::ControlId right = binding::kInvalidControlId;
  // [-1, 1], positive right and up
  binding::ControlId leftX = binding::kInvalidControlId;
  binding::ControlId leftY = binding::kInvalidControlId;
  binding::ControlId rightX = binding::kInvalidControlId;
  binding::ControlId rightY = binding::kInvalidControlId;
  // [0, 1]
  binding::ControlId triggerL = binding::kInvalidControlId;
  binding::ControlId triggerR = binding::kInvalidControlId;
  // PADExtButton bits
  std::array<binding::ControlId, PAD_EXT_BUTTON_COUNT> ext{};
};

[[nodiscard]] const Controls& controls();
[[nodiscard]] binding::ControlId control_for_button(PADButton button);
[[nodiscard]] binding::ControlId control_for_ext_button(PADExtButton button);

// Resolved controller, keyboard and mouse bindings for a port, and action bindings.
// A new set is published when any of those change, so compare pointers to notice.
[[nodiscard]] std::shared_ptr<const binding::BindingSet> binding_set(uint32_t port);

// Bindings that a controller would use if not assigned to a port. (Its own mapping or defaults)
[[nodiscard]] std::shared_ptr<const binding::BindingSet> controller_binding_set(input::SourceId controller);

// Extra per-port bindings for game-defined controls, e.g. "metaforce.open_menu".
void set_action_bindings(uint32_t port, std::vector<binding::Binding> bindings);

struct ControllerIdentity {
  std::string guid;
  std::string serial;
  uint16_t vid = 0;
  uint16_t pid = 0;
  std::string name;
  std::optional<uint32_t> port; // Four-port GameCube adapter identity/template.
  bool operator==(const ControllerIdentity&) const = default;
};

struct ProfileSettings {
  bool useDeadzones = true;
  float leftStickDeadZone = 8000.f / 32767.f;
  float rightStickDeadZone = 8000.f / 32767.f;
  bool emulateTriggers = true;
  float leftTriggerThreshold = 31151.f / 32767.f;
  float rightTriggerThreshold = 31151.f / 32767.f;
  float rumbleIntensityLow = 0.5f;
  float rumbleIntensityHigh = 0.5f;
  bool forceDeviceRumble = false;
  uint32_t wpadExtension = 1;
  bool operator==(const ProfileSettings&) const = default;
};

struct Profile {
  ControllerIdentity identity;
  ProfileSettings settings;
  // Missing controls inherit defaults; an empty list is explicitly unbound.
  std::map<std::string, std::vector<binding::Binding>, std::less<>> bindings;
};

struct ProfileTarget {
  enum class Kind { Keyboard, Controller };
  Kind kind = Kind::Keyboard;
  uint64_t id = 0;
  static ProfileTarget keyboard(uint32_t port) { return {Kind::Keyboard, port}; }
  static ProfileTarget controller(input::SourceId source) { return {Kind::Controller, source}; }
};

enum class PortAssignment { Auto, None, Controller };
struct PortPreference {
  PortAssignment state = PortAssignment::None;
  ControllerIdentity identity;
};

enum class ConflictPolicy { Unbind, Allow };

[[nodiscard]] std::optional<ControllerIdentity> controller_identity(input::SourceId source);
[[nodiscard]] const Profile* profile(ProfileTarget target);
[[nodiscard]] std::vector<binding::Binding> bindings_for_control(ProfileTarget target, binding::ControlId control);
[[nodiscard]] std::vector<binding::Binding> bindings_for_control(uint32_t port, binding::ControlId control);
bool set_binding(ProfileTarget target, std::string_view control, std::vector<binding::Binding> bindings);
bool set_binding(ProfileTarget target, binding::ControlId control, std::vector<binding::Binding> bindings);
bool set_settings(ProfileTarget target, ProfileSettings settings);
void reset_to_defaults(ProfileTarget target);
void clear_bindings(ProfileTarget target);
void set_default_keyboard_bindings(uint32_t port, std::vector<binding::Binding> bindings);
void set_default_controller_bindings(std::vector<binding::Binding> bindings);
[[nodiscard]] std::vector<binding::ControlId> find_conflicts(ProfileTarget target, binding::ControlId control,
                                                             const binding::PhysicalInput& input);
bool rebind(ProfileTarget target, binding::ControlId control, binding::Binding binding,
            ConflictPolicy policy = ConflictPolicy::Unbind);
bool rebind(ProfileTarget target, binding::ControlId control, binding::PhysicalInput input,
            ConflictPolicy policy = ConflictPolicy::Unbind);
[[nodiscard]] PortPreference port_preference(uint32_t port);
[[nodiscard]] input::SourceId port_controller(uint32_t port);
bool set_port_controller(uint32_t port, input::SourceId source);
void set_port_auto(uint32_t port);
void clear_port(uint32_t port);
[[nodiscard]] bool connected(uint32_t port);
[[nodiscard]] binding::State& gameplay_state(uint32_t port);

struct LegacyMigration {
  std::array<bool, 4> keyboardActive{};
  std::array<std::optional<ControllerIdentity>, 4> controllers{};
};
[[nodiscard]] const std::optional<LegacyMigration>& legacy_migration();
// Explicit paths also allow tools/tests to import and export profiles.
bool load_profiles(const std::filesystem::path& userPath);
bool save_profiles();

} // namespace aurora::pad

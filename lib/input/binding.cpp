#include <aurora/binding.hpp>

#include "source_state.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

namespace aurora::binding {
namespace {

using input::InputEvent;
using Cancelled = InputEvent::Cancelled;
using Reason = ControlChange::Reason;

struct Registry {
  // Deque keeps descriptor addresses stable across registrations.
  std::deque<ControlDescriptor> controls;
  std::unordered_map<std::string, ControlId> byName;
};

Registry& registry() {
  static Registry sRegistry;
  return sRegistry;
}

ControlKind kind_of(ControlId control) {
  const auto* descriptor = describe_control(control);
  return descriptor != nullptr ? descriptor->kind : ControlKind::Axis;
}

// The physical control an input or event refers to, independent of its source.
struct Address {
  enum class Type {
    None,
    Key,
    MouseButton,
    GamepadButton,
    GamepadAxis,
  };

  Type type = Type::None;
  uint32_t code = 0;

  static Address of(const PhysicalInput& input);
  // The control a press, release, axis sample, or targeted cancellation refers to.
  // None for payloads bindings do not map, and for whole-source cancellation.
  static Address of(const input::InputSource& source, const InputEvent& event);

  bool operator==(const Address&) const = default;
};

Address Address::of(const PhysicalInput& input) {
  return input.control.match(
      [](const PhysicalInput::Key& key) { return Address{Type::Key, static_cast<uint32_t>(key.scancode)}; },
      [](const PhysicalInput::MouseButton& button) { return Address{Type::MouseButton, button.button}; },
      [](const PhysicalInput::GamepadButton& button) {
        return Address{Type::GamepadButton, static_cast<uint32_t>(button.button)};
      },
      [](const PhysicalInput::GamepadAxis& axis) {
        return Address{Type::GamepadAxis, static_cast<uint32_t>(axis.axis)};
      });
}

Address Address::of(const input::InputSource& source, const InputEvent& event) {
  using Phase = InputEvent::PointerChanged::Phase;
  return event.payload.match(
      [](const InputEvent::KeyChanged& key) { return Address{Type::Key, static_cast<uint32_t>(key.scancode)}; },
      [](const InputEvent::ButtonChanged& button) {
        return Address{Type::GamepadButton, static_cast<uint32_t>(button.button)};
      },
      [](const InputEvent::AxisChanged& axis) { return Address{Type::GamepadAxis, static_cast<uint32_t>(axis.axis)}; },
      [&](const InputEvent::PointerChanged& pointer) {
        const bool click = source.kind == input::InputSource::Kind::Mouse && pointer.button != 0 &&
                           (pointer.phase == Phase::Down || pointer.phase == Phase::Up);
        return click ? Address{Type::MouseButton, pointer.button} : Address{};
      },
      [](const Cancelled& cancelled) {
        return cancelled.target.match(
            [](const Cancelled::Key& key) { return Address{Type::Key, static_cast<uint32_t>(key.scancode)}; },
            [](const Cancelled::Button& button) {
              return Address{Type::GamepadButton, static_cast<uint32_t>(button.button)};
            },
            [](const Cancelled::Axis& axis) { return Address{Type::GamepadAxis, static_cast<uint32_t>(axis.axis)}; },
            [](const auto&) { return Address{}; });
      },
      [](const auto&) { return Address{}; });
}

// Whether the binding's primary or held input is `address` on `source`. A None
// address matches any input from the source.
bool uses(const Binding& binding, input::SourceId source, const Address& address) {
  const auto matches = [&](const PhysicalInput& input) {
    return input.source == source && (address.type == Address::Type::None || Address::of(input) == address);
  };
  return matches(binding.input) ||
         std::ranges::any_of(binding.held, [&](const HeldInput& held) { return matches(held.input); });
}

} // namespace

ControlId register_control(const ControlDescriptor& desc) {
  if (desc.name.empty()) {
    return kInvalidControlId;
  }
  auto& reg = registry();
  if (const auto it = reg.byName.find(desc.name); it != reg.byName.end()) {
    return reg.controls[it->second - 1].kind == desc.kind ? it->second : kInvalidControlId;
  }
  reg.controls.push_back(desc);
  const ControlId id = reg.controls.size();
  reg.byName.emplace(desc.name, id);
  return id;
}

ControlId find_control(std::string_view name) {
  const auto& reg = registry();
  const auto it = reg.byName.find(std::string{name});
  return it != reg.byName.end() ? it->second : kInvalidControlId;
}

const ControlDescriptor* describe_control(ControlId control) {
  const auto& reg = registry();
  if (control == kInvalidControlId || control > reg.controls.size()) {
    return nullptr;
  }
  return &reg.controls[control - 1];
}

bool BindingSet::references(input::SourceId source) const {
  return std::ranges::any_of(bindings, [&](const Binding& binding) { return uses(binding, source, Address{}); });
}

struct State::Impl {
  struct Producer {
    std::string label;
    std::unordered_map<ControlId, float> values;

    [[nodiscard]] float value(ControlId control) const {
      const auto it = values.find(control);
      return it != values.end() ? it->second : 0.f;
    }
  };

  std::shared_ptr<const BindingSet> set;
  std::unordered_map<input::SourceId, input::SourceState> admitted;
  std::vector<float> contributions;            // Per binding in `set`.
  std::unordered_map<ControlId, float> values; // Nonzero aggregate values.
  std::map<ProducerId, Producer> producers;
  ProducerId nextProducer = 1;

  void assign(std::shared_ptr<const BindingSet> bindings) {
    set = std::move(bindings);
    contributions.assign(list().size(), 0.f);
  }

  [[nodiscard]] std::span<const Binding> list() const {
    return set != nullptr ? std::span<const Binding>{set->bindings} : std::span<const Binding>{};
  }

  [[nodiscard]] float input_value(const PhysicalInput& input) const {
    const auto it = admitted.find(input.source);
    if (it == admitted.end()) {
      return 0.f;
    }
    const auto& state = it->second;
    return input.control.match(
        [&](const PhysicalInput::Key& key) { return state.key(key.scancode) ? 1.f : 0.f; },
        [&](const PhysicalInput::MouseButton& button) { return state.mouse_button(button.button) ? 1.f : 0.f; },
        [&](const PhysicalInput::GamepadButton& button) { return state.button(button.button) ? 1.f : 0.f; },
        [&](const PhysicalInput::GamepadAxis& axis) { return axis.select(state.axis(axis.axis)); });
  }

  [[nodiscard]] float contribution(const Binding& binding) const {
    if (binding.target == kInvalidControlId || std::ranges::any_of(binding.held, [&](const HeldInput& held) {
          return input_value(held.input) < held.threshold;
        })) {
      return 0.f;
    }
    float value = input_value(binding.input);
    if (std::abs(value) <= binding.deadZone) {
      value = 0.f;
    }
    if (kind_of(binding.target) == ControlKind::Button) {
      return value >= binding.threshold && value != 0.f ? 1.f : 0.f;
    }
    return std::clamp(value * binding.scale, -1.f, 1.f);
  }

  [[nodiscard]] float aggregate(ControlId control) const {
    const auto bindings = list();
    const auto producerValues = producers | std::views::values |
                                std::views::transform([control](const Producer& p) { return p.value(control); });
    if (kind_of(control) == ControlKind::Button) {
      for (size_t i = 0; i < bindings.size(); ++i) {
        if (bindings[i].target == control && contributions[i] != 0.f) {
          return 1.f;
        }
      }
      return std::ranges::any_of(producerValues, [](float value) { return value >= 0.5f; }) ? 1.f : 0.f;
    }
    float sum = 0.f;
    for (size_t i = 0; i < bindings.size(); ++i) {
      if (bindings[i].target == control) {
        sum += contributions[i];
      }
    }
    for (const float value : producerValues) {
      sum += value;
    }
    return std::clamp(sum, -1.f, 1.f);
  }

  [[nodiscard]] float value(ControlId control) const {
    const auto it = values.find(control);
    return it != values.end() ? it->second : 0.f;
  }

  // Recomputes the controls' aggregate values and appends changes. A control
  // listed twice simply reports no change the second time.
  void refresh(const std::vector<ControlId>& controls, Reason reason, std::vector<ControlChange>& changes) {
    for (const auto control : controls) {
      const float previous = value(control);
      const float next = aggregate(control);
      if (next == previous) {
        continue;
      }
      if (next == 0.f) {
        values.erase(control);
      } else {
        values[control] = next;
      }
      changes.push_back({.control = control, .previousValue = previous, .value = next, .reason = reason});
    }
  }

  // Recomputes bindings that use `address` on `source` and appends changes.
  void update(input::SourceId source, const Address& address, Reason reason, std::vector<ControlChange>& changes) {
    const auto bindings = list();
    std::vector<ControlId> targets;
    for (size_t i = 0; i < bindings.size(); ++i) {
      if (!uses(bindings[i], source, address)) {
        continue;
      }
      if (const float next = contribution(bindings[i]); next != contributions[i]) {
        contributions[i] = next;
        targets.push_back(bindings[i].target);
      }
    }
    refresh(targets, reason, changes);
  }

  [[nodiscard]] std::vector<ControlId> targets(input::SourceId source, const Address& address) const {
    std::vector<ControlId> result;
    for (const auto& binding : list()) {
      if (binding.target != kInvalidControlId && uses(binding, source, address) &&
          std::ranges::find(result, binding.target) == result.end()) {
        result.push_back(binding.target);
      }
    }
    return result;
  }

  std::vector<ControlChange> cancel_all() {
    admitted.clear();
    std::ranges::fill(contributions, 0.f);
    for (auto& producer : producers | std::views::values) {
      producer.values.clear();
    }
    std::vector<ControlChange> changes;
    changes.reserve(values.size());
    for (const auto& [control, previous] : values) {
      changes.push_back({.control = control, .previousValue = previous, .value = 0.f, .reason = Reason::Cancelled});
    }
    values.clear();
    return changes;
  }

  std::vector<ControlChange> clear_producer(ProducerId id, bool remove) {
    std::vector<ControlChange> changes;
    const auto it = producers.find(id);
    if (it == producers.end()) {
      return changes;
    }
    const auto controls = std::views::keys(it->second.values);
    const std::vector<ControlId> targets{controls.begin(), controls.end()};
    if (remove) {
      producers.erase(it);
    } else {
      it->second.values.clear();
    }
    refresh(targets, remove ? Reason::Cancelled : Reason::Input, changes);
    return changes;
  }
};

State::State(std::shared_ptr<const BindingSet> bindings) : m_impl(std::make_unique<Impl>()) {
  m_impl->assign(std::move(bindings));
}

State::~State() = default;
State::State(State&&) noexcept = default;
State& State::operator=(State&&) noexcept = default;

MappingResult State::process(const input::InputSource& source, const input::InputEvent& event) {
  auto& impl = *m_impl;
  const bool cancelled = event.payload.is<Cancelled>();
  const Address address = Address::of(source, event);
  MappingResult result;
  if (!cancelled) {
    if (address.type == Address::Type::None) {
      return result;
    }
    result.targets = impl.targets(source.id, address);
  }
  impl.admitted[source.id].apply(source.kind, event);
  impl.update(source.id, address, cancelled ? Reason::Cancelled : Reason::Input, result.changes);
  return result;
}

float State::value(ControlId control) const { return m_impl->value(control); }

std::vector<ControlChange> State::set_bindings(std::shared_ptr<const BindingSet> bindings) {
  auto changes = m_impl->cancel_all();
  m_impl->assign(std::move(bindings));
  return changes;
}

std::vector<ControlChange> State::reset() { return m_impl->cancel_all(); }

const std::shared_ptr<const BindingSet>& State::bindings() const { return m_impl->set; }

ProducerId State::add_producer(std::string_view label) {
  const ProducerId id = m_impl->nextProducer++;
  m_impl->producers.emplace(id, Impl::Producer{.label = std::string{label}});
  return id;
}

std::vector<ControlChange> State::set_value(ProducerId producer, ControlId control, float value) {
  std::vector<ControlChange> changes;
  const auto it = m_impl->producers.find(producer);
  if (it == m_impl->producers.end() || control == kInvalidControlId) {
    return changes;
  }
  if (value == 0.f) {
    it->second.values.erase(control);
  } else {
    it->second.values[control] = value;
  }
  m_impl->refresh({control}, Reason::Input, changes);
  return changes;
}

std::vector<ControlChange> State::clear_producer(ProducerId producer) {
  return m_impl->clear_producer(producer, false);
}

std::vector<ControlChange> State::remove_producer(ProducerId producer) {
  return m_impl->clear_producer(producer, true);
}

} // namespace aurora::binding

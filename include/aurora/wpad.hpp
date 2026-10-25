#pragma once

#include <aurora/binding.hpp>
#include <aurora/wpad.h>
#include <array>
#include <memory>

namespace aurora::wpad {

struct Controls {
  binding::ControlId a;
  binding::ControlId b;
  binding::ControlId one;
  binding::ControlId two;
  binding::ControlId plus;
  binding::ControlId minus;
  binding::ControlId home;
  binding::ControlId up;
  binding::ControlId down;
  binding::ControlId left;
  binding::ControlId right;
  binding::ControlId irX;
  binding::ControlId irY;
  binding::ControlId recenter;
  binding::ControlId shake;

  struct Freestyle {
    binding::ControlId c;
    binding::ControlId z;
    binding::ControlId stickX;
    binding::ControlId stickY;
    binding::ControlId shake;
  } fs;

  struct Classic {
    binding::ControlId a;
    binding::ControlId b;
    binding::ControlId x;
    binding::ControlId y;
    binding::ControlId plus;
    binding::ControlId minus;
    binding::ControlId home;
    binding::ControlId l;
    binding::ControlId r;
    binding::ControlId zl;
    binding::ControlId zr;
    binding::ControlId up;
    binding::ControlId down;
    binding::ControlId left;
    binding::ControlId right;
    binding::ControlId leftX;
    binding::ControlId leftY;
    binding::ControlId rightX;
    binding::ControlId rightY;
    binding::ControlId triggerL;
    binding::ControlId triggerR;
  } cl;
};

[[nodiscard]] const Controls& controls();

struct ChannelSample {
  WPADStatus core{};
  WPADFSStatus fs{};
  WPADCLStatus cl{};
  AuroraWpadPointer pointer{};
  float distance = 1.f;
  bool synthesizedPointer = false;
};

// Providers run on the input thread. Native providers can supply calibrated raw reports.
class ChannelProvider {
public:
  virtual ~ChannelProvider() = default;
  virtual ChannelSample sample(uint32_t port, uint64_t timestampNs) = 0;
};

void set_channel_provider(uint32_t port, std::shared_ptr<ChannelProvider> provider);
void update();
void shutdown();

inline constexpr uint64_t kSampleIntervalNs = 5'000'000;
inline constexpr uint64_t kSensorResampleDelay = 25'000'000;
} // namespace aurora::wpad

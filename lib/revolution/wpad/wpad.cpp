#include "internal.hpp"

#include "../../input/port.hpp"

#include <dolphin/pad.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace aurora::wpad {
namespace {

struct Channel {
  u32 device = WPAD_DEV_FREESTYLE;
  bool deviceOverride = false;
  u32 format = WPAD_FMT_CORE;
  bool connected = false;
  bool disabled = false;
  bool dpd = false;
  bool notifyConnect = false;
  bool notifyExtension = false;
  WPADConnectCallback connect = nullptr;
  WPADExtensionCallback extension = nullptr;
  WPADSamplingCallback sampling = nullptr;
  void* buffer = nullptr;
  u32 count = 0;
  u32 index = 0;
  std::optional<AuroraWpadPointer> pointer;
  std::optional<Vec> coreAcc;
  std::optional<Vec> nunchukAcc;
  Sample sample{};
};

struct Completion {
  s32 chan;
  WPADCallback callback;
  s32 result;
  WPADInfo* info = nullptr;
};

std::array<Channel, WPAD_MAX_CONTROLLERS> channels;
std::vector<Completion> completions;
bool initialized = false;
bool acceptConnection = true;
bool motorEnabled = true;
u8 speakerVolume = 0;
uint64_t lastUpdateNs = 0;
uint64_t sampleTimeNs = 0;
uint64_t accumulatorNs = 0;
bool firstUpdate = true;
bool updating = false;
Observer observer = nullptr;
void (*resetObserver)() = nullptr;

bool valid(s32 chan) { return chan >= 0 && chan < WPAD_MAX_CONTROLLERS; }

bool fs_format(u32 format) { return format >= WPAD_FMT_FREESTYLE && format <= WPAD_FMT_FREESTYLE_ACC_DPD; }

bool cl_format(u32 format) { return format >= WPAD_FMT_CLASSIC && format <= WPAD_FMT_CLASSIC_ACC_DPD; }

bool acc_format(u32 format) {
  return format != WPAD_FMT_CORE && format != WPAD_FMT_FREESTYLE && format != WPAD_FMT_CLASSIC;
}

bool dpd_format(u32 format) {
  return format == WPAD_FMT_CORE_ACC_DPD || format == WPAD_FMT_FREESTYLE_ACC_DPD ||
         format == WPAD_FMT_CLASSIC_ACC_DPD || format == WPAD_FMT_CORE_ACC_DPD_FULL;
}

size_t report_size(u32 format) {
  if (fs_format(format)) {
    return sizeof(WPADFSStatus);
  }

  if (cl_format(format)) {
    return sizeof(WPADCLStatus);
  }

  if (format == WPAD_FMT_CORE_ACC_DPD_FULL) {
    return sizeof(WPADStatusEx);
  }

  return sizeof(WPADStatus);
}

void copy_report(const Channel& channel, void* dest) {
  const auto& sample = channel.sample;

  if (fs_format(channel.format)) {
    std::memcpy(dest, &sample.fs, sizeof(sample.fs));
  } else if (cl_format(channel.format)) {
    std::memcpy(dest, &sample.cl, sizeof(sample.cl));
  } else if (channel.format == WPAD_FMT_CORE_ACC_DPD_FULL) {
    WPADStatusEx extended{};
    std::memcpy(&extended, &sample.core, sizeof(sample.core));
    std::memcpy(dest, &extended, sizeof(extended));
  } else {
    std::memcpy(dest, &sample.core, sizeof(sample.core));
  }
}

s16 raw_acc(float value, float unit, float limit) {
  return static_cast<s16>(std::lround(std::clamp(std::isfinite(value) ? value * unit : 0.f, -limit, limit)));
}

void make_sample(Channel& channel, const ChannelSample& host, double time, bool blocked, bool captured) {
  auto& sample = channel.sample;
  sample = {};
  sample.time = time;
  sample.format = channel.format;
  sample.distance = host.distance;
  sample.synthesizedPointer = host.synthesizedPointer;
  const bool connected = channel.connected && !channel.disabled;
  auto& core = sample.core;
  core = host.core;
  core.dev = connected ? static_cast<u8>(channel.device) : WPAD_DEV_NOT_FOUND;
  core.err = connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
  if (!connected || blocked) {
    core.button = 0;
    core.accX = core.accY = 0;
    core.accZ = 106;
  } else if (channel.coreAcc && !captured) {
    core.accX = raw_acc(-channel.coreAcc->x, 106, 360);
    core.accY = raw_acc(channel.coreAcc->z, 106, 360);
    core.accZ = raw_acc(-channel.coreAcc->y, 106, 360);
  }

  if (!acc_format(channel.format)) {
    core.accX = core.accY = core.accZ = 0;
  }

  if (!channel.dpd || !dpd_format(channel.format) || !connected || blocked) {
    for (auto& object : core.obj) {
      object = {};
      object.traceId = 0xff;
    }
  } else {
    const bool overridePointer = channel.pointer && !captured;
    sample.pointer = overridePointer ? *channel.pointer : host.pointer;
    if (overridePointer) {
      sample.synthesizedPointer = true;
    }

    auto& ptr = sample.pointer;
    ptr.valid =
        ptr.valid && std::isfinite(ptr.x) && std::isfinite(ptr.y) && std::abs(ptr.x) <= 1.f && std::abs(ptr.y) <= 1.f;
    if (sample.synthesizedPointer) {
      for (auto& object : core.obj) {
        object = {};
        object.traceId = 0xff;
      }

      if (ptr.valid) {
        const auto x = static_cast<s16>((ptr.x + 1) * 511.5f);
        const auto y = static_cast<s16>((ptr.y + 1) * 383.5f);
        core.obj[0] = {static_cast<s16>(std::max(0, x - 32)), y, 8, 0};
        core.obj[1] = {static_cast<s16>(std::min(1023, x + 32)), y, 8, 1};
      }
    }
  }

  sample.fs = host.fs;
  sample.cl = host.cl;
  std::memcpy(&sample.fs, &core, sizeof(core));
  std::memcpy(&sample.cl, &core, sizeof(core));
  if (!connected || blocked) {
    sample.fs.fsAccX = sample.fs.fsAccY = 0;
    sample.fs.fsAccZ = 204;
    sample.fs.fsStickX = sample.fs.fsStickY = 0;
    sample.cl.clButton = 0;
    sample.cl.clLStickX = sample.cl.clLStickY = sample.cl.clRStickX = sample.cl.clRStickY = 0;
    sample.cl.clTriggerL = sample.cl.clTriggerR = 0;
  } else if (channel.nunchukAcc && !captured) {
    sample.fs.fsAccX = raw_acc(-channel.nunchukAcc->x, 204, 428);
    sample.fs.fsAccY = raw_acc(channel.nunchukAcc->z, 204, 428);
    sample.fs.fsAccZ = raw_acc(-channel.nunchukAcc->y, 204, 428);
  }
}

s32 result(s32 chan) {
  if (!valid(chan)) {
    return WPAD_ERR_INVALID;
  }

  if (initialized && channels[chan].connected && !channels[chan].disabled) {
    return WPAD_ERR_NONE;
  }

  return WPAD_ERR_NO_CONTROLLER;
}

s32 complete(s32 chan, s32 status, WPADCallback callback, WPADInfo* info = nullptr) {
  if (callback || info) {
    completions.push_back({chan, callback, status, info});
  }

  return status;
}

WPADInfo get_info(s32 chan) {
  float percent = -1;
  const auto power = PADGetBatteryState(chan, &percent);
  WPADInfo info{};
  const auto& channel = channels[chan];
  info.dpd = channel.dpd;
  info.attach = channel.device != WPAD_DEV_CORE;
  info.battery = WPAD_BATTERY_LEVEL_MAX;

  if (power == PAD_BATTERYSTATE_ON_BATTERY && std::isfinite(percent) && percent >= 0) {
    info.battery = static_cast<u8>(std::clamp(static_cast<int>(std::ceil(percent * 4)), 0, 4));
    info.lowBat = info.battery <= WPAD_BATTERY_LEVEL_LOW;
    info.nearempty = info.battery == WPAD_BATTERY_LEVEL_CRITICAL;
  }

  info.led = static_cast<u8>(1u << chan);

  return info;
}
} // namespace

void set_observer(Observer callback, void (*reset)()) {
  observer = callback;
  resetObserver = reset;
}
} // namespace aurora::wpad

using namespace aurora::wpad;

void WPADInit() {
  if (initialized) {
    return;
  }

  PADInit();
  initialized = true;
  firstUpdate = true;
  acceptConnection = motorEnabled = true;
  speakerVolume = 0;
  accumulatorNs = 0;
  initialize_provider();

  for (auto& channel : channels) {
    channel.sample.core.dev = WPAD_DEV_NOT_FOUND;
    channel.sample.core.err = WPAD_ERR_NO_CONTROLLER;
    std::memcpy(&channel.sample.fs, &channel.sample.core, sizeof(WPADStatus));
    std::memcpy(&channel.sample.cl, &channel.sample.core, sizeof(WPADStatus));
  }

  install_hooks(aurora_wpad_update, aurora_wpad_shutdown);
}

void aurora_wpad_update() { aurora::wpad::detail::update_at(timestamp()); }

void aurora::wpad::detail::update_at(uint64_t timestampNs) {
  if (!initialized || updating) {
    return;
  }

  updating = true;
  // Requests made by callbacks are completed on the next update.
  auto pending = std::exchange(completions, {});
  input::reconcile();
  uint32_t sampleCount = 0;
  if (firstUpdate) {
    firstUpdate = false;
    lastUpdateNs = sampleTimeNs = timestampNs;
    sampleCount = 1;
  } else {
    const uint64_t elapsed = timestampNs >= lastUpdateNs ? timestampNs - lastUpdateNs : 0;
    lastUpdateNs = timestampNs;
    accumulatorNs += std::min<uint64_t>(elapsed, 16 * kSampleIntervalNs);
    if (accumulatorNs > 16 * kSampleIntervalNs) {
      accumulatorNs = 16 * kSampleIntervalNs;
    }

    sampleCount = static_cast<uint32_t>(accumulatorNs / kSampleIntervalNs);
    accumulatorNs %= kSampleIntervalNs;
    sampleTimeNs = timestampNs - accumulatorNs - sampleCount * kSampleIntervalNs;
  }

  for (uint32_t tick = 0; tick < sampleCount && initialized; ++tick) {
    if (tick || sampleCount != 1 || sampleTimeNs != timestampNs) {
      sampleTimeNs += kSampleIntervalNs;
    }

    const double time = static_cast<double>(sampleTimeNs) / 1e9;

    for (s32 i = 0; i < WPAD_MAX_CONTROLLERS && initialized; ++i) {
      auto& channel = channels[i];
      const auto host = read_channel(i, sampleTimeNs);
      const bool present = host.core.err == WPAD_ERR_NONE;
      if (!channel.deviceOverride && channel.device != host.core.dev && host.core.dev <= WPAD_DEV_CLASSIC) {
        channel.device = host.core.dev;
        channel.notifyExtension = true;
      }

      if (!present) {
        channel.disabled = false;
      }

      const bool connected = present && !channel.disabled && (acceptConnection || channel.connected);
      const bool changed = connected != channel.connected;
      const bool extensionChanged = std::exchange(channel.notifyExtension, false);
      const bool notifyConnect = std::exchange(channel.notifyConnect, false);
      channel.connected = connected;

      if (observer && connected && (changed || extensionChanged)) {
        WPADSetDataFormat(i, channel.device * 3 + 2);
        channel.dpd = true;
      }

      make_sample(channel, host, time, pad::detail::blocked() || (!is_simulated_channel(i) && PADIsInputCaptured(i)),
                  PADIsInputCaptured(i));

      if (changed && !connected) {
        PADControlMotor(i, PAD_MOTOR_STOP);
      }

      if ((changed || notifyConnect) && channel.connect) {
        s32 reason = WPAD_ERR_NO_CONTROLLER;

        if (connected) {
          reason = WPAD_ERR_NONE;
        }

        channel.connect(i, reason);
      }

      if (!initialized) {
        break;
      }

      if (connected && (changed || extensionChanged || channel.notifyExtension) && channel.extension &&
          !channel.disabled) {
        channel.notifyExtension = false;
        channel.extension(i, channel.device);
      }

      if (!initialized) {
        break;
      }

      // Callbacks can change the format or disconnect the channel.
      make_sample(channel, host, time, pad::detail::blocked() || (!is_simulated_channel(i) && PADIsInputCaptured(i)),
                  PADIsInputCaptured(i));

      if (channel.connected || changed) {
        if (channel.buffer && channel.count) {
          copy_report(channel, static_cast<u8*>(channel.buffer) + channel.index * report_size(channel.format));
          channel.index = (channel.index + 1) % channel.count;
        }

        if (observer) {
          observer(i, channel.sample);
        }

        if (channel.sampling) {
          channel.sampling(i);
        }
      }
    }
  }

  for (auto& completion : pending) {
    if (!initialized) {
      break;
    }

    auto status = completion.result;

    if (status == WPAD_ERR_NONE) {
      status = result(completion.chan);
    }

    if (status == WPAD_ERR_NONE && completion.info) {
      *completion.info = get_info(completion.chan);
    }

    if (completion.callback) {
      completion.callback(completion.chan, status);
    }
  }

  updating = false;
}

void aurora_wpad_shutdown() {
  if (!initialized) {
    return;
  }

  for (s32 i = 0; i < WPAD_MAX_CONTROLLERS; ++i) {
    PADControlMotor(i, PAD_MOTOR_STOP);
  }

  initialized = false;
  channels = {};
  completions.clear();

  if (resetObserver) {
    resetObserver();
  }

  observer = nullptr;
  resetObserver = nullptr;
  install_hooks(nullptr, nullptr);
  shutdown_provider();
}

s32 WPADGetStatus() {
  if (initialized) {
    return WPAD_STATE_SETUP;
  }
  return WPAD_STATE_DISABLED;
}

s32 WPADProbe(s32 chan, u32* type) {
  const auto status = result(chan);
  if (type) {
    *type = WPAD_DEV_NOT_FOUND;
    if (status == WPAD_ERR_NONE) {
      *type = channels[chan].device;
    }
  }
  return status;
}

void WPADRead(s32 chan, void* status) {
  if (!status) {
    return;
  }

  if (!valid(chan)) {
    WPADStatus empty{};
    empty.dev = WPAD_DEV_NOT_FOUND;
    empty.err = WPAD_ERR_INVALID;
    std::memcpy(status, &empty, sizeof(empty));
    return;
  }

  if (!initialized) {
    auto empty = channels[chan];
    empty.sample.core = {};
    empty.sample.core.dev = WPAD_DEV_NOT_FOUND;
    empty.sample.core.err = WPAD_ERR_NO_CONTROLLER;
    std::memcpy(&empty.sample.fs, &empty.sample.core, sizeof(WPADStatus));
    std::memcpy(&empty.sample.cl, &empty.sample.core, sizeof(WPADStatus));
    copy_report(empty, status);
    return;
  }

  copy_report(channels[chan], status);
}

u32 WPADGetDataFormat(s32 chan) {
  if (!valid(chan)) {
    return WPAD_FMT_CORE;
  }
  return channels[chan].format;
}

s32 WPADSetDataFormat(s32 chan, u32 format) {
  if (!valid(chan) || format > WPAD_FMT_CORE_ACC_DPD_FULL) {
    return WPAD_ERR_INVALID;
  }
  if (const auto status = result(chan); status != WPAD_ERR_NONE) {
    return status;
  }

  auto& channel = channels[chan];
  if (channel.format != format) {
    channel.buffer = nullptr;
    channel.count = channel.index = 0;
    channel.format = format;
  }

  return WPAD_ERR_NONE;
}

void WPADSetAutoSamplingBuf(s32 chan, void* buffer, u32 count) {
  if (!valid(chan)) {
    return;
  }

  auto& channel = channels[chan];
  channel.buffer = buffer;
  channel.count = 0;
  if (buffer) {
    channel.count = count;
  }
  channel.index = 0;
}

WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback callback) {
  if (!valid(chan)) {
    return nullptr;
  }
  auto& channel = channels[chan];
  channel.notifyConnect = channel.connected && callback;
  return std::exchange(channel.connect, callback);
}

WPADExtensionCallback WPADSetExtensionCallback(s32 chan, WPADExtensionCallback callback) {
  if (!valid(chan)) {
    return nullptr;
  }
  auto& channel = channels[chan];
  channel.notifyExtension = channel.connected && callback;
  return std::exchange(channel.extension, callback);
}

WPADSamplingCallback WPADSetSamplingCallback(s32 chan, WPADSamplingCallback callback) {
  if (!valid(chan)) {
    return nullptr;
  }
  return std::exchange(channels[chan].sampling, callback);
}

void WPADDisconnect(s32 chan) {
  if (!valid(chan)) {
    return;
  }
  channels[chan].disabled = true;
  PADControlMotor(chan, PAD_MOTOR_STOP);
}

void aurora_wpad_reconnect(s32 chan) {
  if (valid(chan)) {
    channels[chan].disabled = false;
  }
}

BOOL aurora_wpad_set_device(s32 chan, u32 device) {
  if (!valid(chan) || device > WPAD_DEV_CLASSIC) {
    return FALSE;
  }

  auto& channel = channels[chan];
  channel.deviceOverride = true;
  if (channel.device != device) {
    channel.device = device;
    channel.notifyExtension = true;
  }

  return TRUE;
}

void aurora_wpad_set_pointer(s32 chan, const AuroraWpadPointer* pointer) {
  if (!valid(chan)) {
    return;
  }

  if (pointer) {
    channels[chan].pointer = *pointer;
  } else {
    channels[chan].pointer.reset();
  }
}

void aurora_wpad_set_acceleration(s32 chan, const Vec* core, const Vec* nunchuk) {
  if (!valid(chan)) {
    return;
  }

  if (core) {
    channels[chan].coreAcc = *core;
  } else {
    channels[chan].coreAcc.reset();
  }

  if (nunchuk) {
    channels[chan].nunchukAcc = *nunchuk;
  } else {
    channels[chan].nunchukAcc.reset();
  }
}

void WPADControlMotor(s32 chan, u32 command) {
  if (valid(chan) && (motorEnabled || command == WPAD_MOTOR_STOP) && command <= WPAD_MOTOR_RUMBLE &&
      (command == WPAD_MOTOR_STOP || result(chan) == WPAD_ERR_NONE)) {
    PADControlMotor(chan, command);
  }
}

s32 WPADGetInfoAsync(s32 chan, WPADInfo* info, WPADCallback callback) {
  s32 status = WPAD_ERR_INVALID;
  if (info) {
    status = result(chan);
  }
  return complete(chan, status, callback, info);
}

void WPADGetAccGravityUnit(s32 chan, u32 type, WPADAcc* acc) {
  if (!acc) {
    return;
  }
  *acc = {};
  if (valid(chan) && (type == WPAD_DEV_CORE || type == WPAD_DEV_FREESTYLE)) {
    const s16 unit = type == WPAD_DEV_CORE ? 106 : 204;
    *acc = {unit, unit, unit};
  }
}

BOOL WPADIsDpdEnabled(s32 chan) { return result(chan) == WPAD_ERR_NONE && channels[chan].dpd; }

s32 WPADControlDpd(s32 chan, u32 command, WPADCallback callback) {
  s32 status = WPAD_ERR_INVALID;
  if (command <= 3) {
    status = result(chan);
  }
  if (status == WPAD_ERR_NONE) {
    channels[chan].dpd = command != 0;
  }
  return complete(chan, status, callback);
}

u8 WPADGetDpdSensitivity() { return 3; }

u8 WPADGetSensorBarPosition() { return WPAD_SENSOR_BAR_POS_TOP; }

void WPADSetAutoSleepTime(u8) {}

void WPADRegisterAllocator(WPADAlloc, WPADFree) {}

u32 WPADGetWorkMemorySize() { return 0; }

BOOL WPADIsSpeakerEnabled(s32) { return FALSE; }

s32 WPADControlSpeaker(s32 chan, u32, WPADCallback callback) {
  auto status = result(chan);
  if (status == WPAD_ERR_NONE) {
    status = WPAD_ERR_INVALID;
  }
  return complete(chan, status, callback);
}

u8 WPADGetSpeakerVolume() { return speakerVolume; }

BOOL WPADCanSendStreamData(s32) { return FALSE; }

s32 WPADSendStreamData(s32 chan, void*, u16) {
  auto status = result(chan);
  if (status == WPAD_ERR_NONE) {
    return WPAD_ERR_INVALID;
  }
  return status;
}

BOOL WPADSetAcceptConnection(BOOL accept) { return std::exchange(acceptConnection, accept != FALSE); }

BOOL WPADGetAcceptConnection() { return acceptConnection; }

void WPADEnableMotor(BOOL enabled) {
  motorEnabled = enabled != FALSE;
  if (!motorEnabled) {
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; ++chan) {
      PADControlMotor(chan, PAD_MOTOR_STOP);
    }
  }
}

BOOL WPADIsMotorEnabled() { return motorEnabled; }

u8 WPADGetRadioSensitivity(s32 chan) { return result(chan) == WPAD_ERR_NONE ? 100 : 0; }

void WPADSetSpeakerVolume(u8 volume) { speakerVolume = volume; }

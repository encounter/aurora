#include "internal.hpp"

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
	std::optional<Vec> coreAcc, nunchukAcc;
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
bool updating = false;
Observer observer = nullptr;
void (*resetObserver)() = nullptr;

bool valid(s32 chan) {
	return chan >= 0 && chan < WPAD_MAX_CONTROLLERS;
}

bool fs_format(u32 format) {
	return format >= WPAD_FMT_FREESTYLE && format <= WPAD_FMT_FREESTYLE_ACC_DPD;
}

bool cl_format(u32 format) {
	return format >= WPAD_FMT_CLASSIC && format <= WPAD_FMT_CLASSIC_ACC_DPD;
}

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

s16 raw_acc(float value) {
	if (!std::isfinite(value)) {
		return 0;
	}

	return static_cast<s16>(std::lround(std::clamp(value * 256.f, -32768.f, 32767.f)));
}

u16 remote_buttons(const PADStatus& pad, bool nunchuk) {
	u16 result = pad.button & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN | PAD_BUTTON_UP);
	const std::pair<u32, u16> mapping[] = {{PAD_BUTTON_A, WPAD_BUTTON_A},
										   {PAD_BUTTON_B, WPAD_BUTTON_B},
										   {PAD_BUTTON_X, WPAD_BUTTON_1},
										   {PAD_BUTTON_Y, WPAD_BUTTON_2},
										   {PAD_BUTTON_START, WPAD_BUTTON_PLUS}};

	for (auto [from, to] : mapping) {
		if (pad.button & from) {
			result |= to;
		}
	}

	if (pad.extButton & PAD_BUTTON_BACK) {
		result |= WPAD_BUTTON_MINUS;
	}

	if (pad.extButton & PAD_BUTTON_GUIDE) {
		result |= WPAD_BUTTON_HOME;
	}

	if (nunchuk) {
		if (pad.button & PAD_TRIGGER_Z) {
			result |= WPAD_BUTTON_Z;
		}

		if (pad.button & PAD_TRIGGER_L) {
			result |= WPAD_BUTTON_C;
		}
	}

	return result;
}

u16 classic_buttons(const PADStatus& pad) {
	u16 result = 0;
	const std::pair<u32, u16> mapping[] = {
		{PAD_BUTTON_LEFT, WPAD_CL_BUTTON_LEFT},	 {PAD_BUTTON_RIGHT, WPAD_CL_BUTTON_RIGHT},
		{PAD_BUTTON_DOWN, WPAD_CL_BUTTON_DOWN},	 {PAD_BUTTON_UP, WPAD_CL_BUTTON_UP},
		{PAD_BUTTON_A, WPAD_CL_BUTTON_A},		 {PAD_BUTTON_B, WPAD_CL_BUTTON_B},
		{PAD_BUTTON_X, WPAD_CL_BUTTON_X},		 {PAD_BUTTON_Y, WPAD_CL_BUTTON_Y},
		{PAD_BUTTON_START, WPAD_CL_BUTTON_PLUS}, {PAD_TRIGGER_L, WPAD_CL_TRIGGER_L},
		{PAD_TRIGGER_R, WPAD_CL_TRIGGER_R},		 {PAD_TRIGGER_Z, WPAD_CL_TRIGGER_ZR}};

	for (auto [from, to] : mapping) {
		if (pad.button & from) {
			result |= to;
		}
	}

	if (pad.extButton & PAD_BUTTON_BACK) {
		result |= WPAD_CL_BUTTON_MINUS;
	}

	if (pad.extButton & PAD_BUTTON_GUIDE) {
		result |= WPAD_CL_BUTTON_HOME;
	}

	if (pad.extButton & PAD_BUTTON_LEFT_STICK) {
		result |= WPAD_CL_TRIGGER_ZL;
	}

	return result;
}

void make_sample(Channel& channel, const HostState& host, double time) {
	auto& sample = channel.sample;
	sample = {};
	sample.format = channel.format;
	sample.time = time;
	auto& core = sample.core;
	const bool connected = channel.connected && !channel.disabled;
	core.dev = WPAD_DEV_NOT_FOUND;
	core.err = WPAD_ERR_NO_CONTROLLER;

	if (connected) {
		core.dev = static_cast<u8>(channel.device);
		core.err = WPAD_ERR_NONE;
	}

	for (auto& object : core.obj) {
		object.traceId = 0xff;
	}

	if (connected) {
		const bool nunchuk = channel.device == WPAD_DEV_FREESTYLE && fs_format(channel.format);
		Vec accel{0, -1, 0};

		if (!host.blocked) {
			core.button = remote_buttons(host.pad, nunchuk);
			accel = channel.coreAcc.value_or(host.coreAcc);
		}

		if (acc_format(channel.format)) {
			core.accX = raw_acc(-accel.x);
			core.accY = raw_acc(accel.z);
			core.accZ = raw_acc(-accel.y);
		}

		if (channel.dpd && dpd_format(channel.format) && !host.blocked) {
			sample.pointer = channel.pointer.value_or(host.pointer);
			auto& p = sample.pointer;
			p.valid =
				p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1.f && std::abs(p.y) <= 1.f;

			if (p.valid) {
				const auto x = static_cast<s16>((p.x + 1) * 511.5f);
				const auto y = static_cast<s16>((p.y + 1) * 383.5f);
				core.obj[0] = {static_cast<s16>(std::max(0, x - 32)), y, 8, 0};
				core.obj[1] = {static_cast<s16>(std::min(1023, x + 32)), y, 8, 1};
			}
		}
	}

	std::memcpy(&sample.fs, &core, sizeof(core));
	std::memcpy(&sample.cl, &core, sizeof(core));

	if (!connected) {
		return;
	}

	if (host.blocked) {
		if (channel.device == WPAD_DEV_FREESTYLE && acc_format(channel.format)) {
			sample.fs.fsAccZ = 256;
		}

		return;
	}

	if (channel.device == WPAD_DEV_FREESTYLE && fs_format(channel.format)) {
		sample.fs.fsStickX = static_cast<s8>(std::clamp<int>(host.pad.stickX, -127, 127) * 71 / 127);
		sample.fs.fsStickY = static_cast<s8>(std::clamp<int>(host.pad.stickY, -127, 127) * 71 / 127);

		if (acc_format(channel.format)) {
			const auto accel = channel.nunchukAcc.value_or(host.nunchukAcc);
			sample.fs.fsAccX = raw_acc(-accel.x);
			sample.fs.fsAccY = raw_acc(accel.z);
			sample.fs.fsAccZ = raw_acc(-accel.y);
		}
	} else if (channel.device == WPAD_DEV_CLASSIC && cl_format(channel.format)) {
		sample.cl.clButton = classic_buttons(host.pad);
		sample.cl.clLStickX = static_cast<s16>(std::clamp<int>(host.pad.stickX, -127, 127) * 308 / 127);
		sample.cl.clLStickY = static_cast<s16>(std::clamp<int>(host.pad.stickY, -127, 127) * 308 / 127);
		sample.cl.clRStickX = static_cast<s16>(std::clamp<int>(host.pad.substickX, -127, 127) * 308 / 127);
		sample.cl.clRStickY = static_cast<s16>(std::clamp<int>(host.pad.substickY, -127, 127) * 308 / 127);
		sample.cl.clTriggerL = static_cast<u8>(host.pad.triggerLeft * 180 / 255);
		sample.cl.clTriggerR = static_cast<u8>(host.pad.triggerRight * 180 / 255);
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

	for (auto& channel : channels) {
		channel.sample.core.dev = WPAD_DEV_NOT_FOUND;
		channel.sample.core.err = WPAD_ERR_NO_CONTROLLER;
		std::memcpy(&channel.sample.fs, &channel.sample.core, sizeof(WPADStatus));
		std::memcpy(&channel.sample.cl, &channel.sample.core, sizeof(WPADStatus));
	}

	install_hooks(aurora_wpad_update, aurora_wpad_shutdown);
}

void aurora_wpad_update() {
	if (!initialized || updating) {
		return;
	}

	updating = true;
	// Requests made by callbacks are completed on the next update.
	auto pending = std::exchange(completions, {});
	const auto host = read_host();
	const double time = now();

	for (s32 i = 0; i < WPAD_MAX_CONTROLLERS && initialized; ++i) {
		auto& channel = channels[i];
		const bool present = host[i].pad.err == PAD_ERR_NONE;

		if (!present) {
			channel.disabled = false;
		}

		const bool connected = present && !channel.disabled;
		const bool changed = connected != channel.connected;
		const bool extensionChanged = std::exchange(channel.notifyExtension, false);
		const bool notifyConnect = std::exchange(channel.notifyConnect, false);
		channel.connected = connected;

		if (observer && connected && (changed || extensionChanged)) {
			WPADSetDataFormat(i, channel.device * 3 + 2);
			channel.dpd = true;
		}

		make_sample(channel, host[i], time);

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
		make_sample(channel, host[i], time);

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
		// Buffer stride belongs to the format under which it was registered.
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
	if (valid(chan) && command <= WPAD_MOTOR_RUMBLE && (command == WPAD_MOTOR_STOP || result(chan) == WPAD_ERR_NONE)) {
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
		*acc = {256, 256, 256};
	}
}

BOOL WPADIsDpdEnabled(s32 chan) {
	return result(chan) == WPAD_ERR_NONE && channels[chan].dpd;
}

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

u8 WPADGetDpdSensitivity() {
	return 3;
}

u8 WPADGetSensorBarPosition() {
	return WPAD_SENSOR_BAR_POS_TOP;
}

void WPADSetAutoSleepTime(u8) {
}

void WPADRegisterAllocator(WPADAlloc, WPADFree) {
}

u32 WPADGetWorkMemorySize() {
	return 0;
}

BOOL WPADIsSpeakerEnabled(s32) {
	return FALSE;
}

s32 WPADControlSpeaker(s32 chan, u32, WPADCallback callback) {
	auto status = result(chan);

	if (status == WPAD_ERR_NONE) {
		status = WPAD_ERR_INVALID;
	}

	return complete(chan, status, callback);
}

u8 WPADGetSpeakerVolume() {
	return 0;
}

BOOL WPADCanSendStreamData(s32) {
	return FALSE;
}

s32 WPADSendStreamData(s32 chan, void*, u16) {
	auto status = result(chan);

	if (status == WPAD_ERR_NONE) {
		return WPAD_ERR_INVALID;
	}

	return status;
}

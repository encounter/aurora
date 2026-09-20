#include <revolution/kpad.h>
#include "wpad/internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr u32 Capacity = 120;

struct Filter {
	float radius = 0;
	float sensitivity = 1;
};

struct Channel {
	std::array<KPADStatus, Capacity> samples{};
	KPADStatus previous{};
	u32 index = 0, count = 0;
	float delay = 0, pulse = 0, sensorHeight = 0;
	double nextRepeat = 0, nextClassicRepeat = 0;
	Filter pos, horizon, dist;
	bool hasPrevious = false;
};

std::array<Channel, WPAD_MAX_CONTROLLERS> channels;
bool initialized = false;

bool valid(s32 chan) {
	return chan >= 0 && chan < WPAD_MAX_CONTROLLERS;
}

float length(Vec v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec difference(Vec a, Vec b) {
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec acceleration(s16 x, s16 y, s16 z) {
	return {-x / 256.f, -z / 256.f, y / 256.f};
}

Vec2 stick(s16 x, s16 y, float range) {
	Vec2 value{std::clamp(x / range, -1.f, 1.f), std::clamp(y / range, -1.f, 1.f)};
	const float magnitude = std::hypot(value.x, value.y);

	if (magnitude > 1) {
		value.x /= magnitude;
		value.y /= magnitude;
	}

	return value;
}

float filtered(float old, float value, Filter filter) {
	const auto delta = value - old;
	float gain = filter.sensitivity;

	if (filter.radius > 0 && std::abs(delta) < filter.radius) {
		const float fraction = delta / filter.radius;
		gain *= fraction * fraction * fraction * fraction;
	}

	return old + gain * delta;
}

void buttons(u32 raw, u32 old, u32& hold, u32& trig, u32& release, Channel& channel, double time, double& nextRepeat) {
	old &= KPAD_BUTTON_MASK;
	hold = raw;
	trig = raw & ~old;
	release = old & ~raw;

	if (trig || release) {
		nextRepeat = time + channel.delay;

		if (trig && channel.pulse > 0) {
			hold |= KPAD_BUTTON_RPT;
		}
	} else if (raw && channel.pulse > 0 && time >= nextRepeat) {
		hold |= KPAD_BUTTON_RPT;
		nextRepeat += (std::floor((time - nextRepeat) / channel.pulse) + 1) * channel.pulse;
	}
}

void sample(s32 chan, const aurora::wpad::Sample& raw) {
	auto& channel = channels[chan];
	const auto& old = channel.previous;
	KPADStatus status{};
	status.dev_type = raw.core.dev;
	status.wpad_err = raw.core.err;
	status.data_format = static_cast<u8>(raw.format);
	const bool connected = raw.core.err == WPAD_ERR_NONE;
	const bool previous = channel.hasPrevious && old.wpad_err == WPAD_ERR_NONE;
	u32 heldButtons = 0;
	u32 previousButtons = 0;

	if (connected) {
		heldButtons = raw.core.button;
	}

	if (previous) {
		previousButtons = old.hold;
	}

	buttons(heldButtons, previousButtons, status.hold, status.trig, status.release, channel, raw.time,
			channel.nextRepeat);

	if (connected) {
		status.acc = acceleration(raw.core.accX, raw.core.accY, raw.core.accZ);
		status.acc_value = length(status.acc);

		if (previous) {
			status.acc_speed = length(difference(status.acc, old.acc));
		}

		const float vertical = std::hypot(status.acc.y, status.acc.z);

		if (vertical > 0) {
			status.acc_vertical = {-status.acc.z / vertical, -status.acc.y / vertical};
		}

		const float roll = std::hypot(status.acc.x, status.acc.y);
		Vec2 horizon{1, 0};

		if (roll > 0) {
			horizon = {-status.acc.y / roll, status.acc.x / roll};
		}

		status.horizon = horizon;

		if (previous) {
			status.horizon = {filtered(old.horizon.x, horizon.x, channel.horizon),
							  filtered(old.horizon.y, horizon.y, channel.horizon)};
			status.hori_vec = {status.horizon.x - old.horizon.x, status.horizon.y - old.horizon.y};
			status.hori_speed = std::hypot(status.hori_vec.x, status.hori_vec.y);
		}

		if (raw.pointer.valid) {
			const Vec2 pos{raw.pointer.x, raw.pointer.y + channel.sensorHeight};
			const bool tracked = previous && old.dpd_valid_fg;
			status.pos = pos;
			status.dist = 1.f;

			if (tracked) {
				status.pos = {filtered(old.pos.x, pos.x, channel.pos), filtered(old.pos.y, pos.y, channel.pos)};
				status.dist = filtered(old.dist, 1.f, channel.dist);
			}

			status.dpd_valid_fg = 2;

			if (tracked) {
				status.vec = {status.pos.x - old.pos.x, status.pos.y - old.pos.y};
				status.speed = std::hypot(status.vec.x, status.vec.y);
				status.dist_vec = status.dist - old.dist;
				status.dist_speed = std::abs(status.dist_vec);
			}
		}

		if (raw.core.dev == WPAD_DEV_FREESTYLE && raw.format >= WPAD_FMT_FREESTYLE &&
			raw.format <= WPAD_FMT_FREESTYLE_ACC_DPD) {
			auto& fs = status.ex_status.fs;
			fs.stick = stick(raw.fs.fsStickX, raw.fs.fsStickY, 71.f);
			fs.acc = acceleration(raw.fs.fsAccX, raw.fs.fsAccY, raw.fs.fsAccZ);
			fs.acc_value = length(fs.acc);

			if (previous && old.dev_type == WPAD_DEV_FREESTYLE) {
				fs.acc_speed = length(difference(fs.acc, old.ex_status.fs.acc));
			}
		} else if (raw.core.dev == WPAD_DEV_CLASSIC && raw.format >= WPAD_FMT_CLASSIC &&
				   raw.format <= WPAD_FMT_CLASSIC_ACC_DPD) {
			auto& cl = status.ex_status.cl;
			u32 oldHold = 0;

			if (previous && old.dev_type == WPAD_DEV_CLASSIC) {
				oldHold = old.ex_status.cl.hold;
			}

			buttons(raw.cl.clButton, oldHold, cl.hold, cl.trig, cl.release, channel, raw.time,
					channel.nextClassicRepeat);
			cl.lstick = stick(raw.cl.clLStickX, raw.cl.clLStickY, 308.f);
			cl.rstick = stick(raw.cl.clRStickX, raw.cl.clRStickY, 308.f);
			cl.ltrigger = raw.cl.clTriggerL / 180.f;
			cl.rtrigger = raw.cl.clTriggerR / 180.f;
		}
	}

	channel.samples[channel.index] = status;
	channel.index = (channel.index + 1) % Capacity;
	channel.count = std::min(channel.count + 1, Capacity);
	channel.previous = status;
	channel.hasPrevious = true;
}

void shutdown() {
	channels = {};
	initialized = false;
}

void set_filter(Filter& filter, float radius, float sensitivity) {
	if (std::isfinite(radius) && std::isfinite(sensitivity)) {
		filter = {std::max(radius, 0.f), std::clamp(sensitivity, 0.f, 1.f)};
	}
}
} // namespace

void KPADInit() {
	if (initialized) {
		return;
	}

	WPADInit();
	initialized = true;
	aurora::wpad::set_observer(sample, shutdown);

	for (s32 i = 0; i < WPAD_MAX_CONTROLLERS; ++i) {
		u32 device;

		if (WPADProbe(i, &device) == WPAD_ERR_NONE) {
			WPADSetDataFormat(i, device * 3 + 2);
			WPADControlDpd(i, 3, nullptr);
		}
	}
}

void KPADReset() {
	for (auto& channel : channels) {
		channel.count = channel.index = 0;
		channel.previous = {};
		channel.hasPrevious = false;
		channel.nextRepeat = channel.nextClassicRepeat = 0;
	}
}

s32 KPADRead(s32 chan, KPADStatus* samples, u32 count) {
	if (!initialized || !valid(chan) || !samples || !count) {
		return 0;
	}

	auto& channel = channels[chan];
	const auto copied = std::min(channel.count, count);

	for (u32 i = 0; i < copied; ++i) {
		samples[i] = channel.samples[(channel.index + Capacity - 1 - i) % Capacity];
	}

	channel.count = 0;

	return static_cast<s32>(copied);
}

void KPADSetBtnRepeat(s32 chan, f32 delay, f32 pulse) {
	if (!valid(chan) || !std::isfinite(delay) || !std::isfinite(pulse)) {
		return;
	}

	auto& channel = channels[chan];
	channel.delay = std::max(delay, 0.f);
	channel.pulse = std::max(pulse, 0.f);
	channel.nextRepeat = channel.nextClassicRepeat = aurora::wpad::now() + channel.delay;
}

void KPADSetSensorHeight(s32 chan, f32 height) {
	if (valid(chan) && std::isfinite(height)) {
		channels[chan].sensorHeight = height;
	}
}

void KPADSetPosParam(s32 chan, f32 radius, f32 sensitivity) {
	if (valid(chan)) {
		set_filter(channels[chan].pos, radius, sensitivity);
	}
}

void KPADSetHoriParam(s32 chan, f32 radius, f32 sensitivity) {
	if (valid(chan)) {
		set_filter(channels[chan].horizon, radius, sensitivity);
	}
}

void KPADSetDistParam(s32 chan, f32 radius, f32 sensitivity) {
	if (valid(chan)) {
		set_filter(channels[chan].dist, radius, sensitivity);
	}
}

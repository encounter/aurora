#pragma once

#include <aurora/wpad.hpp>
#include <aurora/aurora.h>
#include <aurora/input.hpp>

namespace aurora::wpad {

void initialize_provider();
void shutdown_provider();
bool is_simulated_channel(uint32_t port);
ChannelSample read_channel(uint32_t port, uint64_t timestampNs);
void install_hooks(void (*update)(), void (*shutdown)());
double now();
uint64_t timestamp();
float sensor_bar_distance();

namespace detail {
void update_at(uint64_t timestampNs);
AuroraWpadPointer map_window_pointer(SDL_FPoint position, const AuroraWindowSize& size);
void set_clock(uint64_t (*clock)());
} // namespace detail

} // namespace aurora::wpad

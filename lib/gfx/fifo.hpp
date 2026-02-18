#pragma once

#include <dolphin/gx.h>
#include <cstdint>

namespace aurora::gfx::fifo {

// Initialize the FIFO subsystem
void init();

// Write primitives to the current FIFO target (internal buffer or display list buffer)
void write_u8(u8 val);
void write_u16(u16 val);
void write_u32(u32 val);
void write_f32(f32 val);

// Display list recording: redirect FIFO writes to a user-provided buffer
void begin_display_list(u8* buf, u32 size);
// End display list recording: restore internal FIFO, return ROUNDUP32(bytes written)
u32 end_display_list();
// Returns true if currently recording a display list
bool in_display_list();

// Drain the internal FIFO buffer through the command processor
void drain();

// Process external data (e.g. display list playback) through the command processor
void process(const u8* data, u32 size, bool bigEndian = true);

// Internal buffer inspection (useful for testing and debug)
const u8* get_buffer_data();
u32 get_buffer_size();
void clear_buffer();

} // namespace aurora::gfx::fifo

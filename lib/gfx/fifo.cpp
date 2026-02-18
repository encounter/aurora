#include "fifo.hpp"
#include "command_processor.hpp"
#include "../internal.hpp"

#include <cstring>
#include <vector>

static aurora::Module Log("aurora::gfx::fifo");

namespace aurora::gfx::fifo {

// Internal FIFO buffer for inline rendering
static std::vector<u8> sInternalBuffer;

// Display list recording state
static u8* sDlBuffer = nullptr;
static u32 sDlSize = 0;
static u32 sDlWritePos = 0;
static bool sInDisplayList = false;

// Current write target
static inline void write_byte(u8 val) {
  if (sInDisplayList) {
    if (sDlWritePos < sDlSize) {
      sDlBuffer[sDlWritePos++] = val;
    }
  } else {
    sInternalBuffer.push_back(val);
  }
}

void init() {
  sInternalBuffer.reserve(64 * 1024); // 64KB initial capacity
  sInDisplayList = false;
  sDlBuffer = nullptr;
  sDlSize = 0;
  sDlWritePos = 0;
}

void write_u8(u8 val) { write_byte(val); }

void write_u16(u16 val) {
  // Big-endian byte order (matching GX hardware)
  write_byte(static_cast<u8>(val >> 8));
  write_byte(static_cast<u8>(val & 0xFF));
}

void write_u32(u32 val) {
  write_byte(static_cast<u8>((val >> 24) & 0xFF));
  write_byte(static_cast<u8>((val >> 16) & 0xFF));
  write_byte(static_cast<u8>((val >> 8) & 0xFF));
  write_byte(static_cast<u8>(val & 0xFF));
}

void write_f32(f32 val) {
  u32 bits;
  std::memcpy(&bits, &val, sizeof(bits));
  write_u32(bits);
}

void begin_display_list(u8* buf, u32 size) {
  sInDisplayList = true;
  sDlBuffer = buf;
  sDlSize = size;
  sDlWritePos = 0;
}

u32 end_display_list() {
  sInDisplayList = false;
  u32 bytesWritten = sDlWritePos;
  // Pad to 32-byte alignment
  u32 padded = (bytesWritten + 31) & ~31u;
  // Zero-fill padding
  while (sDlWritePos < padded && sDlWritePos < sDlSize) {
    sDlBuffer[sDlWritePos++] = 0;
  }
  sDlBuffer = nullptr;
  sDlSize = 0;
  sDlWritePos = 0;
  return padded;
}

bool in_display_list() { return sInDisplayList; }

void drain() {
  if (sInternalBuffer.empty()) {
    return;
  }
  // Process the internal buffer through the command processor
  command_processor::process(sInternalBuffer.data(), static_cast<u32>(sInternalBuffer.size()), true);
  sInternalBuffer.clear();
}

void process(const u8* data, u32 size, bool bigEndian) {
  command_processor::process(data, size, bigEndian);
}

const u8* get_buffer_data() { return sInternalBuffer.data(); }
u32 get_buffer_size() { return static_cast<u32>(sInternalBuffer.size()); }
void clear_buffer() { sInternalBuffer.clear(); }

} // namespace aurora::gfx::fifo

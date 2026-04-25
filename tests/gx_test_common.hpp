#pragma once

#include <gtest/gtest.h>

#include <dolphin/gx.h>

#include "gx/gx.hpp"
#include "gx/fifo.hpp"
#include "gx/command_processor.hpp"
#include "gfx/depth_peek.hpp"

#include <cstring>
#include <vector>

// Forward declare dirty state flush
extern "C" void __GXSetDirtyState();

class GXFifoTest : public ::testing::Test {
protected:
  void SetUp() override {
    GXInit(nullptr, 0);
    aurora::gx::fifo::clear_buffer();
    aurora::gx::g_gxState = aurora::gx::GXState{};
    aurora::gfx::depth_peek::testing::reset();
  }

  // Copy the internal FIFO buffer contents and clear it
  std::vector<u8> capture_fifo() {
    auto size = aurora::gx::fifo::get_buffer_size();
    auto* data = aurora::gx::fifo::get_buffer_data();
    std::vector<u8> bytes(data, data + size);
    aurora::gx::fifo::clear_buffer();
    return bytes;
  }

  // Reset g_gxState to default-constructed state
  void reset_gx_state() { aurora::gx::g_gxState = aurora::gx::GXState{}; }

  // Decode a captured FIFO byte stream through the command processor
  void decode_fifo(const std::vector<u8>& bytes) {
    aurora::gx::fifo::process(bytes.data(), static_cast<u32>(bytes.size()), true);
  }

  // Flush dirty state, then capture the FIFO buffer
  std::vector<u8> flush_and_capture() {
    __GXSetDirtyState();
    return capture_fifo();
  }

  // Convenience reference to g_gxState
  aurora::gx::GXState& gxState() { return aurora::gx::g_gxState; }
};

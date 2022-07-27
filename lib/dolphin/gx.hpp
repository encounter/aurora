#pragma once

#include "../internal.hpp"
#include "../gfx/gx.hpp"

static aurora::Module Log("aurora::gx");

using aurora::gfx::gx::g_gxState;

template <typename T>
static inline void update_gx_state(T& val, T newVal) {
  if (val != newVal) {
    val = std::move(newVal);
    g_gxState.stateDirty = true;
  }
}

static inline aurora::Vec4<float> from_gx_color(GXColor color) {
  return {
      static_cast<float>(color.r) / 255.f,
      static_cast<float>(color.g) / 255.f,
      static_cast<float>(color.b) / 255.f,
      static_cast<float>(color.a) / 255.f,
  };
}

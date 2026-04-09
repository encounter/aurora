#pragma once

#include "../../internal.hpp"
#include "../../gx/gx.hpp"

static aurora::Module Log("aurora::gx");

using aurora::gx::g_gxState;

static inline aurora::Vec4<float> from_gx_color(GXColor color) {
  return {
      static_cast<float>(color.r) / 255.f,
      static_cast<float>(color.g) / 255.f,
      static_cast<float>(color.b) / 255.f,
      static_cast<float>(color.a) / 255.f,
  };
}

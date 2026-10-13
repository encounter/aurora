#pragma once

#include "../gx/gx.hpp"

#include <string_view>

// WGSL helpers to convert EFB contents to GX values
namespace aurora::gfx::efb_wgsl {

// fn gx_z24_from_depth(depth: f32) -> u32: forward GX Z24 (far = 0xFFFFFF) from host depth
inline constexpr std::string_view Z24Reversed = R"(
fn gx_z24_from_depth(depth: f32) -> u32 {
    return min(u32(clamp(1.0 - depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)";

inline constexpr std::string_view Z24Forward = R"(
fn gx_z24_from_depth(depth: f32) -> u32 {
    return min(u32(clamp(depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)";

constexpr std::string_view z24() noexcept { return gx::UseReversedZ ? Z24Reversed : Z24Forward; }

// fn unorm8(c: vec4f) -> vec4u: 8-bit channel values of a color sample
// fn efb_quantize(v: vec4u, pixelFormat: u32) -> vec4u: applies the EFB's storage precision
inline constexpr std::string_view Color = R"(
fn unorm8(c: vec4f) -> vec4u {
    return vec4u(clamp(c, vec4f(0.0), vec4f(1.0)) * 255.0 + 0.5);
}

fn efb_quantize_rgba6(v: vec4u) -> vec4u {
    return (v & vec4u(0xfcu)) | (v >> vec4u(6u));
}

fn efb_quantize(v: vec4u, pixelFormat: u32) -> vec4u {
    switch pixelFormat {
        case 1u: { // GX_PF_RGBA6_Z24
            return efb_quantize_rgba6(v);
        }
        case 2u: { // GX_PF_RGB565_Z16
            return vec4u((v.r & 0xf8u) | (v.r >> 5u), (v.g & 0xfcu) | (v.g >> 6u), (v.b & 0xf8u) | (v.b >> 5u), 255u);
        }
        default: {
            return vec4u(v.rgb, 255u);
        }
    }
}
)";

} // namespace aurora::gfx::efb_wgsl

#pragma once

#include "types.hpp"

namespace aurora::gfx {

BindGroupRef bind_group_ref(const WGPUBindGroupDescriptor& descriptor);
wgpu::BindGroup find_bind_group(BindGroupRef id);
wgpu::Sampler sampler_ref(const wgpu::SamplerDescriptor& descriptor);

namespace detail {
void clear_bind_group_cache();
void expire_cached_bind_groups();
void shutdown_resource_cache();
} // namespace detail

} // namespace aurora::gfx

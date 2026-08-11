#include "resource_cache.hpp"

#include "frame.hpp"
#include "hash.hpp"
#include "../webgpu/gpu.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>

#include <absl/container/flat_hash_map.h>
#include <tracy/Tracy.hpp>

namespace aurora {
// These specializations are coupled to Dawn's descriptor layouts. Hash only
// the value-bearing fields and referenced entries, skipping chains and labels.
template <>
inline HashType xxh3_hash(const WGPUBindGroupDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(WGPUBindGroupDescriptor, layout);
  const auto hash = xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                                sizeof(WGPUBindGroupDescriptor) - offset - sizeof(void*), seed);
  return xxh3_hash_s(input.entries, sizeof(WGPUBindGroupEntry) * input.entryCount, hash);
}

template <>
inline HashType xxh3_hash(const wgpu::SamplerDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(wgpu::SamplerDescriptor, addressModeU);
  return xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset, sizeof(wgpu::SamplerDescriptor) - offset - 2, seed);
}
} // namespace aurora

namespace aurora::gfx {
namespace {

constexpr Module Log{"aurora::gfx"};

struct CachedBindGroup {
  wgpu::BindGroup bindGroup;
  uint32_t lastUsedFrame = 0;
};

constexpr uint32_t BindGroupCacheRetainFrames = 32;
constexpr uint32_t BindGroupCacheSweepPeriod = 16;

absl::flat_hash_map<BindGroupRef, CachedBindGroup> g_cachedBindGroups;
absl::flat_hash_map<SamplerRef, wgpu::Sampler> g_cachedSamplers;
std::mutex g_bindGroupCacheMutex;
std::mutex g_samplerCacheMutex;

} // namespace

namespace detail {

void clear_bind_group_cache() {
  std::lock_guard lock{g_bindGroupCacheMutex};
  g_cachedBindGroups.clear();
}

void expire_cached_bind_groups() {
  std::lock_guard lock{g_bindGroupCacheMutex};
  const auto frameIndex = current_frame();
  if (g_cachedBindGroups.empty() || frameIndex == UINT32_MAX || frameIndex % BindGroupCacheSweepPeriod != 0) {
    return;
  }

  ZoneScoped;
  for (auto it = g_cachedBindGroups.begin(); it != g_cachedBindGroups.end();) {
    if (frameIndex - it->second.lastUsedFrame > BindGroupCacheRetainFrames) {
      g_cachedBindGroups.erase(it++);
    } else {
      ++it;
    }
  }
}

void shutdown_resource_cache() {
  clear_bind_group_cache();
  std::lock_guard lock{g_samplerCacheMutex};
  g_cachedSamplers.clear();
}

} // namespace detail

BindGroupRef bind_group_ref(const WGPUBindGroupDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  std::lock_guard lock{g_bindGroupCacheMutex};
  const auto it = g_cachedBindGroups.find(id);
  if (it == g_cachedBindGroups.end()) {
    auto bindGroup = wgpu::BindGroup::Acquire(wgpuDeviceCreateBindGroup(webgpu::g_device.Get(), &descriptor));
    g_cachedBindGroups.emplace(id, CachedBindGroup{
                                       .bindGroup = std::move(bindGroup),
                                       .lastUsedFrame = current_frame(),
                                   });
  } else {
    it->second.lastUsedFrame = current_frame();
  }
  return id;
}

wgpu::BindGroup find_bind_group(BindGroupRef id) {
  std::lock_guard lock{g_bindGroupCacheMutex};
  const auto it = g_cachedBindGroups.find(id);
  CHECK(it != g_cachedBindGroups.end(), "get_bind_group: failed to locate {:x}", id);
  return it->second.bindGroup;
}

wgpu::Sampler sampler_ref(const wgpu::SamplerDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  std::lock_guard lock{g_samplerCacheMutex};
  auto it = g_cachedSamplers.find(id);
  if (it == g_cachedSamplers.end()) {
    it = g_cachedSamplers.try_emplace(id, webgpu::g_device.CreateSampler(&descriptor)).first;
  }
  return it->second;
}

} // namespace aurora::gfx

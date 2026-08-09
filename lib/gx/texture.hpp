#pragma once

#include "gx.hpp"

#include <cstddef>
#include <cstdint>

namespace aurora::gx {
struct TextureStats {
  uint64_t uploadBytes = 0;
  uint64_t hashedBytes = 0;
  uint64_t objectHits = 0;
  uint64_t contentHits = 0;
  uint64_t replacementHits = 0;
  uint64_t misses = 0;
  uint64_t evictions = 0;
  uint64_t contentCacheBytes = 0;
  uint64_t contentCacheEntries = 0;
  uint64_t pendingLoads = 0;
  uint64_t publishes = 0;
  uint64_t publishBytes = 0;
};

const TextureStats& texture_stats() noexcept;

namespace texture {
constexpr uint64_t ContentCacheBudgetBytes = 128ull * 1024ull * 1024ull;
constexpr uint64_t ObjectCacheIdleFrames = 600;
constexpr bool AsyncTextureReplacements = true;
constexpr uint32_t ReplacementThumbnailDim = 64;
constexpr uint64_t ReplacementPublishBudgetBytes = 12ull * 1024ull * 1024ull;

size_t texture_source_size(u32 format, u32 width, u32 height, u32 mipCount) noexcept;
size_t tlut_source_size(u16 numEntries) noexcept;

void invalidate_bindings() noexcept;
uint64_t current_bind_generation() noexcept;
void invalidate_replacement(uint64_t replacementId) noexcept;
void end_frame() noexcept;
void shutdown() noexcept;
void set_content_cache_budget_for_testing(uint64_t bytes) noexcept;

// Internal resolvers exposed for cache tests.
gfx::TextureHandle resolve_static_texture(const GXTexObj_& obj);
gfx::TextureHandle resolve_static_palette_texture(const GXTexObj_& obj, const GXTlutObj_& tlut);
} // namespace texture
} // namespace aurora::gx

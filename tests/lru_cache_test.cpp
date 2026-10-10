#include "gfx/lru_cache.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace aurora::gfx {
namespace {

TEST(LruCacheTest, BudgetOnlyCacheRetainsIdleEntriesAndStillUsesLruOrder) {
  LruCache<int, int> cache{{.bytes = 64}};
  cache.insert(1, 10, 32);
  cache.insert(2, 20, 32);
  for (int i = 0; i < 1024; ++i) {
    cache.end_frame();
  }
  EXPECT_EQ(cache.cached_count(), 2);
  ASSERT_NE(cache.find(1), nullptr);
  cache.insert(3, 30, 32);

  EXPECT_TRUE(cache.contains(1));
  EXPECT_FALSE(cache.contains(2));
  EXPECT_TRUE(cache.contains(3));
  EXPECT_EQ(cache.cached_bytes(), 64);
}

TEST(LruCacheTest, ZeroIdleLimitExpiresAtNextSweep) {
  LruCache<int, int> cache{{.bytes = 64, .idleFrames = 0, .sweepFrames = 4}};
  cache.insert(1, 10, 32);
  for (int i = 0; i < 3; ++i) {
    cache.end_frame();
  }
  EXPECT_TRUE(cache.contains(1));
  cache.end_frame();
  EXPECT_FALSE(cache.contains(1));
}

TEST(LruCacheTest, OversizedEntryDoesNotFlushReusableWorkingSet) {
  LruCache<int, int> cache{{64, 32, 16}};
  cache.insert(1, 1, 32);
  cache.insert(2, 2, 32);
  cache.insert(3, 3, 128);

  EXPECT_TRUE(cache.contains(1));
  EXPECT_TRUE(cache.contains(2));
  EXPECT_FALSE(cache.contains(3));
  EXPECT_EQ(cache.cached_bytes(), 64);
}

TEST(LruCacheTest, ReleasingPinPreservesLastUseForExpiry) {
  LruCache<int, int> cache{{64, 32, 16}};
  cache.insert_pinned(1, 1, 32);
  ASSERT_NE(cache.pin(1), nullptr); // Repeated pinning is not reference counting.
  for (int i = 0; i < 48; ++i) {
    cache.end_frame();
  }
  ASSERT_TRUE(cache.contains(1));
  EXPECT_EQ(cache.cached_bytes(), 0);
  EXPECT_EQ(cache.pinned_bytes(), 32);

  cache.unpin(1);
  cache.unpin(1);
  EXPECT_EQ(cache.cached_bytes(), 32);
  EXPECT_EQ(cache.pinned_bytes(), 0);
  for (int i = 0; i < 16; ++i) {
    cache.end_frame();
  }
  EXPECT_FALSE(cache.contains(1));
}

TEST(LruCacheTest, PinnedValueRemainsStableThroughGrowthAndEviction) {
  LruCache<int, int> cache{{64, 32, 16}};
  auto& current = cache.insert_pinned(0, 42, 128);
  for (int i = 1; i <= 1024; ++i) {
    cache.insert(i, i, 1);
  }
  EXPECT_EQ(&current, cache.find(0));
  EXPECT_EQ(current, 42);
  EXPECT_EQ(cache.cached_bytes(), 64);
  EXPECT_EQ(cache.pinned_bytes(), 128);

  cache.unpin(0);
  EXPECT_FALSE(cache.contains(0));
  EXPECT_EQ(cache.cached_bytes(), 64);
}

TEST(LruCacheTest, EvictionReleasesOnlyCacheOwnership) {
  LruCache<int, std::shared_ptr<int>> cache{{64, 32, 16}};
  auto recorded = std::make_shared<int>(42);
  std::weak_ptr<int> resource = recorded;
  cache.insert(1, recorded, 64);
  cache.insert(2, std::make_shared<int>(0), 64);

  EXPECT_FALSE(cache.contains(1));
  EXPECT_FALSE(resource.expired());
  EXPECT_EQ(*recorded, 42);
  recorded.reset();
  EXPECT_TRUE(resource.expired());
}

TEST(LruCacheTest, EveryRemovalNotifiesResourceOwnerOnce) {
  static std::vector<int> retired;
  retired.clear();
  LruCache<int, int> cache{{64, 32, 16}, [](const int& value) { retired.push_back(value); }};
  cache.insert(1, 10, 64);
  cache.insert(1, 11, 64); // Replacement
  cache.insert(2, 20, 64); // Budget
  cache.insert_pinned(3, 30, 128);
  cache.erase_if([](int key) { return key == 3; }); // Explicit retirement of pinned contents
  cache.clear();
  cache.clear();

  EXPECT_EQ(retired, (std::vector<int>{10, 11, 30, 20}));
  EXPECT_EQ(cache.cached_bytes(), 0);
  EXPECT_EQ(cache.cached_count(), 0);
  EXPECT_EQ(cache.pinned_bytes(), 0);
}

} // namespace
} // namespace aurora::gfx

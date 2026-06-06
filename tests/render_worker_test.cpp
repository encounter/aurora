#include "../lib/gfx/render_worker.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {
using namespace std::chrono_literals;
using aurora::gfx::render_worker::BoundedQueue;
using aurora::gfx::render_worker::FrameSlotPool;
using aurora::gfx::render_worker::ItemType;
using aurora::gfx::render_worker::QueueItem;

class RenderWorkerTest : public testing::Test {
protected:
  void TearDown() override { aurora::gfx::render_worker::shutdown(); }
};

TEST(RenderWorkerQueue, PreservesOrdering) {
  BoundedQueue queue{4};
  ASSERT_TRUE(queue.push(QueueItem{.type = ItemType::BeginFrame, .frameId = 1}));
  ASSERT_TRUE(queue.push(QueueItem{.type = ItemType::EncodePass, .frameId = 1, .passIndex = 7}));
  ASSERT_TRUE(queue.push(QueueItem{.type = ItemType::EndFrame, .frameId = 1}));

  bool closed = false;
  auto first = queue.pop_for(0ms, closed);
  auto second = queue.pop_for(0ms, closed);
  auto third = queue.pop_for(0ms, closed);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(first->type, ItemType::BeginFrame);
  EXPECT_EQ(second->type, ItemType::EncodePass);
  EXPECT_EQ(second->passIndex, 7u);
  EXPECT_EQ(third->type, ItemType::EndFrame);
  EXPECT_FALSE(closed);
}

TEST(RenderWorkerQueue, PushBlocksWhenFull) {
  BoundedQueue queue{1};
  ASSERT_TRUE(queue.push(QueueItem{.type = ItemType::BeginFrame}));

  std::atomic_bool pushed = false;
  auto future = std::async(std::launch::async, [&] {
    const bool result = queue.push(QueueItem{.type = ItemType::EncodePass});
    pushed.store(result, std::memory_order_release);
  });

  std::this_thread::sleep_for(20ms);
  EXPECT_FALSE(pushed.load(std::memory_order_acquire));

  bool closed = false;
  ASSERT_TRUE(queue.pop_for(0ms, closed).has_value());
  future.wait();
  EXPECT_TRUE(pushed.load(std::memory_order_acquire));
}

TEST_F(RenderWorkerTest, SyncWaitsForPriorWork) {
  std::vector<int> order;
  aurora::gfx::render_worker::initialize();
  aurora::gfx::render_worker::enqueue_begin_frame(1, [&] { order.push_back(1); });
  aurora::gfx::render_worker::enqueue_encode_pass(1, 0, [&] { order.push_back(2); });
  aurora::gfx::render_worker::synchronize();

  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], 1);
  EXPECT_EQ(order[1], 2);
  EXPECT_TRUE(aurora::gfx::render_worker::is_idle());
}

TEST_F(RenderWorkerTest, ShutdownCompletesQueuedWork) {
  std::atomic_int count = 0;
  aurora::gfx::render_worker::initialize();
  aurora::gfx::render_worker::enqueue_begin_frame(1, [&] { count.fetch_add(1, std::memory_order_acq_rel); });
  aurora::gfx::render_worker::shutdown();

  EXPECT_EQ(count.load(std::memory_order_acquire), 1);
  EXPECT_TRUE(aurora::gfx::render_worker::is_idle());
}

TEST(RenderWorkerFrameSlots, RecyclesReleasedSlots) {
  FrameSlotPool slots{2};
  const size_t first = slots.acquire();
  const size_t second = slots.acquire();
  EXPECT_NE(first, second);
  EXPECT_EQ(slots.free_count(), 0u);

  std::atomic_bool acquired = false;
  auto future = std::async(std::launch::async, [&] {
    const size_t recycled = slots.acquire();
    EXPECT_EQ(recycled, first);
    acquired.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(20ms);
  EXPECT_FALSE(acquired.load(std::memory_order_acquire));
  slots.release(first);
  future.wait();
  slots.release(second);
  EXPECT_EQ(slots.free_count(), 1u);
}
} // namespace

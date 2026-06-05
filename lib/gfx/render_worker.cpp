#include "render_worker.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

#include <tracy/Tracy.hpp>

namespace aurora::gfx::render_worker {
namespace {
constexpr size_t QueueCapacity = 256;
constexpr auto IdlePumpInterval = std::chrono::milliseconds{1};

BoundedQueue g_queue{QueueCapacity};
std::thread g_thread;
std::atomic_bool g_running = false;
std::atomic_size_t g_pendingItems = 0;
std::thread::id g_workerThreadId;

void complete_sync(const std::shared_ptr<SyncState>& sync) {
  if (!sync) {
    return;
  }
  ZoneScoped;
  {
    std::lock_guard lock{sync->mutex};
    sync->complete = true;
  }
  sync->cv.notify_all();
}

void worker_main() {
#ifdef TRACY_ENABLE
  tracy::SetThreadName("Aurora render worker");
#endif
  g_workerThreadId = std::this_thread::get_id();

  while (true) {
    bool closed = false;
    auto item = g_queue.pop_for(IdlePumpInterval, closed);
    if (!item) {
      if (closed) {
        break;
      }
      continue;
    }

    if (item->work) {
      ZoneScopedN("QueueItem work");
      item->work();
    }
    complete_sync(item->sync);
    g_pendingItems.fetch_sub(1, std::memory_order_acq_rel);
    if (item->type == ItemType::Shutdown) {
      break;
    }
  }

  g_workerThreadId = {};
}

void enqueue(QueueItem item) {
  ZoneScoped;
  if (is_worker_thread()) {
    if (item.work) {
      item.work();
    }
    complete_sync(item.sync);
    return;
  }

  if (!g_running.load(std::memory_order_acquire)) {
    if (item.work) {
      item.work();
    }
    complete_sync(item.sync);
    return;
  }

  g_pendingItems.fetch_add(1, std::memory_order_acq_rel);
  if (!g_queue.push(std::move(item))) {
    g_pendingItems.fetch_sub(1, std::memory_order_acq_rel);
  }
}
} // namespace

BoundedQueue::BoundedQueue(size_t capacity) : m_capacity(capacity) {}

bool BoundedQueue::push(QueueItem item) {
  ZoneScoped;
  std::unique_lock lock{m_mutex};
  m_notFull.wait(lock, [&] { return m_closed || m_items.size() < m_capacity; });
  if (m_closed) {
    return false;
  }
  m_items.emplace_back(std::move(item));
  lock.unlock();
  m_notEmpty.notify_one();
  return true;
}

std::optional<QueueItem> BoundedQueue::pop_for(std::chrono::milliseconds timeout, bool& closed) {
  std::unique_lock lock{m_mutex};
  m_notEmpty.wait_for(lock, timeout, [&] { return m_closed || !m_items.empty(); });
  closed = m_closed && m_items.empty();
  if (m_items.empty()) {
    return std::nullopt;
  }
  auto item = std::move(m_items.front());
  m_items.pop_front();
  lock.unlock();
  m_notFull.notify_one();
  return item;
}

void BoundedQueue::close() {
  {
    std::lock_guard lock{m_mutex};
    m_closed = true;
  }
  m_notEmpty.notify_all();
  m_notFull.notify_all();
}

void BoundedQueue::reset() {
  {
    std::lock_guard lock{m_mutex};
    m_items.clear();
    m_closed = false;
  }
  m_notFull.notify_all();
}

size_t BoundedQueue::size() const {
  std::lock_guard lock{m_mutex};
  return m_items.size();
}

FrameSlotPool::FrameSlotPool(size_t slotCount) : m_freeSlots(slotCount, true) {}

size_t FrameSlotPool::acquire() {
  std::unique_lock lock{m_mutex};
  m_cv.wait(lock, [&] {
    for (const bool free : m_freeSlots) {
      if (free) {
        return true;
      }
    }
    return false;
  });
  for (size_t i = 0; i < m_freeSlots.size(); ++i) {
    if (m_freeSlots[i]) {
      m_freeSlots[i] = false;
      return i;
    }
  }
  return 0;
}

std::optional<size_t> FrameSlotPool::try_acquire() {
  std::lock_guard lock{m_mutex};
  for (size_t i = 0; i < m_freeSlots.size(); ++i) {
    if (m_freeSlots[i]) {
      m_freeSlots[i] = false;
      return i;
    }
  }
  return std::nullopt;
}

void FrameSlotPool::release(size_t slot) {
  {
    std::lock_guard lock{m_mutex};
    if (slot < m_freeSlots.size()) {
      m_freeSlots[slot] = true;
    }
  }
  m_cv.notify_one();
}

void FrameSlotPool::reset() {
  {
    std::lock_guard lock{m_mutex};
    std::fill(m_freeSlots.begin(), m_freeSlots.end(), true);
  }
  m_cv.notify_all();
}

size_t FrameSlotPool::free_count() const {
  std::lock_guard lock{m_mutex};
  return static_cast<size_t>(std::ranges::count(m_freeSlots, true));
}

void initialize() {
  if (g_running.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  g_queue.reset();
  g_pendingItems.store(0, std::memory_order_release);
  g_thread = std::thread(worker_main);
}

void shutdown() {
  if (!g_running.load(std::memory_order_acquire)) {
    return;
  }
  enqueue({
      .type = ItemType::Shutdown,
  });
  if (g_thread.joinable()) {
    g_thread.join();
  }
  g_running.store(false, std::memory_order_release);
  g_queue.close();
  g_queue.reset();
  g_pendingItems.store(0, std::memory_order_release);
}

void enqueue_begin_frame(uint64_t frameId, WorkCallback work) {
  enqueue({
      .type = ItemType::BeginFrame,
      .frameId = frameId,
      .work = std::move(work),
  });
}

void enqueue_encode_pass(uint64_t frameId, uint32_t passIndex, WorkCallback work) {
  enqueue({
      .type = ItemType::EncodePass,
      .frameId = frameId,
      .passIndex = passIndex,
      .work = std::move(work),
  });
}

void enqueue_end_frame(uint64_t frameId, WorkCallback work) {
  enqueue({
      .type = ItemType::EndFrame,
      .frameId = frameId,
      .work = std::move(work),
  });
}

void enqueue_work(WorkCallback work) {
  enqueue({
      .type = ItemType::Sync,
      .work = std::move(work),
  });
}

void synchronize() {
  if (is_worker_thread()) {
    return;
  }

  ZoneScoped;
  auto sync = std::make_shared<SyncState>();
  enqueue({
      .type = ItemType::Sync,
      .sync = sync,
  });
  std::unique_lock lock{sync->mutex};
  sync->cv.wait(lock, [&] { return sync->complete; });
}

bool is_worker_thread() noexcept { return g_workerThreadId == std::this_thread::get_id(); }

bool is_idle() noexcept { return g_pendingItems.load(std::memory_order_acquire) == 0; }

} // namespace aurora::gfx::render_worker

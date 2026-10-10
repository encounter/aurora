#pragma once

#include <absl/container/flat_hash_map.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <optional>
#include <utility>

namespace aurora::gfx {

struct LruCacheLimits {
  uint64_t bytes;
  std::optional<uint64_t> idleFrames = std::nullopt;
  uint64_t sweepFrames = 1;
};

inline constexpr LruCacheLimits RenderTextureCacheLimits{
    .bytes = 64ull * 1024 * 1024,
    .idleFrames = 32,
    .sweepFrames = 16,
};

// LRU storage for resources that can be recreated
template <typename Key, typename Value>
class LruCache {
  struct Entry {
    Key key;
    Value value;
    uint64_t bytes;
    uint64_t lastUsedFrame;
    bool pinned;
  };
  using Lru = std::list<Entry>;
  using Iterator = Lru::iterator;

public:
  using OnEvict = void (*)(const Value&);

  explicit LruCache(LruCacheLimits limits, OnEvict onEvict = nullptr) : m_limits(limits), m_onEvict(onEvict) {
    assert(limits.sweepFrames != 0);
  }
  LruCache(const LruCache&) = delete;
  LruCache& operator=(const LruCache&) = delete;

  Value* find(const Key& key) {
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
      return nullptr;
    }
    touch(it->second);
    return &it->second->value;
  }

  Value* pin(const Key& key) {
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
      return nullptr;
    }
    auto entry = it->second;
    touch(entry);
    if (!entry->pinned) {
      entry->pinned = true;
      m_pinnedBytes += entry->bytes;
      ++m_pinnedCount;
    }
    return &entry->value;
  }

  void unpin(const Key& key) {
    const auto it = m_entries.find(key);
    if (it == m_entries.end() || !it->second->pinned) {
      return;
    }
    auto entry = it->second;
    entry->pinned = false;
    m_pinnedBytes -= entry->bytes;
    --m_pinnedCount;
    // Don't attempt to cache an oversized resource
    if (entry->bytes > m_limits.bytes) {
      erase(entry);
    }
    trim();
  }

  void insert(Key key, Value value, uint64_t bytes) {
    auto entry = insert(std::move(key), std::move(value), bytes, false);
    if (bytes > m_limits.bytes) {
      erase(entry);
    }
    trim();
  }

  Value& insert_pinned(Key key, Value value, uint64_t bytes) {
    return insert(std::move(key), std::move(value), bytes, true)->value;
  }

  template <typename Predicate>
  void erase_if(Predicate predicate) {
    for (auto it = m_lru.begin(); it != m_lru.end();) {
      auto entry = it++;
      if (predicate(entry->key)) {
        erase(entry);
      }
    }
  }

  void end_frame() {
    if (!m_limits.idleFrames || ++m_frame % m_limits.sweepFrames != 0) {
      return;
    }
    for (auto it = m_lru.end(); it != m_lru.begin();) {
      auto entry = std::prev(it);
      if (m_frame - entry->lastUsedFrame <= *m_limits.idleFrames) {
        break;
      }
      if (entry->pinned) {
        it = entry;
      } else {
        erase(entry);
      }
    }
  }

  void clear() {
    while (!m_lru.empty()) {
      erase(m_lru.begin());
    }
    m_frame = 0;
  }

  void set_budget(uint64_t bytes) {
    m_limits.bytes = bytes;
    trim();
  }

  bool contains(const Key& key) const { return m_entries.contains(key); }
  uint64_t budget() const { return m_limits.bytes; }
  uint64_t cached_bytes() const { return m_bytes - m_pinnedBytes; }
  size_t cached_count() const { return m_entries.size() - m_pinnedCount; }
  uint64_t pinned_bytes() const { return m_pinnedBytes; }

private:
  void touch(Iterator entry) {
    entry->lastUsedFrame = m_frame;
    m_lru.splice(m_lru.begin(), m_lru, entry);
  }

  Iterator insert(Key key, Value value, uint64_t bytes, bool pinned) {
    if (const auto it = m_entries.find(key); it != m_entries.end()) {
      erase(it->second);
    }
    m_lru.push_front({std::move(key), std::move(value), bytes, m_frame, pinned});
    m_entries.emplace(m_lru.front().key, m_lru.begin());
    m_bytes += bytes;
    if (pinned) {
      m_pinnedBytes += bytes;
      ++m_pinnedCount;
    }
    return m_lru.begin();
  }

  void erase(Iterator entry) {
    if (m_onEvict) {
      m_onEvict(entry->value);
    }
    m_bytes -= entry->bytes;
    if (entry->pinned) {
      m_pinnedBytes -= entry->bytes;
      --m_pinnedCount;
    }
    m_entries.erase(entry->key);
    m_lru.erase(entry);
  }

  void trim() {
    for (auto it = m_lru.end(); it != m_lru.begin() && cached_bytes() > m_limits.bytes;) {
      auto entry = std::prev(it);
      if (entry->pinned) {
        it = entry;
      } else {
        erase(entry);
      }
    }
  }

  LruCacheLimits m_limits;
  OnEvict m_onEvict;
  Lru m_lru;
  absl::flat_hash_map<Key, Iterator> m_entries;
  uint64_t m_frame = 0;
  uint64_t m_bytes = 0;
  uint64_t m_pinnedBytes = 0;
  size_t m_pinnedCount = 0;
};

} // namespace aurora::gfx

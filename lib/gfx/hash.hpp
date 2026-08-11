#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace aurora {
#if INTPTR_MAX == INT32_MAX
using HashType = XXH32_hash_t;
#else
using HashType = XXH64_hash_t;
#endif

inline HashType xxh3_hash_s(const void* input, size_t len, HashType seed = 0) {
  return static_cast<HashType>(XXH3_64bits_withSeed(input, len, seed));
}

template <typename T>
HashType xxh3_hash(const T& input, HashType seed = 0) {
  static_assert(std::has_unique_object_representations_v<T>);
  return xxh3_hash_s(&input, sizeof(T), seed);
}

class Hasher {
public:
  explicit Hasher(XXH64_hash_t seed = 0) {
    XXH3_INITSTATE(&m_state);
    XXH3_64bits_reset_withSeed(&m_state, seed);
  }

  void update(const void* data, size_t size) { XXH3_64bits_update(&m_state, data, size); }

  template <typename T>
  void update(const T& data) {
    static_assert(std::has_unique_object_representations_v<T>);
    update(&data, sizeof(T));
  }

  [[nodiscard]] XXH64_hash_t digest() const { return XXH3_64bits_digest(&m_state); }

private:
  XXH3_state_t m_state;
};
} // namespace aurora

#pragma once

#include "logging.hpp" // IWYU pragma: keep

#include <aurora/aurora.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <vector>

using namespace std::string_view_literals;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#ifndef SBIG
#define SBIG(q)                                                                                                        \
  (((q) & 0x000000FF) << 24 | ((q) & 0x0000FF00) << 8 | ((q) & 0x00FF0000) >> 8 | ((q) & 0xFF000000) >> 24)
#endif
#else
#ifndef SBIG
#define SBIG(q) (q)
#endif
#endif

#ifndef ALWAYS_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline inline
#else
#define ALWAYS_INLINE inline
#endif
#endif

template <typename T>
  requires(sizeof(T) == sizeof(uint16_t) && std::is_arithmetic_v<T>)
ALWAYS_INLINE constexpr T bswap(T val) noexcept {
  auto bits = std::bit_cast<uint16_t>(val);
#if defined(__GNUC__) || defined(__clang__)
  bits = __builtin_bswap16(bits);
#elif defined(_WIN32)
  bits = _byteswap_ushort(bits);
#else
  bits = static_cast<uint16_t>((bits << 8) | (bits >> 8));
#endif
  return std::bit_cast<T>(bits);
}

template <typename T>
  requires(sizeof(T) == sizeof(uint32_t) && std::is_arithmetic_v<T>)
ALWAYS_INLINE constexpr T bswap(T val) noexcept {
  auto bits = std::bit_cast<uint32_t>(val);
#if defined(__GNUC__) || defined(__clang__)
  bits = __builtin_bswap32(bits);
#elif defined(_WIN32)
  bits = _byteswap_ulong(bits);
#else
  bits = ((bits & 0x000000ffU) << 24) | ((bits & 0x0000ff00U) << 8) | ((bits & 0x00ff0000U) >> 8) |
         ((bits & 0xff000000U) >> 24);
#endif
  return std::bit_cast<T>(bits);
}

template <typename T>
  requires(sizeof(T) == sizeof(uint64_t) && std::is_arithmetic_v<T>)
ALWAYS_INLINE constexpr T bswap(T val) noexcept {
  auto bits = std::bit_cast<uint64_t>(val);
#if defined(__GNUC__) || defined(__clang__)
  bits = __builtin_bswap64(bits);
#elif defined(_WIN32)
  bits = _byteswap_uint64(bits);
#else
  bits = ((bits & 0x00000000000000ffULL) << 56) | ((bits & 0x000000000000ff00ULL) << 40) |
         ((bits & 0x0000000000ff0000ULL) << 24) | ((bits & 0x00000000ff000000ULL) << 8) |
         ((bits & 0x000000ff00000000ULL) >> 8) | ((bits & 0x0000ff0000000000ULL) >> 24) |
         ((bits & 0x00ff000000000000ULL) >> 40) | ((bits & 0xff00000000000000ULL) >> 56);
#endif
  return std::bit_cast<T>(bits);
}

template <typename T>
  requires(std::is_trivially_copyable_v<T>)
ALWAYS_INLINE T unaligned_load(const void* ptr) noexcept {
  T value;
  std::memcpy(&value, ptr, sizeof(value));
  return value;
}

template <typename T>
  requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
ALWAYS_INLINE T read_bits(const void* ptr, const std::endian e = std::endian::big) noexcept {
  using U = std::make_unsigned_t<T>;
  U val = unaligned_load<U>(ptr);
  if constexpr (sizeof(U) > 1) {
    if (e != std::endian::native) {
      val = bswap(val);
    }
  }
  return std::bit_cast<T>(val);
}

template <size_t N>
struct uint_of_size;
template <>
struct uint_of_size<1> {
  using type = uint8_t;
};
template <>
struct uint_of_size<2> {
  using type = uint16_t;
};
template <>
struct uint_of_size<4> {
  using type = uint32_t;
};
template <>
struct uint_of_size<8> {
  using type = uint64_t;
};
template <size_t N>
using uint_of_size_t = uint_of_size<N>::type;

template <typename T>
  requires(std::is_floating_point_v<T> && requires { typename uint_of_size_t<sizeof(T)>; })
ALWAYS_INLINE T read_bits(const void* ptr, const std::endian e = std::endian::big) noexcept {
  using U = uint_of_size_t<sizeof(T)>;
  return std::bit_cast<T>(read_bits<U>(ptr, e));
}

template <typename T>
  requires(std::is_enum_v<T>)
ALWAYS_INLINE constexpr auto underlying(T value) noexcept -> std::underlying_type_t<T> {
  return static_cast<std::underlying_type_t<T>>(value);
}

#define AURORA_ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define POINTER_ADD_TYPE(type_, ptr_, offset_) ((type_)((uintptr_t)(ptr_) + (uintptr_t)(offset_)))
#define POINTER_ADD(ptr_, offset_) POINTER_ADD_TYPE(decltype(ptr_), ptr_, offset_)

#if !defined(__has_cpp_attribute)
#define __has_cpp_attribute(name) 0
#endif
#if __has_cpp_attribute(unlikely)
#define UNLIKELY [[unlikely]]
#else
#define UNLIKELY
#endif
#if __has_cpp_attribute(likely)
#define LIKELY [[likely]]
#else
#define LIKELY
#endif
#define FATAL(msg, ...) Log.fatal(msg, ##__VA_ARGS__);
#define AURORA_ASSERT(cond, msg, ...)                                                                                  \
  if (!(cond))                                                                                                         \
  UNLIKELY FATAL(msg, ##__VA_ARGS__)
#ifdef NDEBUG
#define CHECK(cond, msg, ...)
#else
#define CHECK(cond, msg, ...) AURORA_ASSERT(cond, msg, ##__VA_ARGS__)
#endif
#define DEFAULT_FATAL(msg, ...) UNLIKELY default : FATAL(msg, ##__VA_ARGS__)
#define TRY(cond, msg, ...)                                                                                            \
  if (!(cond))                                                                                                         \
    UNLIKELY {                                                                                                         \
      Log.error(msg, ##__VA_ARGS__);                                                                                   \
      return false;                                                                                                    \
    }
#define TRY_WARN(cond, msg, ...)                                                                                       \
  if (!(cond))                                                                                                         \
    UNLIKELY { Log.warn(msg, ##__VA_ARGS__); }

#define UNIMPLEMENTED() FATAL("UNIMPLEMENTED: {}", __FUNCTION__)

namespace aurora {
extern AuroraConfig g_config;
extern uint32_t g_sdlCustomEventsStart;
extern char g_gameName[4];

template <typename T>
class ArrayRef {
public:
  using value_type = std::remove_cvref_t<T>;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = const_pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  ArrayRef() = default;
  explicit ArrayRef(const T& one) : ptr(&one), length(1) {}
  ArrayRef(const T* data, size_t length) : ptr(data), length(length) {}
  ArrayRef(const T* begin, const T* end) : ptr(begin), length(end - begin) {}
  template <size_t N>
  constexpr ArrayRef(const T (&arr)[N]) : ptr(arr), length(N) {}
  template <size_t N>
  constexpr ArrayRef(const std::array<T, N>& arr) : ptr(arr.data()), length(arr.size()) {}
  ArrayRef(const std::vector<T>& vec) : ptr(vec.data()), length(vec.size()) {}

  const T* data() const { return ptr; }
  size_t size() const { return length; }
  bool empty() const { return length == 0; }

  const T& front() const {
    assert(!empty());
    return ptr[0];
  }
  const T& back() const {
    assert(!empty());
    return ptr[length - 1];
  }
  const T& operator[](size_t i) const {
    assert(i < length && "Invalid index!");
    return ptr[i];
  }

  iterator begin() const { return ptr; }
  iterator end() const { return ptr + length; }

  reverse_iterator rbegin() const { return reverse_iterator(end()); }
  reverse_iterator rend() const { return reverse_iterator(begin()); }

  /// Disallow accidental assignment from a temporary.
  template <typename U>
  std::enable_if_t<std::is_same<U, T>::value, ArrayRef<T>>& operator=(U&& Temporary) = delete;

  /// Disallow accidental assignment from a temporary.
  template <typename U>
  std::enable_if_t<std::is_same<U, T>::value, ArrayRef<T>>& operator=(std::initializer_list<U>) = delete;

private:
  const T* ptr = nullptr;
  size_t length = 0;
};

class ByteBuffer {
public:
  constexpr ByteBuffer() noexcept = default;
  explicit ByteBuffer(size_t size) noexcept
  : m_data(static_cast<uint8_t*>(calloc(1, size))), m_length(size), m_capacity(size) {}
  explicit ByteBuffer(uint8_t* data, size_t size) noexcept : m_data(data), m_capacity(size), m_owned(false) {}
  ~ByteBuffer() noexcept { release(); }

  ByteBuffer(ByteBuffer&& rhs) noexcept
  : m_data(rhs.m_data), m_length(rhs.m_length), m_capacity(rhs.m_capacity), m_owned(rhs.m_owned) {
    rhs.m_data = nullptr;
    rhs.m_length = 0;
    rhs.m_capacity = 0;
    rhs.m_owned = true;
  }

  ByteBuffer& operator=(ByteBuffer&& rhs) noexcept {
    if (this == &rhs) {
      return *this;
    }
    release();
    m_data = rhs.m_data;
    m_length = rhs.m_length;
    m_capacity = rhs.m_capacity;
    m_owned = rhs.m_owned;
    rhs.m_data = nullptr;
    rhs.m_length = 0;
    rhs.m_capacity = 0;
    rhs.m_owned = true;
    return *this;
  }

  ByteBuffer(const ByteBuffer&) = delete;
  ByteBuffer& operator=(const ByteBuffer&) = delete;
  operator ArrayRef<uint8_t>() const noexcept { return {m_data, m_length}; }

  [[nodiscard]] uint8_t* data() noexcept { return m_data; }
  [[nodiscard]] const uint8_t* data() const noexcept { return m_data; }
  [[nodiscard]] size_t size() const noexcept { return m_length; }
  [[nodiscard]] bool empty() const noexcept { return m_length == 0; }

  void append(const void* data, size_t size) {
    resize(m_length + size, false);
    memcpy(m_data + m_length, data, size);
    m_length += size;
  }

  template <typename T>
  void append(const T& obj) {
    append(&obj, sizeof(T));
  }

  void append_zeroes(size_t size) {
    resize(m_length + size, true);
    m_length += size;
  }

  void release() {
    if (m_data != nullptr && m_owned) {
      free(m_data);
    }
    m_data = nullptr;
    m_length = 0;
    m_capacity = 0;
    m_owned = true;
  }

  void clear() { m_length = 0; }
  void reserve_extra(size_t size) { resize(m_length + size, true); }

  ByteBuffer clone() const {
    ByteBuffer clone{m_length};
    std::memcpy(clone.data(), m_data, m_length);
    return clone;
  }

private:
  uint8_t* m_data = nullptr;
  size_t m_length = 0;
  size_t m_capacity = 0;
  bool m_owned = true;

  void resize(size_t size, bool zeroed) {
    if (size == 0) {
      clear();
    } else if (m_data == nullptr) {
      m_data = static_cast<uint8_t*>(zeroed ? calloc(1, size) : malloc(size));
      m_owned = true;
    } else if (size > m_capacity) {
      if (!m_owned) {
        abort();
      }
      if (size < m_capacity * 2) {
        size = m_capacity * 2;
      }
      m_data = static_cast<uint8_t*>(realloc(m_data, size));
      if (zeroed) {
        memset(m_data + m_capacity, 0, size - m_capacity);
      }
    } else {
      return;
    }
    m_capacity = size;
  }
};
} // namespace aurora

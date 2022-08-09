#pragma once

#include <aurora/aurora.h>
#include <fmt/format.h>

#include <string>
#include <string_view>
#include <vector>
#include <cassert>

using namespace std::string_view_literals;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#ifndef SBIG
#define SBIG(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif
#else
#ifndef SBIG
#define SBIG(q) (q)
#endif
#endif

#ifdef __GNUC__
[[noreturn]] inline __attribute__((always_inline)) void unreachable() { __builtin_unreachable(); }
#elif defined(_MSC_VER)
[[noreturn]] __forceinline void unreachable() { __assume(false); }
#else
#error Unknown compiler
#endif

#ifndef ALIGN
#define ALIGN(x, a) (((x) + ((a)-1)) & ~((a)-1))
#endif

#if !defined(__has_cpp_attribute)
#define __has_cpp_attribute(name) 0
#endif
#if __has_cpp_attribute(unlikely)
#define UNLIKELY [[unlikely]]
#else
#define UNLIKELY
#endif
#define FATAL(msg, ...)                                                                                                \
  {                                                                                                                    \
    Log.report(LOG_FATAL, FMT_STRING(msg), ##__VA_ARGS__);                                                             \
    unreachable();                                                                                                     \
  }
#define ASSERT(cond, msg, ...)                                                                                         \
  if (!(cond))                                                                                                         \
  UNLIKELY FATAL(msg, ##__VA_ARGS__)
#ifdef NDEBUG
#define CHECK(cond, msg, ...)
#else
#define CHECK(cond, msg, ...) ASSERT(cond, msg, ##__VA_ARGS__)
#endif
#define DEFAULT_FATAL(msg, ...) UNLIKELY default: FATAL(msg, ##__VA_ARGS__)

namespace aurora {
extern AuroraConfig g_config;

struct Module {
  const char* name;
  explicit Module(const char* name) noexcept : name(name) {}

  template <typename... T>
  inline void report(AuroraLogLevel level, fmt::format_string<T...> fmt, T&&... args) noexcept {
    auto message = fmt::format(fmt, std::forward<T>(args)...);
    if (g_config.logCallback != nullptr) {
      g_config.logCallback(level, message.c_str(), message.size());
    }
  }
};

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
} // namespace aurora

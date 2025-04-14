#pragma once

#include <aurora/aurora.h>

#include <fmt/base.h>
#include <fmt/format.h>

#include <cstdlib>
#include <string_view>

namespace aurora {
void log_internal(AuroraLogLevel level, const char* module, const char* message, unsigned int len) noexcept;

struct Module {
  const char* name;
  explicit Module(const char* name) noexcept : name(name) {}

  template <typename... T>
  void report(const AuroraLogLevel level, fmt::format_string<T...> fmt, T&&... args) noexcept {
    auto message = fmt::format(fmt, std::forward<T>(args)...);
    log_internal(level, name, message.c_str(), message.size());
  }

  template <typename... T>
  void debug(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_DEBUG, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  void info(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_INFO, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  void warn(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_WARNING, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  void error(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_ERROR, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  [[noreturn]] void fatal(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_FATAL, fmt, std::forward<T>(args)...);
    std::abort();
  }
};
} // namespace aurora

template <>
struct fmt::formatter<AuroraLogLevel> : formatter<std::string_view> {
  auto format(AuroraLogLevel level, format_context& ctx) const -> format_context::iterator;
};

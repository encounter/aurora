#include "logging.hpp"

#include <fmt/base.h>
#include <fmt/format.h>
#include <aurora/aurora.h>

#include <cstdio>
#include <string_view>

namespace aurora {
extern AuroraConfig g_config;

void log_internal(const AuroraLogLevel level, const char* module, const char* message,
                  const unsigned int len) noexcept {
  if (g_config.logCallback == nullptr) {
    fmt::println(stderr, "[{}] [{}] {}", level, module, std::string_view(message, len));
  } else {
    g_config.logCallback(level, module, message, len);
  }
}
} // namespace aurora

auto fmt::formatter<AuroraLogLevel>::format(const AuroraLogLevel level, format_context& ctx) const -> format_context::iterator {
  std::string_view name = "unknown";
  switch (level) {
  case LOG_DEBUG:
    name = "debug";
    break;
  case LOG_INFO:
    name = "info";
    break;
  case LOG_WARNING:
    name = "warning";
    break;
  case LOG_ERROR:
    name = "error";
    break;
  case LOG_FATAL:
    name = "fatal";
    break;
  default:
    break;
  }
  return formatter<std::string_view>::format(name, ctx);
}

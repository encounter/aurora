#pragma once

#include <string>

namespace aurora {

void log_system_information();

#ifdef __APPLE__
namespace detail {
std::string system_version_string();
} // namespace detail
#endif

} // namespace aurora

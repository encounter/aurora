#pragma once

#include <filesystem>

/**
 * Converts a std::filesystem::path to a std::string, UTF-8, without exploding on Windows.
 */
inline std::string fs_path_to_string(const std::filesystem::path& path) {
  const auto u8str = path.u8string();
  return { reinterpret_cast<const char*>(u8str.c_str()) };
}

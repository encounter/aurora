#pragma once

#include <string>

namespace kabufuda {
std::string WideToUTF8(std::wstring_view src);
std::wstring UTF8ToWide(std::string_view src);
} // namespace kabufuda

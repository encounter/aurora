#pragma once
#include <optional>
#include <string>

#include "Constants.hpp"

namespace aurora::card {
std::string ResolveDolphinCardPath(ECardSlot slot, const char* regionCode, bool isGciFolder);
}
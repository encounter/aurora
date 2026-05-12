#pragma once

#include <string>

namespace aurora {
  void log_system_information();

  namespace system_info {
#if __APPLE__
    std::string getSystemVersionString();
#endif
  }
}
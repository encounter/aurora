#include "system_info.hpp"
#include <Foundation/NSProcessInfo.h>
#include <Foundation/NSString.h>

namespace aurora::detail {
std::string system_version_string() {
    NSString * str = [[NSProcessInfo processInfo] operatingSystemVersionString];
    return [str UTF8String];
}
} // namespace aurora::detail

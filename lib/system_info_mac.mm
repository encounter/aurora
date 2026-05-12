#include "system_info.hpp"
#include <Foundation/NSProcessInfo.h>
#include <Foundation/NSString.h>

namespace aurora::system_info {
    std::string getSystemVersionString() {
        NSString * str = [[NSProcessInfo processInfo] operatingSystemVersionString];
        return [str UTF8String];
    }
}
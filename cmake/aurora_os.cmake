add_library(aurora_os STATIC lib/dolphin/os/OSInit.cpp
        lib/dolphin/os/OSMemory.cpp
        lib/dolphin/os/internal.hpp
        lib/dolphin/os/OSBootInfo.cpp
        lib/dolphin/os/OSTime.cpp
        lib/dolphin/os/OSArena.cpp
        lib/dolphin/os/OSAddress.cpp
        lib/dolphin/os/OSReport.cpp)
add_library(aurora::os ALIAS aurora_os)

target_include_directories(aurora_os PUBLIC include)
target_link_libraries(aurora_os PRIVATE aurora::core)

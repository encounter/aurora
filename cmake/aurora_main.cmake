add_library(aurora_main STATIC lib/main.cpp)
add_library(aurora::main ALIAS aurora_main)

target_include_directories(aurora_main PUBLIC include)
target_link_libraries(aurora_main PUBLIC SDL3::SDL3-static)

add_library(aurora_ms STATIC lib/dolphin/ms/mouse.cpp)

add_library(aurora::ms ALIAS aurora_ms)

target_include_directories(aurora_ms PUBLIC include)
target_link_libraries(aurora_ms PUBLIC aurora::core)
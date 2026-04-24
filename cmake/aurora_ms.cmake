add_library(aurora_ms STATIC lib/dolphin/ms/mouse.cpp)
add_library(aurora::ms ALIAS aurora_ms)
set_target_properties(aurora_ms PROPERTIES FOLDER "aurora")

target_include_directories(aurora_ms PUBLIC include)
target_link_libraries(aurora_ms PUBLIC aurora::core)
target_link_libraries(aurora_ms PRIVATE absl::flat_hash_map)

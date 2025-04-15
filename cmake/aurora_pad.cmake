add_library(aurora_pad STATIC lib/dolphin/pad/pad.cpp)

add_library(aurora::pad ALIAS aurora_pad)

target_include_directories(aurora_pad PUBLIC include)
target_link_libraries(aurora_pad PUBLIC aurora::core aurora::si)
target_link_libraries(aurora_pad PRIVATE absl::flat_hash_map)

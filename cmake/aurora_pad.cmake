add_library(aurora_pad STATIC lib/dolphin/pad/pad.cpp)
add_library(aurora::pad ALIAS aurora_pad)
set_target_properties(aurora_pad PROPERTIES FOLDER "aurora")

target_include_directories(aurora_pad PUBLIC include)
target_link_libraries(aurora_pad PUBLIC aurora::core aurora::si)
target_link_libraries(aurora_pad PRIVATE absl::flat_hash_map)

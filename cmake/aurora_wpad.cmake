add_library(aurora_wpad STATIC
  lib/revolution/wpad/wpad.cpp
  lib/revolution/wpad/backend.cpp
)
add_library(aurora::wpad ALIAS aurora_wpad)
set_target_properties(aurora_wpad PROPERTIES FOLDER "aurora")
target_include_directories(aurora_wpad PUBLIC include)
target_link_libraries(aurora_wpad PUBLIC aurora::pad)
target_link_libraries(aurora_wpad PRIVATE absl::flat_hash_map)

add_library(aurora_kpad STATIC lib/revolution/kpad.cpp)
add_library(aurora::kpad ALIAS aurora_kpad)
set_target_properties(aurora_kpad PROPERTIES FOLDER "aurora")
target_include_directories(aurora_kpad PUBLIC include)
target_link_libraries(aurora_kpad PUBLIC aurora::wpad)

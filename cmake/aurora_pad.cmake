add_library(aurora_pad STATIC
        lib/input/port.cpp
        lib/input/port.hpp
        lib/input/profiles.cpp
        lib/input/legacy.cpp
        lib/dolphin/pad/pad.cpp
)
add_library(aurora::pad ALIAS aurora_pad)
set_target_properties(aurora_pad PROPERTIES FOLDER "aurora")

target_include_directories(aurora_pad PUBLIC include)
target_link_libraries(aurora_pad PUBLIC aurora::core aurora::vi)
target_link_libraries(aurora_pad PRIVATE absl::flat_hash_map nlohmann_json::nlohmann_json)

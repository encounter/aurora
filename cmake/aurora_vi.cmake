add_library(aurora_vi STATIC
        lib/dolphin/vi/vi.cpp
)
add_library(aurora::vi ALIAS aurora_vi)
set_target_properties(aurora_vi PROPERTIES FOLDER "aurora")

target_link_libraries(aurora_vi PUBLIC aurora::core)

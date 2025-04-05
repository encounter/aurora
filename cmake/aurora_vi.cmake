add_library(aurora_vi STATIC
        lib/dolphin/vi/vi.cpp
)
add_library(aurora::vi ALIAS aurora_vi)

target_link_libraries(aurora_vi PUBLIC aurora::core)

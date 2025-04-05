add_library(aurora_vi STATIC
        lib/dolphin/vi/vi.cpp
)
target_include_directories(aurora_vi PUBLIC include)
target_compile_definitions(aurora_vi PUBLIC AURORA TARGET_PC)

add_library(aurora::vi ALIAS aurora_vi)
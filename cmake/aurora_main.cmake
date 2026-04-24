add_library(aurora_main STATIC lib/main.cpp)
add_library(aurora::main ALIAS aurora_main)
set_target_properties(aurora_main PROPERTIES FOLDER "aurora")

target_include_directories(aurora_main PUBLIC include)
target_link_libraries(aurora_main PUBLIC ${AURORA_SDL3_TARGET})

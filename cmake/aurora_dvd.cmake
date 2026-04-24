include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuroraNodProvider.cmake)

add_library(aurora_dvd STATIC lib/dolphin/dvd/dvd.cpp)
add_library(aurora::dvd ALIAS aurora_dvd)
set_target_properties(aurora_dvd PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_dvd PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_dvd PUBLIC include)
target_link_libraries(aurora_dvd PUBLIC nod::nod ${AURORA_SDL3_TARGET})

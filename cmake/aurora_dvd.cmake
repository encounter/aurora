include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuroraNodProvider.cmake)
find_package(Threads REQUIRED)

add_library(aurora_dvd STATIC lib/dolphin/dvd/dvd.cpp lib/dolphin/dvd/dvd.hpp lib/dolphin/dvd/fst.cpp)
add_library(aurora::dvd ALIAS aurora_dvd)
set_target_properties(aurora_dvd PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_dvd PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_dvd PUBLIC include)
target_link_libraries(aurora_dvd PUBLIC nod::nod fmt::fmt ${AURORA_SDL3_TARGET} Threads::Threads)
target_link_libraries(aurora_dvd PRIVATE TracyClient)

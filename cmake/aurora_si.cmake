add_library(aurora_si STATIC lib/dolphin/si/si.cpp)

add_library(aurora::si ALIAS aurora_si)

target_include_directories(aurora_si PUBLIC include)
target_link_libraries(aurora_si PUBLIC aurora::core)

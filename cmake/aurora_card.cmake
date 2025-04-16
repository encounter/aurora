add_library(aurora_card STATIC
        lib/dolphin/card.cpp
)

add_library(aurora::card ALIAS aurora_card)

target_link_libraries(aurora_card PUBLIC aurora::core)
target_include_directories(aurora_card PRIVATE include)

add_library(aurora_card STATIC
        lib/card/BlockAllocationTable.cpp
        lib/card/Card.cpp
        lib/card/Directory.cpp
        lib/card/File.cpp
        lib/card/FileIO.cpp
        lib/card/SRAM.cpp
        lib/card/Util.cpp
        lib/dolphin/card.cpp
)

add_library(aurora::card ALIAS aurora_card)

target_link_libraries(aurora_card PUBLIC aurora::core)
target_include_directories(aurora_card PRIVATE include)

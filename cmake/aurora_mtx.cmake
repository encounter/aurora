add_library(aurora_mtx STATIC
  lib/dolphin/mtx.c
)
add_library(aurora::mtx ALIAS aurora_mtx)

target_include_directories(aurora_mtx PUBLIC include)

add_library(aurora_mtx STATIC
  lib/dolphin/mtx/mtx.c
  lib/dolphin/mtx/mtxstack.c
  lib/dolphin/mtx/mtxvec.c
  lib/dolphin/mtx/mtx44.c
  lib/dolphin/mtx/vec.c
  lib/dolphin/mtx/quat.c
)
add_library(aurora::mtx ALIAS aurora_mtx)

target_include_directories(aurora_mtx PUBLIC include)

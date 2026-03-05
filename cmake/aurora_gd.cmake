add_library(aurora_gd STATIC
        lib/dolphin/gd/GDBase.cpp
        lib/dolphin/gd/GDGeometry.cpp
        lib/dolphin/gd/GDIndirect.cpp
        lib/dolphin/gd/GDLight.cpp
        lib/dolphin/gd/GDPixel.cpp
        lib/dolphin/gd/GDTev.cpp
        lib/dolphin/gd/GDTexture.cpp
        lib/dolphin/gd/GDTransform.cpp
)

add_library(aurora::gd ALIAS aurora_gd)

target_link_libraries(aurora_gd PUBLIC aurora::gx)

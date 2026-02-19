add_library(aurora_gx STATIC
        lib/gfx/common.cpp
        lib/gfx/texture.cpp
        lib/gfx/gx.cpp
        lib/gfx/gx_shader.cpp
        lib/gfx/texture_convert.cpp
        lib/gfx/shader_info.cpp
        lib/gfx/model/shader.cpp
        lib/gfx/fifo.cpp
        lib/gfx/command_processor.cpp
        lib/dolphin/gx/GXBump.cpp
        lib/dolphin/gx/GXCull.cpp
        lib/dolphin/gx/GXDispList.cpp
        lib/dolphin/gx/GXDraw.cpp
        lib/dolphin/gx/GXExtra.cpp
        lib/dolphin/gx/GXFifo.cpp
        lib/dolphin/gx/GXFrameBuffer.cpp
        lib/dolphin/gx/GXGeometry.cpp
        lib/dolphin/gx/GXGet.cpp
        lib/dolphin/gx/GXLighting.cpp
        lib/dolphin/gx/GXManage.cpp
        lib/dolphin/gx/GXPerf.cpp
        lib/dolphin/gx/GXPixel.cpp
        lib/dolphin/gx/GXTev.cpp
        lib/dolphin/gx/GXTexture.cpp
        lib/dolphin/gx/GXTransform.cpp
        lib/dolphin/gx/GXVert.cpp
)
add_library(aurora::gx ALIAS aurora_gx)

target_link_libraries(aurora_gx PUBLIC aurora::core xxhash)
target_link_libraries(aurora_gx PRIVATE absl::btree absl::flat_hash_map)
if (EMSCRIPTEN)
    target_link_options(aurora_gx PUBLIC -sUSE_WEBGPU=1 -sASYNCIFY -sEXIT_RUNTIME)
    target_compile_definitions(aurora_gx PRIVATE ENABLE_BACKEND_WEBGPU)
else ()
    target_link_libraries(aurora_gx PRIVATE dawn::webgpu_dawn)
    target_compile_definitions(aurora_gx PRIVATE WEBGPU_DAWN)
endif ()
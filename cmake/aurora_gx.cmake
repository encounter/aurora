add_library(aurora_gx STATIC
        lib/gfx/clear.cpp
        lib/gfx/common.cpp
        lib/gfx/depth_peek.cpp
        lib/gfx/pipeline_cache.cpp
        lib/gfx/dds_io.cpp
        lib/gfx/tex_copy_conv.cpp
        lib/gfx/tex_palette_conv.cpp
        lib/gfx/texture.cpp
        lib/gfx/texture_convert.cpp
        lib/gfx/texture_replacement.cpp
        lib/gx/command_processor.cpp
        lib/gx/fifo.cpp
        lib/gx/gx.cpp
        lib/gx/pipeline.cpp
        lib/gx/shader.cpp
        lib/gx/shader_info.cpp
        lib/dolphin/gx/GXBump.cpp
        lib/dolphin/gx/GXCull.cpp
        lib/dolphin/gx/GXCpu2Efb.cpp
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
        lib/dolphin/gx/GXAurora.cpp
)
add_library(aurora::gx ALIAS aurora_gx)
set_target_properties(aurora_gx PROPERTIES FOLDER "aurora")

target_link_libraries(aurora_gx PUBLIC aurora::core xxhash)
target_link_libraries(aurora_gx PRIVATE absl::btree absl::flat_hash_map dawn::webgpu_dawn sqlite3 TracyClient)
target_compile_definitions(aurora_gx PRIVATE WEBGPU_DAWN)

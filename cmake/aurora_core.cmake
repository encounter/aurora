add_library(aurora_core STATIC
        lib/aurora.cpp
        lib/input.cpp
        lib/window.cpp
        lib/logging.cpp
)
add_library(aurora::core ALIAS aurora_core)
set_target_properties(aurora_core PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_core PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_core PUBLIC include)
target_link_libraries(aurora_core PUBLIC fmt::fmt ${AURORA_SDL3_TARGET} xxhash)
target_link_libraries(aurora_core PRIVATE absl::btree absl::flat_hash_map sqlite3 TracyClient)
if (AURORA_ENABLE_GX AND AURORA_CACHE_USE_ZSTD)
    target_compile_definitions(aurora_core PRIVATE AURORA_CACHE_USE_ZSTD)
    target_link_libraries(aurora_core PRIVATE libzstd_static)
endif ()

if (AURORA_ENABLE_GX)
    target_sources(aurora_core PRIVATE lib/imgui.cpp)
    target_link_libraries(aurora_core PUBLIC imgui)
endif ()

if(AURORA_ENABLE_RMLUI)
    target_compile_definitions(aurora_core PUBLIC AURORA_ENABLE_RMLUI)

    target_sources(aurora_core PRIVATE
            lib/rmlui.cpp
            lib/rmlui/RmlUi_Backend_Aurora.cpp
            lib/rmlui/WebGPURenderInterface.cpp
            lib/rmlui/SystemInterface_Aurora.cpp
            lib/rmlui/FileInterface_SDL.cpp
    )
    target_link_libraries(aurora_core PUBLIC rmlui)

    target_link_libraries(aurora_core PUBLIC rmlui_backends)
endif ()

if (AURORA_ENABLE_GX)
    target_compile_definitions(aurora_core PUBLIC AURORA_ENABLE_GX WEBGPU_DAWN)
    target_sources(aurora_core PRIVATE lib/webgpu/gpu.cpp lib/webgpu/gpu_cache.cpp lib/dawn/BackendBinding.cpp)
    target_link_libraries(aurora_core PRIVATE dawn::webgpu_dawn)
    if (DAWN_ENABLE_VULKAN)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_VULKAN)
    endif ()
    if (DAWN_ENABLE_METAL)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_METAL)
        target_sources(aurora_core PRIVATE lib/dawn/MetalBinding.mm)
        set_source_files_properties(lib/dawn/MetalBinding.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
    endif ()
    if (DAWN_ENABLE_D3D11)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_D3D11)
    endif ()
    if (DAWN_ENABLE_D3D12)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_D3D12)
    endif ()
    if (DAWN_ENABLE_DESKTOP_GL OR DAWN_ENABLE_OPENGLES)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_OPENGL)
        if (DAWN_ENABLE_DESKTOP_GL)
            target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_DESKTOP_GL)
        endif ()
        if (DAWN_ENABLE_OPENGLES)
            target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_OPENGLES)
        endif ()
    endif ()
    if (DAWN_ENABLE_NULL)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_NULL)
    endif ()
endif ()

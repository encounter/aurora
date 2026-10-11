if (NOT TARGET imgui)
    message(STATUS "aurora: Fetching imgui")
    FetchContent_Declare(imgui
            URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
            URL_HASH SHA256=466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE
            EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui_headers INTERFACE)
    target_include_directories(imgui_headers INTERFACE ${imgui_SOURCE_DIR})

    if (AURORA_IMGUI_IMPLEMENTATION)
        add_library(imgui STATIC
                ${imgui_SOURCE_DIR}/imgui.cpp
                ${imgui_SOURCE_DIR}/imgui_demo.cpp
                ${imgui_SOURCE_DIR}/imgui_draw.cpp
                ${imgui_SOURCE_DIR}/imgui_tables.cpp
                ${imgui_SOURCE_DIR}/imgui_widgets.cpp
                ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        )

        add_library(imgui_backends STATIC
                ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
                ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
                ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
        )
        target_compile_definitions(imgui_backends PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
        target_link_libraries(imgui_backends PRIVATE imgui ${AURORA_SDL3_TARGET} dawn::webgpu_dawn)
        target_link_libraries(imgui PUBLIC imgui_backends imgui_headers)

        if (TARGET Freetype::Freetype)
            target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
            target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
            target_link_libraries(imgui PRIVATE Freetype::Freetype)
        endif ()
    endif ()
else ()
    message(STATUS "aurora: Using existing imgui")
    add_library(imgui_headers ALIAS imgui)
endif ()

if (TARGET imgui)
    target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../include)
    target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="aurora/imgui_config.h")
    target_sources(imgui PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../lib/imgui_config.cpp)
    target_link_libraries(imgui PRIVATE ${AURORA_SDL3_TARGET})
endif ()

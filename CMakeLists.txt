cmake_minimum_required(VERSION 3.13)
project(aurora LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

option(AURORA_NATIVE_MATRIX "Assume OpenGL-layout matrices, disables transposing" OFF)

add_subdirectory(extern)
add_library(aurora STATIC
    lib/aurora.cpp
    lib/webgpu/gpu.cpp
    lib/imgui.cpp
    lib/input.cpp
    lib/window.cpp
    lib/gfx/common.cpp
    lib/gfx/texture.cpp
    lib/gfx/gx.cpp
    lib/gfx/gx_shader.cpp
    lib/gfx/texture_convert.cpp
    lib/gfx/stream/shader.cpp
    lib/gfx/model/shader.cpp
    lib/dolphin/GXBump.cpp
    lib/dolphin/GXCull.cpp
    lib/dolphin/GXDispList.cpp
    lib/dolphin/GXDraw.cpp
    lib/dolphin/GXExtra.cpp
    lib/dolphin/GXFifo.cpp
    lib/dolphin/GXFrameBuffer.cpp
    lib/dolphin/GXGeometry.cpp
    lib/dolphin/GXGet.cpp
    lib/dolphin/GXLighting.cpp
    lib/dolphin/GXManage.cpp
    lib/dolphin/GXPerf.cpp
    lib/dolphin/GXPixel.cpp
    lib/dolphin/GXTev.cpp
    lib/dolphin/GXTexture.cpp
    lib/dolphin/GXTransform.cpp
    lib/dolphin/GXVert.cpp
    lib/dolphin/vi.cpp
    )
add_library(aurora::aurora ALIAS aurora)
target_compile_definitions(aurora PUBLIC AURORA TARGET_PC)
if (AURORA_NATIVE_MATRIX)
  target_compile_definitions(aurora PRIVATE AURORA_NATIVE_MATRIX)
endif ()
target_include_directories(aurora PUBLIC include)
target_include_directories(aurora PRIVATE ../imgui)
if (NOT TARGET SDL2::SDL2-static)
  find_package(SDL2 REQUIRED)
endif ()
target_link_libraries(aurora PUBLIC SDL2::SDL2-static fmt::fmt imgui xxhash)
if (EMSCRIPTEN)
  target_link_options(aurora PUBLIC -sUSE_WEBGPU=1 -sASYNCIFY -sEXIT_RUNTIME)
  target_compile_definitions(aurora PRIVATE ENABLE_BACKEND_WEBGPU)
else ()
  target_link_libraries(aurora PRIVATE dawn_native dawncpp webgpu_dawn)
  target_sources(aurora PRIVATE lib/dawn/BackendBinding.cpp)
  target_compile_definitions(aurora PRIVATE WEBGPU_DAWN)
endif ()
target_link_libraries(aurora PRIVATE absl::btree absl::flat_hash_map)
if (DAWN_ENABLE_VULKAN)
  target_compile_definitions(aurora PRIVATE DAWN_ENABLE_BACKEND_VULKAN)
  target_sources(aurora PRIVATE lib/dawn/VulkanBinding.cpp)
endif ()
if (DAWN_ENABLE_METAL)
  target_compile_definitions(aurora PRIVATE DAWN_ENABLE_BACKEND_METAL)
  target_sources(aurora PRIVATE lib/dawn/MetalBinding.mm)
  set_source_files_properties(lib/dawn/MetalBinding.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
endif ()
if (DAWN_ENABLE_D3D12)
  target_compile_definitions(aurora PRIVATE DAWN_ENABLE_BACKEND_D3D12)
  target_sources(aurora PRIVATE lib/dawn/D3D12Binding.cpp)
endif ()
if (DAWN_ENABLE_DESKTOP_GL OR DAWN_ENABLE_OPENGLES)
  target_compile_definitions(aurora PRIVATE DAWN_ENABLE_BACKEND_OPENGL)
  if (DAWN_ENABLE_DESKTOP_GL)
    target_compile_definitions(aurora PRIVATE DAWN_ENABLE_BACKEND_DESKTOP_GL)
  endif ()
  if (DAWN_ENABLE_OPENGLES)
    target_compile_definitions(aurora PRIVATE DAWN_ENABLE_BACKEND_OPENGLES)
  endif ()
  target_sources(aurora PRIVATE lib/dawn/OpenGLBinding.cpp)
endif ()
if (DAWN_ENABLE_NULL)
  target_compile_definitions(aurora PRIVATE DAWN_ENABLE_BACKEND_NULL)
  target_sources(aurora PRIVATE lib/dawn/NullBinding.cpp)
endif ()

add_library(aurora_main STATIC lib/main.cpp)
target_include_directories(aurora_main PUBLIC include)
target_link_libraries(aurora_main PUBLIC SDL2::SDL2main)
add_library(aurora::main ALIAS aurora_main)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(examples)
endif ()

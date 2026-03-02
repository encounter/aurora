cmake_minimum_required(VERSION 3.30)
project(aurora LANGUAGES C CXX)
if (APPLE)
    enable_language(OBJC)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

option(AURORA_ENABLE_GX "Enable GX implementation and WebGPU renderer" ON)

add_subdirectory(extern)

include(cmake/aurora_core.cmake)
if (AURORA_ENABLE_GX)
  include(cmake/aurora_gx.cmake)
endif ()
include(cmake/aurora_pad.cmake)
include(cmake/aurora_si.cmake)
include(cmake/aurora_main.cmake)
include(cmake/aurora_mtx.cmake)
include(cmake/aurora_vi.cmake)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(examples)
endif ()

if (NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif ()

# =============================================================================
# aurora_shared: Single SHARED library bundling all Aurora components.
# Used for DLL isolation — keeps JSystem's global operator new/delete
# confined to the game shared library, away from Dawn/SDL allocations.
# =============================================================================
add_library(aurora_shared SHARED
  # aurora_core sources
  lib/aurora.cpp
  lib/input.cpp
  lib/window.cpp
  lib/logging.cpp
  # aurora_si sources
  lib/dolphin/si/si.cpp
  # aurora_pad sources
  lib/dolphin/pad/pad.cpp
  # aurora_vi sources
  lib/dolphin/vi/vi.cpp
  # aurora_mtx sources
  lib/dolphin/mtx/mtx.c
  lib/dolphin/mtx/mtxstack.c
  lib/dolphin/mtx/mtxvec.c
  lib/dolphin/mtx/mtx44.c
  lib/dolphin/mtx/vec.c
  lib/dolphin/mtx/quat.c
)
add_library(aurora::shared ALIAS aurora_shared)

target_compile_definitions(aurora_shared PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_shared PUBLIC include)
target_link_libraries(aurora_shared PUBLIC SDL3::SDL3-static fmt::fmt xxhash)
target_link_libraries(aurora_shared PRIVATE absl::btree absl::flat_hash_map)

if (AURORA_ENABLE_GX)
  target_sources(aurora_shared PRIVATE
    # aurora_core GX sources
    lib/imgui.cpp
    lib/webgpu/gpu.cpp
    # aurora_gx sources
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
  target_compile_definitions(aurora_shared PUBLIC AURORA_ENABLE_GX)
  # imgui code is compiled into aurora_shared; link PRIVATE to avoid duplicating the static lib
  # into downstream targets. Propagate only the include directories for header access.
  target_link_libraries(aurora_shared PRIVATE imgui)
  target_include_directories(aurora_shared PUBLIC $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
  if (EMSCRIPTEN)
    target_link_options(aurora_shared PUBLIC -sUSE_WEBGPU=1 -sASYNCIFY -sEXIT_RUNTIME)
    target_compile_definitions(aurora_shared PRIVATE ENABLE_BACKEND_WEBGPU)
  else ()
    target_link_libraries(aurora_shared PRIVATE dawn::webgpu_dawn)
    target_sources(aurora_shared PRIVATE lib/dawn/BackendBinding.cpp)
    target_compile_definitions(aurora_shared PRIVATE WEBGPU_DAWN)
  endif ()
  if (DAWN_ENABLE_VULKAN)
    target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_VULKAN)
    target_link_libraries(aurora_shared PRIVATE Vulkan::Headers)
  endif ()
  if (DAWN_ENABLE_METAL)
    target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_METAL)
    target_sources(aurora_shared PRIVATE lib/dawn/MetalBinding.mm)
    set_source_files_properties(lib/dawn/MetalBinding.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
  endif ()
  if (DAWN_ENABLE_D3D11)
    target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_D3D11)
  endif ()
  if (DAWN_ENABLE_D3D12)
    target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_D3D12)
  endif ()
  if (DAWN_ENABLE_DESKTOP_GL OR DAWN_ENABLE_OPENGLES)
    target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_OPENGL)
    if (DAWN_ENABLE_DESKTOP_GL)
      target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_DESKTOP_GL)
    endif ()
    if (DAWN_ENABLE_OPENGLES)
      target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_OPENGLES)
    endif ()
  endif ()
  if (DAWN_ENABLE_NULL)
    target_compile_definitions(aurora_shared PRIVATE DAWN_ENABLE_BACKEND_NULL)
  endif ()
endif ()

# Export all symbols from the aurora shared library.
# This is safe because aurora has no global operator new/delete overrides.
set_target_properties(aurora_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

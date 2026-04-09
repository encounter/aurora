cmake_minimum_required(VERSION 3.25)
project(aurora LANGUAGES C CXX)
if (APPLE)
    enable_language(OBJC)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(AURORA_ENABLE_GX "Enable GX implementation and WebGPU renderer" ON)
option(AURORA_ENABLE_DVD "Enable DVD implementation backed by nod" OFF)  # TODO: need to fix tests
option(AURORA_ENABLE_CARD "Enable CARD implementation based on kabufuda" ON)
option(AURORA_CACHE_USE_ZSTD "Compress WebGPU cache entries with zstd" ON)

# Dependency versions
set(AURORA_DAWN_VERSION "v20260309.222352" CACHE STRING "Dawn version tag (https://github.com/encounter/dawn-build/releases)")
set(AURORA_SDL3_VERSION "3.4.2" CACHE STRING "SDL3 version tag (https://github.com/libsdl-org/SDL/releases)")
set(AURORA_NOD_VERSION "v2.0.0-alpha.8" CACHE STRING "nod version tag (https://github.com/encounter/nod/releases)")

# Platform-specific defaults
if (CMAKE_CROSSCOMPILING OR EMSCRIPTEN)
  # Cross-compiling: vendor everything, don't use system packages
  set(_default_provider "vendor")
  set(_default_linkage "static")
else ()
  set(_default_provider "auto")
  if (WIN32)
    set(_default_linkage "shared")
  else ()
    set(_default_linkage "static")
  endif ()
endif ()

# Dawn/WebGPU (if AURORA_ENABLE_GX)
set(AURORA_DAWN_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide Dawn: auto, vendor (build from source), system (find_package/imported), package (download prebuilt)")
set_property(CACHE AURORA_DAWN_PROVIDER PROPERTY STRINGS auto vendor system package)
set(AURORA_DAWN_LINKAGE "${_default_linkage}" CACHE STRING "Dawn linkage type preference")
set_property(CACHE AURORA_DAWN_LINKAGE PROPERTY STRINGS shared static)

# SDL3
set(AURORA_SDL3_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide SDL3: auto, vendor (build from source), system (find_package/imported), or package (download prebuilt)")
set_property(CACHE AURORA_SDL3_PROVIDER PROPERTY STRINGS auto vendor system package)
set(AURORA_SDL3_LINKAGE "${_default_linkage}" CACHE STRING "SDL3 linkage type preference")
set_property(CACHE AURORA_SDL3_LINKAGE PROPERTY STRINGS shared static)

# nod (if AURORA_ENABLE_DVD)
set(AURORA_NOD_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide nod: auto, vendor (build from source), system (find_package/imported), package (download prebuilt)")
set_property(CACHE AURORA_NOD_PROVIDER PROPERTY STRINGS auto vendor system package)
set(AURORA_NOD_LINKAGE "${_default_linkage}" CACHE STRING "nod linkage type preference")
set_property(CACHE AURORA_NOD_LINKAGE PROPERTY STRINGS shared static)

add_subdirectory(extern)

include(cmake/aurora_core.cmake)
include(cmake/aurora_os.cmake)
if (AURORA_ENABLE_GX)
  include(cmake/aurora_gx.cmake)
  include(cmake/aurora_gd.cmake)
endif ()
include(cmake/aurora_pad.cmake)
include(cmake/aurora_si.cmake)
include(cmake/aurora_main.cmake)
include(cmake/aurora_mtx.cmake)
include(cmake/aurora_vi.cmake)
if(AURORA_ENABLE_CARD)
  include(cmake/aurora_card.cmake)
endif()
if (AURORA_ENABLE_DVD)
  include(cmake/aurora_dvd.cmake)
endif ()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(examples)
endif ()

if (NOT CMAKE_CROSSCOMPILING)
  enable_testing()
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
target_link_libraries(aurora_shared PUBLIC ${AURORA_SDL3_TARGET} fmt::fmt xxhash)
target_link_libraries(aurora_shared PRIVATE absl::btree absl::flat_hash_map)

if (AURORA_ENABLE_DVD)
  target_sources(aurora_shared PRIVATE lib/dolphin/dvd/dvd.cpp)
  target_link_libraries(aurora_shared PRIVATE nod::nod)
endif ()

if (AURORA_ENABLE_GX)
  target_sources(aurora_shared PRIVATE
    # aurora_core GX sources
    lib/imgui.cpp
    lib/webgpu/gpu.cpp
    lib/webgpu/gpu_cache.cpp
    # aurora_gx sources
    lib/gfx/clear.cpp
    lib/gfx/common.cpp
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
  target_link_libraries(aurora_shared PRIVATE imgui sqlite3)
  if (AURORA_CACHE_USE_ZSTD)
    target_compile_definitions(aurora_shared PRIVATE AURORA_CACHE_USE_ZSTD)
    target_link_libraries(aurora_shared PRIVATE libzstd_static)
  endif ()
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

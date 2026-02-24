# Third-party dependencies via CPM
# https://github.com/cpm-cmake/CPM.cmake

# Don't build tests for third party dependencies
set(BUILD_TESTING OFF CACHE INTERNAL "Build tests")

# ==== Dawn (WebGPU) ====
if (NOT EMSCRIPTEN AND AURORA_ENABLE_GX)
  CPMAddPackage(
    NAME dawn
    GITHUB_REPOSITORY google/dawn
    VERSION 20260214.164635
    EXCLUDE_FROM_ALL YES
    OPTIONS
      "DAWN_BUILD_MONOLITHIC_LIBRARY STATIC"
      "DAWN_BUILD_SAMPLES OFF"
      "DAWN_BUILD_BENCHMARKS OFF"
      "DAWN_SUPPORTS_GLFW_FOR_WINDOWING OFF"
      "DAWN_ENABLE_DESKTOP_GL OFF"
      "DAWN_ENABLE_OPENGLES OFF"
      "DAWN_FETCH_DEPENDENCIES ON"
      "TINT_BUILD_TESTS OFF"
      "TINT_BUILD_CMD_TOOLS OFF"
  )
endif ()

# ==== Abseil ====
# When GX is enabled, abseil comes via Dawn; otherwise fetch standalone
if (NOT AURORA_ENABLE_GX)
  if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL$")
    set(_absl_static_runtime OFF)
  else()
    set(_absl_static_runtime ON)
  endif()
  CPMAddPackage(
    NAME abseil-cpp
    GITHUB_REPOSITORY abseil/abseil-cpp
    GIT_TAG 20240722.0
    EXCLUDE_FROM_ALL YES
    OPTIONS
      "ABSL_PROPAGATE_CXX_STD ON"
      "ABSL_MSVC_STATIC_RUNTIME ${_absl_static_runtime}"
  )
endif ()

# ==== SDL3 ====
if (NOT EMSCRIPTEN)
  set(_sdl_options "SDL_STATIC ON")
  if (WIN32)
    list(APPEND _sdl_options "SDL_LIBC ON")
  endif ()
  CPMAddPackage(
    NAME SDL
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG release-3.4.0
    EXCLUDE_FROM_ALL YES
    OPTIONS ${_sdl_options}
  )
endif ()

# ==== xxHash ====
CPMAddPackage(
  NAME xxhash
  GITHUB_REPOSITORY Cyan4973/xxHash
  VERSION 0.8.3
  EXCLUDE_FROM_ALL YES
  SOURCE_SUBDIR cmake_unofficial
  OPTIONS
    "XXHASH_BUILD_XXHSUM OFF"
)

# ==== fmt ====
CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 11.1.4
  EXCLUDE_FROM_ALL YES
)

# ==== Dear ImGui ====
if (AURORA_ENABLE_GX)
  CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.91.9b-docking
    DOWNLOAD_ONLY YES
  )

  if (imgui_ADDED)
    add_library(imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

    add_library(imgui_backends STATIC
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
    )
    target_compile_definitions(imgui_backends PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    target_link_libraries(imgui_backends PRIVATE imgui SDL3::SDL3-static dawn::webgpu_dawn)
    target_link_libraries(imgui PUBLIC imgui_backends)

    # Optional: use FreeType instead of stb_truetype
    find_package(Freetype)
    option(IMGUI_USE_FREETYPE "Enable freetype with imgui" ON)
    if (FREETYPE_FOUND AND IMGUI_USE_FREETYPE)
      target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
      target_compile_definitions(imgui PRIVATE IMGUI_ENABLE_FREETYPE)
      target_link_libraries(imgui PRIVATE Freetype::Freetype)
    endif ()
  endif ()
endif ()

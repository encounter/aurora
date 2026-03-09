include(FetchContent)

# Don't build tests for third party dependencies
set(BUILD_TESTING OFF CACHE INTERNAL "Build tests")

if (NOT EMSCRIPTEN)
  if (AURORA_ENABLE_GX)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AuroraDawnProvider.cmake)
  endif ()

  # Abseil is needed for core libraries. It normally comes via Dawn's vendor build,
  # but when using prebuilt/system Dawn, we need to fetch it separately.
  if (NOT TARGET absl::flat_hash_map)
    message(STATUS "aurora: Fetching abseil-cpp (standalone)")
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL$")
      set(ABSL_MSVC_STATIC_RUNTIME OFF CACHE INTERNAL "Link static runtime libraries")
    else()
      set(ABSL_MSVC_STATIC_RUNTIME ON CACHE INTERNAL "Link static runtime libraries")
    endif()
    set(ABSL_PROPAGATE_CXX_STD ON)
    FetchContent_Declare(abseil-cpp
      URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(abseil-cpp)
    if (NOT TARGET absl::flat_hash_map)
      message(FATAL_ERROR "Failed to make abseil-cpp available")
    endif ()
  endif ()

  include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AuroraSDL3Provider.cmake)
else ()
  set(ABSL_PROPAGATE_CXX_STD ON)
  add_subdirectory(dawn/third_party/abseil-cpp EXCLUDE_FROM_ALL)
  set(AURORA_SDL3_TARGET SDL3::SDL3-static CACHE INTERNAL "SDL3 target for emscripten")
endif ()

if (NOT TARGET xxhash)
  message(STATUS "aurora: Fetching xxhash")
  FetchContent_Declare(xxhash
    URL https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz
    URL_HASH SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
    SOURCE_SUBDIR cmake_unofficial
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  set(XXHASH_BUILD_XXHSUM OFF CACHE INTERNAL "Build the xxhsum binary")
  FetchContent_MakeAvailable(xxhash)
  if (NOT TARGET xxhash)
    message(FATAL_ERROR "Failed to make xxhash available")
  endif ()
else ()
  message(STATUS "aurora: Using existing xxhash")
endif ()

if (NOT TARGET fmt)
  message(STATUS "aurora: Fetching fmt")
  FetchContent_Declare(fmt
    URL https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.tar.gz
    URL_HASH SHA256=ac366b7b4c2e9f0dde63a59b3feb5ee59b67974b14ee5dc9ea8ad78aa2c1ee1e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(fmt)
  if (NOT TARGET fmt)
    message(FATAL_ERROR "Failed to make fmt available")
  endif ()
else ()
  message(STATUS "aurora: Using existing fmt")
endif ()

if (AURORA_ENABLE_GX)
  if (NOT TARGET imgui)
    message(STATUS "aurora: Fetching imgui")
    FetchContent_Declare(imgui
      URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
      URL_HASH SHA256=466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(imgui)

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
    target_link_libraries(imgui_backends PRIVATE imgui ${AURORA_SDL3_TARGET} dawn::webgpu_dawn)
    target_link_libraries(imgui PUBLIC imgui_backends)

    # Optional, replaces stb_freetype if available
    find_package(Freetype)
    # Permit disabling for macOS universal builds
    option(IMGUI_USE_FREETYPE "Enable freetype with imgui" ON)
    if (FREETYPE_FOUND AND IMGUI_USE_FREETYPE)
      target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
      target_compile_definitions(imgui PRIVATE IMGUI_ENABLE_FREETYPE)
      target_link_libraries(imgui PRIVATE Freetype::Freetype)
    endif ()
  else ()
    message(STATUS "aurora: Using existing imgui")
  endif ()
endif ()

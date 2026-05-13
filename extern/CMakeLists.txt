include(FetchContent)

# Don't build tests for third party dependencies
set(BUILD_TESTING OFF CACHE INTERNAL "Build tests")

if (NOT EMSCRIPTEN)
  if (AURORA_ENABLE_GX)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AuroraDawnProvider.cmake)
  endif ()

  # Abseil is needed for core libraries. It normally comes via Dawn's vendor build.
  # Otherwise prefer a system package and only fetch it as a last resort.
  if (NOT TARGET absl::flat_hash_map OR NOT TARGET absl::btree)
    find_package(absl CONFIG QUIET GLOBAL)
  endif ()

  if (TARGET absl::flat_hash_map AND TARGET absl::btree)
    message(STATUS "aurora: Using system/existing abseil")
  else ()
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
    if (NOT TARGET absl::flat_hash_map OR NOT TARGET absl::btree)
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
  # xxhash uses CMAKE_DEPENDENT_OPTION(BUILD_SHARED_LIBS ...) which writes
  # a sticky CACHE INTERNAL entry on reconfigure. Undo it afterward.
  set(_aurora_xxhash_saved_bsl "${BUILD_SHARED_LIBS}")
  FetchContent_MakeAvailable(xxhash)
  unset(BUILD_SHARED_LIBS CACHE)
  set(BUILD_SHARED_LIBS "${_aurora_xxhash_saved_bsl}")
  unset(_aurora_xxhash_saved_bsl)
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
      target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
      target_link_libraries(imgui PRIVATE Freetype::Freetype)
    endif ()
  else ()
    message(STATUS "aurora: Using existing imgui")
  endif ()

  if (NOT TARGET sqlite3)
    message(STATUS "aurora: Fetching sqlite3")
    FetchContent_Declare(sqlite3
      URL https://sqlite.org/2026/sqlite-amalgamation-3510300.zip
      URL_HASH SHA256=acb1e6f5d832484bf6d32b681e858c38add8b2acdfd42ac5df24b8afb46552b4
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(sqlite3)

    add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
  else ()
    message(STATUS "aurora: Using existing sqlite3")
  endif ()

  if (AURORA_CACHE_USE_ZSTD)
    if (NOT TARGET libzstd_static)
      message(STATUS "aurora: Fetching zstd")
      set(ZSTD_BUILD_STATIC ON)
      set(ZSTD_BUILD_SHARED OFF)

      include(FetchContent)
      FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR build/cmake
        EXCLUDE_FROM_ALL
      )
      FetchContent_MakeAvailable(zstd)
    else ()
      message(STATUS "aurora: Using existing zstd")
    endif ()
  endif ()

  if(AURORA_ENABLE_RMLUI AND NOT TARGET RmlUi)
    if(NOT TARGET Freetype::Freetype)
      message(STATUS "aurora: Fetching Freetype")
      FetchContent_Declare(Freetype
              URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.gz
              URL_HASH SHA256=e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1
              DOWNLOAD_EXTRACT_TIMESTAMP TRUE
              EXCLUDE_FROM_ALL
      )
      FetchContent_MakeAvailable(Freetype)

      if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
      endif()
    else()
      message(STATUS "aurora: Using existing Freetype")
    endif()

    message(STATUS "aurora: Fetching RmlUi")
    FetchContent_Declare(rmlui
            URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz
            URL_HASH SHA256=814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(rmlui)

    add_library(rmlui_backends STATIC
            ${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp
    )
    # TODO: maybe dont hardcode this to SDL3?
    target_compile_definitions(rmlui_backends PUBLIC RMLUI_SDL_VERSION_MAJOR=3)

    target_include_directories(rmlui_backends PUBLIC
            ${rmlui_SOURCE_DIR}/Backends
    )

    target_link_libraries(rmlui_backends PRIVATE rmlui ${AURORA_SDL3_TARGET})

  endif ()

endif ()

if (NOT TARGET TracyClient)
  message(STATUS "aurora: Fetching Tracy")

  # Default to off, users can enable the cache variable if they want to use it.
  set(TRACY_ENABLE OFF CACHE BOOL "Enable Tracy profiling")

  include(FetchContent)
  FetchContent_Declare(
          tracy
          URL https://github.com/wolfpld/tracy/archive/a64b9a20294d59421a2f57aeca3c6383d8c48169.tar.gz
          URL_HASH SHA256=24d342b5127d7f659dc3cf94f24347b348cc736f25b17a691fd7f69541937658
          DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(tracy)

  if (NOT TARGET TracyClient)
    message(FATAL_ERROR "Failed to make TracyClient available")
  endif ()
endif ()

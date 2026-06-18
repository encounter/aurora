include(FetchContent)

# Don't build tests for third party dependencies
set(BUILD_TESTING OFF CACHE INTERNAL "Build tests")
set(SKIP_INSTALL_ALL ON)
if (NOT DEFINED BUILD_SHARED_LIBS)
  set(_USE_SHARED ON)
else ()
  set(_USE_SHARED OFF)
endif ()

function(aurora_find_package_global)
  set(_PREV_FIND_PACKAGE_TARGETS_GLOBAL ${CMAKE_FIND_PACKAGE_TARGETS_GLOBAL})
  set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
  find_package(${ARGV})
  set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ${_PREV_FIND_PACKAGE_TARGETS_GLOBAL})
endfunction()

if (AURORA_ENABLE_GX)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AuroraDawnProvider.cmake)
endif ()

# Abseil is needed for core libraries. It normally comes via Dawn's vendor build.
# Otherwise prefer a system package and only fetch it as a last resort.
if (NOT TARGET absl::flat_hash_map OR NOT TARGET absl::btree)
  aurora_find_package_global(absl CONFIG QUIET GLOBAL)
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
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(abseil-cpp)
  if (NOT TARGET absl::flat_hash_map OR NOT TARGET absl::btree)
    message(FATAL_ERROR "Failed to make abseil-cpp available")
  endif ()
endif ()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AuroraSDL3Provider.cmake)

if (NOT TARGET xxhash)
  message(STATUS "aurora: Fetching xxhash")
  FetchContent_Declare(xxhash
    URL https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz
    URL_HASH SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
    SOURCE_SUBDIR cmake_unofficial
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    EXCLUDE_FROM_ALL
  )
  set(XXHASH_BUILD_XXHSUM OFF CACHE INTERNAL "Build the xxhsum binary")
  # xxhash uses CMAKE_DEPENDENT_OPTION(BUILD_SHARED_LIBS ...) which writes
  # a sticky CACHE INTERNAL entry on reconfigure. Undo it afterward, but keep
  # an existing preset/toolchain cache entry intact for build-time regenerates.
  set(_aurora_xxhash_had_bsl_var OFF)
  set(_aurora_xxhash_had_bsl_cache OFF)
  if (DEFINED BUILD_SHARED_LIBS)
    set(_aurora_xxhash_had_bsl_var ON)
    set(_aurora_xxhash_saved_bsl "${BUILD_SHARED_LIBS}")
  endif ()
  if (DEFINED CACHE{BUILD_SHARED_LIBS})
    set(_aurora_xxhash_had_bsl_cache ON)
  endif ()
  FetchContent_MakeAvailable(xxhash)
  unset(BUILD_SHARED_LIBS CACHE)
  if (_aurora_xxhash_had_bsl_cache)
    set(BUILD_SHARED_LIBS "${_aurora_xxhash_saved_bsl}" CACHE BOOL "Build shared libraries" FORCE)
  elseif (_aurora_xxhash_had_bsl_var)
    set(BUILD_SHARED_LIBS "${_aurora_xxhash_saved_bsl}")
  else ()
    unset(BUILD_SHARED_LIBS)
  endif ()
  unset(_aurora_xxhash_had_bsl_cache)
  unset(_aurora_xxhash_had_bsl_var)
  unset(_aurora_xxhash_saved_bsl)
  if (NOT TARGET xxhash)
    message(FATAL_ERROR "Failed to make xxhash available")
  endif ()
else ()
  message(STATUS "aurora: Using existing xxhash")
endif ()

if (NOT TARGET fmt::fmt)
  aurora_find_package_global(fmt 11 QUIET)
  if (NOT TARGET fmt::fmt)
    message(STATUS "aurora: Fetching fmt")
    FetchContent_Declare(fmt
      URL https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz
      URL_HASH SHA256=ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(fmt)
    if (NOT TARGET fmt)
      message(FATAL_ERROR "Failed to make fmt available")
    endif ()
  else ()
    message(STATUS "aurora: Using existing fmt")
  endif ()
endif ()

if (AURORA_ENABLE_GX)
  if (NOT TARGET ZLIB::ZLIB)
    aurora_find_package_global(ZLIB QUIET)
  endif ()
  if (NOT TARGET ZLIB::ZLIB)
    message(STATUS "aurora: Fetching zlib")
    set(ZLIB_BUILD_SHARED ${_USE_SHARED} CACHE BOOL "Enable building zlib shared library" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Enable Zlib Examples as tests" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "Enable installation of zlib" FORCE)
    set(ZLIB_COMPAT ON CACHE BOOL "Use the compat version of zlib instead of the newer zlib-ng" FORCE)
    FetchContent_Declare(
      ZLIB
      URL https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.3.3.tar.gz
      URL_HASH SHA256=f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlib-ng)
    # libpng calls find_package(ZLIB) even when zlib was already provided by FetchContent.
    file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/ZLIBConfig.cmake"
      "set(ZLIB_FOUND TRUE)\n"
      "set(ZLIB_INCLUDE_DIR \"${zlib_SOURCE_DIR}\")\n"
      "set(ZLIB_INCLUDE_DIRS \"${zlib_BINARY_DIR};${zlib_SOURCE_DIR}\")\n"
    )
  endif ()

  if (NOT TARGET PNG::PNG)
    if (TARGET png_shared AND _USE_SHARED)
      add_library(PNG::PNG ALIAS png_shared)
    elseif (TARGET png_static)
      add_library(PNG::PNG ALIAS png_static)
    else ()
      aurora_find_package_global(PNG QUIET)
    endif ()
  endif ()
  if (NOT TARGET PNG::PNG)
    message(STATUS "aurora: Fetching libpng")
    set(PNG_SHARED ${_USE_SHARED} CACHE BOOL "Build libpng as a shared library" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "Build the libpng tests" FORCE)
    set(PNG_TOOLS OFF CACHE BOOL "Build the libpng tools" FORCE)
    FetchContent_Declare(
      PNG
      URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.58.tar.gz
      URL_HASH SHA256=A9D4DF463D36A6E5F9C29BD6F4967312D17E996C1854F3511F833924EB1993CF
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(PNG)
    if (TARGET png_shared AND _USE_SHARED)
      add_library(PNG::PNG ALIAS png_shared)
    elseif (TARGET png_static)
      add_library(PNG::PNG ALIAS png_static)
    else ()
      message(FATAL_ERROR "Failed to make libpng available")
    endif ()
  else ()
    message(STATUS "aurora: Using existing libpng")
  endif ()

  aurora_find_package_global(Freetype QUIET)
  if (NOT TARGET Freetype::Freetype)
    message(STATUS "aurora: Fetching Freetype")
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    FetchContent_Declare(Freetype
      URL https://files.twilitrealm.dev/freetype-2.14.3.tar.gz
      URL_HASH SHA256=e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  else ()
    message(STATUS "aurora: Using existing Freetype")
  endif ()

  if (NOT TARGET imgui)
    message(STATUS "aurora: Fetching imgui")
    FetchContent_Declare(imgui
      URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
      URL_HASH SHA256=466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
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

    if (TARGET Freetype::Freetype)
      target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
      target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
      target_link_libraries(imgui PRIVATE Freetype::Freetype)
    endif ()
  else ()
    message(STATUS "aurora: Using existing imgui")
  endif ()

  if (TARGET imgui)
    target_include_directories(imgui PUBLIC ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="aurora/imgui_config.h")
    target_sources(imgui PRIVATE ${PROJECT_SOURCE_DIR}/lib/imgui_config.cpp)
    target_link_libraries(imgui PRIVATE ${AURORA_SDL3_TARGET})
  endif ()

  aurora_find_package_global(SQLite3)
  if (TARGET SQLite3::SQLite3)
    message(STATUS "aurora: Using existing sqlite3")
    add_library(sqlite3 ALIAS SQLite3::SQLite3)
  elseif (TARGET SQLite::SQLite3) # CMake < 4.3
    message(STATUS "aurora: Using existing sqlite3")
    add_library(sqlite3 ALIAS SQLite::SQLite3)
  else ()
    message(STATUS "aurora: Fetching sqlite3")
    FetchContent_Declare(sqlite3
      URL https://sqlite.org/2026/sqlite-amalgamation-3510300.zip
      URL_HASH SHA256=acb1e6f5d832484bf6d32b681e858c38add8b2acdfd42ac5df24b8afb46552b4
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(sqlite3)

    add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
  endif ()

  if (AURORA_CACHE_USE_ZSTD)
    aurora_find_package_global(zstd CONFIG QUIET)
    if (NOT TARGET zstd::libzstd)
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(ZSTD libzstd IMPORTED_TARGET GLOBAL)
        if(ZSTD_FOUND)
          add_library(zstd::libzstd ALIAS PkgConfig::ZSTD)
        endif()
      endif()
    endif()
    if (NOT TARGET zstd::libzstd)
      message(STATUS "aurora: Fetching zstd")
      set(ZSTD_BUILD_STATIC ON)
      set(ZSTD_BUILD_SHARED OFF)

      FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR build/cmake
        EXCLUDE_FROM_ALL
      )
      FetchContent_MakeAvailable(zstd)
      add_library(zstd::libzstd ALIAS libzstd_static)
    else ()
      message(STATUS "aurora: Using existing zstd")
    endif ()
  endif ()

  if (AURORA_ENABLE_RMLUI AND NOT TARGET RmlUi)
    message(STATUS "aurora: Fetching RmlUi")
    FetchContent_Declare(rmlui
      URL https://github.com/mikke89/RmlUi/archive/f9b8c9e2935d5df2c7dff2c190d3968e99b0c3dc.tar.gz
      URL_HASH SHA256=42b830abc2509a8c07cf02ba3313505de3a4642725654f489402022d8fefdd79
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DRMLUI_SOURCE_DIR=<SOURCE_DIR>
        -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/patches/apply-rmlui-keyboard-showed-state.cmake"
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(rmlui)

    add_library(rmlui_backends STATIC ${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp)
    # TODO: maybe dont hardcode this to SDL3?
    target_compile_definitions(rmlui_backends PUBLIC RMLUI_SDL_VERSION_MAJOR=3)
    target_include_directories(rmlui_backends PUBLIC ${rmlui_SOURCE_DIR}/Backends)
    target_link_libraries(rmlui_backends PRIVATE rmlui ${AURORA_SDL3_TARGET})
  endif ()
endif ()

if (NOT TARGET TracyClient)
  message(STATUS "aurora: Fetching Tracy")

  # Default to off, users can enable the cache variable if they want to use it.
  set(TRACY_ENABLE OFF CACHE BOOL "Enable Tracy profiling")
  set(TRACY_ON_DEMAND ON CACHE BOOL "On-demand profiling")
  set(TRACY_STATIC ON CACHE BOOL "Whether to build Tracy as a static library" FORCE)

  FetchContent_Declare(
          tracy
          URL https://github.com/wolfpld/tracy/archive/6789e7d6f9a65ec98926b602097a33a9676d2606.tar.gz
          URL_HASH SHA256=ebfe4fb50d7c254901979355c80a7d4cd33624aa2ec0fe90b3b238153cd5d69b
          DOWNLOAD_EXTRACT_TIMESTAMP FALSE
          EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(tracy)

  if (NOT TARGET TracyClient)
    message(FATAL_ERROR "Failed to make TracyClient available")
  endif ()
endif ()

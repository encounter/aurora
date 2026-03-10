include_guard(GLOBAL)

# Resolve SDL3 dependency based on AURORA_SDL3_PROVIDER and AURORA_SDL3_LINKAGE.
#
# After this module runs, the variable AURORA_SDL3_TARGET is set to the
# appropriate target name for use in target_link_libraries() calls.

# Select the SDL3 target matching the requested linkage.
function(_aurora_sdl3_select_target)
  if (AURORA_SDL3_LINKAGE STREQUAL "static")
    if (TARGET SDL3::SDL3-static)
      set(AURORA_SDL3_TARGET SDL3::SDL3-static CACHE INTERNAL "")
      message(STATUS "aurora: Using static SDL3")
      return()
    elseif (TARGET SDL3::SDL3)
      message(WARNING "aurora: Static SDL3 requested but only shared is available")
      set(AURORA_SDL3_TARGET SDL3::SDL3 CACHE INTERNAL "")
      message(STATUS "aurora: Using shared SDL3")
      return()
    endif ()
  else ()
    if (TARGET SDL3::SDL3)
      set(AURORA_SDL3_TARGET SDL3::SDL3 CACHE INTERNAL "")
      message(STATUS "aurora: Using shared SDL3")
      return()
    elseif (TARGET SDL3::SDL3-static)
      message(WARNING "aurora: Shared SDL3 requested but only static is available")
      set(AURORA_SDL3_TARGET SDL3::SDL3-static CACHE INTERNAL "")
      message(STATUS "aurora: Using static SDL3")
      return()
    endif ()
  endif ()
  message(FATAL_ERROR "No SDL3 target found (looked for SDL3::SDL3 and SDL3::SDL3-static)")
endfunction()

# ── Auto: resolve provider based on platform availability ──
set(_aurora_sdl3_provider "${AURORA_SDL3_PROVIDER}")
if (_aurora_sdl3_provider STREQUAL "auto")
  # Prebuilt SDL3 packages are available for Windows (MSVC and MinGW)
  if (WIN32)
    set(_aurora_sdl3_provider "package")
  else ()
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    find_package(SDL3 QUIET)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
    if (SDL3_FOUND)
      set(_aurora_sdl3_provider "system")
    else ()
      set(_aurora_sdl3_provider "vendor")
    endif ()
  endif ()
  message(STATUS "aurora: SDL3 auto-resolved provider: ${_aurora_sdl3_provider}")
endif ()

if (_aurora_sdl3_provider STREQUAL "system")
  # ── System: find_package(SDL3) ──
  message(STATUS "aurora: Using system SDL3 (provider=system)")
  if (NOT SDL3_FOUND)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    find_package(SDL3 REQUIRED)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
  endif ()
  _aurora_sdl3_select_target()

elseif (_aurora_sdl3_provider STREQUAL "package")
  # ── Package: download official SDL3 development package ──
  if (NOT AURORA_SDL3_PACKAGE_URL)
    if (WIN32)
      set(AURORA_SDL3_PACKAGE_URL
        "https://github.com/libsdl-org/SDL/releases/download/release-${AURORA_SDL3_VERSION}/SDL3-devel-${AURORA_SDL3_VERSION}-VC.zip")
    elseif (MINGW)
      set(AURORA_SDL3_PACKAGE_URL
        "https://github.com/libsdl-org/SDL/releases/download/release-${AURORA_SDL3_VERSION}/SDL3-devel-${AURORA_SDL3_VERSION}-mingw.tar.gz")
    else ()
      message(FATAL_ERROR
        "AURORA_SDL3_PROVIDER=package requires AURORA_SDL3_PACKAGE_URL on non-Windows platforms.\n"
        "Official SDL3 packages: https://github.com/libsdl-org/SDL/releases")
    endif ()
  endif ()
  message(STATUS "aurora: Fetching prebuilt SDL3 package from ${AURORA_SDL3_PACKAGE_URL}")

  include(FetchContent)
  FetchContent_Declare(sdl3_prebuilt
    URL "${AURORA_SDL3_PACKAGE_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(sdl3_prebuilt)

  set(_sdl3_search_paths
    "${sdl3_prebuilt_SOURCE_DIR}/cmake"
    "${sdl3_prebuilt_SOURCE_DIR}/lib/cmake/SDL3"
    "${sdl3_prebuilt_SOURCE_DIR}"
  )
  file(GLOB _sdl3_subdirs "${sdl3_prebuilt_SOURCE_DIR}/SDL3-*")
  foreach (_sd IN LISTS _sdl3_subdirs)
    list(APPEND _sdl3_search_paths
      "${_sd}/cmake"
      "${_sd}/lib/cmake/SDL3"
      "${_sd}"
    )
  endforeach ()

  set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
  find_package(SDL3 REQUIRED CONFIG PATHS ${_sdl3_search_paths} NO_DEFAULT_PATH)
  set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
  _aurora_sdl3_select_target()

elseif (_aurora_sdl3_provider STREQUAL "vendor")
  # ── Vendor: FetchContent build from source ──
  if (NOT TARGET SDL3-static AND NOT TARGET SDL3-shared)
    message(STATUS "aurora: Fetching SDL3 (provider=vendor)")

    if (AURORA_SDL3_LINKAGE STREQUAL "static")
      set(SDL_STATIC ON CACHE BOOL "Build SDL3 static library" FORCE)
      set(SDL_SHARED OFF CACHE BOOL "Build SDL3 shared library" FORCE)
    else ()
      set(SDL_STATIC OFF CACHE BOOL "Build SDL3 static library" FORCE)
      set(SDL_SHARED ON CACHE BOOL "Build SDL3 shared library" FORCE)
    endif ()
    if (WIN32)
      set(SDL_LIBC ON CACHE BOOL "Use the system C library" FORCE)
    endif ()

    include(FetchContent)
    FetchContent_Declare(SDL
      URL "https://github.com/libsdl-org/SDL/releases/download/release-${AURORA_SDL3_VERSION}/SDL3-${AURORA_SDL3_VERSION}.tar.gz"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL)
  else ()
    message(STATUS "aurora: Using existing SDL3")
  endif ()
  _aurora_sdl3_select_target()

else ()
  message(FATAL_ERROR "Invalid AURORA_SDL3_PROVIDER: ${AURORA_SDL3_PROVIDER} "
    "(must be auto, vendor, system, or package)")
endif ()

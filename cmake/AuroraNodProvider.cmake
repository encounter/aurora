include_guard(GLOBAL)

# Resolve nod dependency based on AURORA_NOD_PROVIDER.
#
# After this module runs:
#   - nod::nod target is available
#
# AURORA_NOD_LINKAGE controls shared vs static linkage:
#   - "shared" (default): prefer nod::nod_shared
#   - "static": prefer nod::nod_static

if (AURORA_NOD_PROVIDER STREQUAL "vendor")
  # ── Vendor: FetchContent + Corrosion build from source ──
  if (NOT TARGET nod::nod)
    message(STATUS "aurora: Building nod (provider=vendor, linkage=${AURORA_NOD_LINKAGE})")

    # Control whether nod builds shared, static, or both.
    # Save/restore BUILD_SHARED_LIBS to avoid leaking into other subdirectories.
    set(_aurora_nod_saved_bsl "${BUILD_SHARED_LIBS}")
    if (AURORA_NOD_LINKAGE STREQUAL "static")
      set(BUILD_SHARED_LIBS OFF)
    else ()
      set(BUILD_SHARED_LIBS ON)
    endif ()

    include(FetchContent)
    FetchContent_Declare(aurora_nod
      GIT_REPOSITORY "https://github.com/encounter/nod.git"
      GIT_TAG "${AURORA_NOD_VERSION}"
      GIT_SHALLOW TRUE
      EXCLUDE_FROM_ALL
    )

    FetchContent_MakeAvailable(aurora_nod)
    set(BUILD_SHARED_LIBS "${_aurora_nod_saved_bsl}")
    unset(_aurora_nod_saved_bsl)
    if (NOT TARGET nod::nod)
      message(FATAL_ERROR "Failed to make nod available")
    endif ()
  else ()
    message(STATUS "aurora: Using existing nod")
  endif ()

elseif (AURORA_NOD_PROVIDER STREQUAL "system")
  # ── System: find preinstalled nod ──
  message(STATUS "aurora: Using system nod (provider=system, linkage=${AURORA_NOD_LINKAGE})")

  set(_aurora_nod_saved_bsl "${BUILD_SHARED_LIBS}")
  if (AURORA_NOD_LINKAGE STREQUAL "static")
    set(BUILD_SHARED_LIBS OFF)
  else ()
    set(BUILD_SHARED_LIBS ON)
  endif ()

  find_package(nod REQUIRED CONFIG)
  set(BUILD_SHARED_LIBS "${_aurora_nod_saved_bsl}")
  unset(_aurora_nod_saved_bsl)
  if (NOT TARGET nod::nod)
    message(FATAL_ERROR "find_package(nod) succeeded but nod::nod target not found")
  endif ()
  message(STATUS "aurora: Found nod via find_package(nod)")

elseif (AURORA_NOD_PROVIDER STREQUAL "package")
  # ── Package: download prebuilt nod library ──
  if (NOT AURORA_NOD_PACKAGE_URL)
    if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(_nod_platform "windows-x86_64")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
      set(_nod_platform "linux-x86_64")
    elseif (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
      set(_nod_platform "macos-arm64")
    else ()
      message(FATAL_ERROR
        "AURORA_NOD_PROVIDER=package requires AURORA_NOD_PACKAGE_URL on this platform.\n"
        "Prebuilt packages: https://github.com/encounter/nod/releases")
    endif ()
    set(AURORA_NOD_PACKAGE_URL
      "https://github.com/encounter/nod/releases/download/${AURORA_NOD_VERSION}/libnod-${_nod_platform}.tar.gz")
  endif ()
  message(STATUS "aurora: Fetching prebuilt nod package (provider=package, linkage=${AURORA_NOD_LINKAGE})")

  include(FetchContent)
  FetchContent_Declare(nod_prebuilt
    URL "${AURORA_NOD_PACKAGE_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(nod_prebuilt)

  # The prebuilt package ships a CMake config that creates nod::nod_shared,
  # nod::nod_static, and a default nod::nod (based on BUILD_SHARED_LIBS).
  # Set BUILD_SHARED_LIBS so the config selects the right default target.
  set(_aurora_nod_saved_bsl "${BUILD_SHARED_LIBS}")
  if (AURORA_NOD_LINKAGE STREQUAL "static")
    set(BUILD_SHARED_LIBS OFF)
  else ()
    set(BUILD_SHARED_LIBS ON)
  endif ()
  find_package(nod REQUIRED CONFIG
    PATHS "${nod_prebuilt_SOURCE_DIR}"
    NO_DEFAULT_PATH
  )
  set(BUILD_SHARED_LIBS "${_aurora_nod_saved_bsl}")
  unset(_aurora_nod_saved_bsl)
  if (NOT TARGET nod::nod)
    message(FATAL_ERROR "Prebuilt nod package did not provide nod::nod target")
  endif ()

else ()
  message(FATAL_ERROR "Invalid AURORA_NOD_PROVIDER: ${AURORA_NOD_PROVIDER} "
    "(must be vendor, system, or package)")
endif ()

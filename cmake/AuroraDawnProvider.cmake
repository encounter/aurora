include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/AuroraTargetPlatform.cmake")

# Resolve Dawn/WebGPU dependency based on AURORA_DAWN_PROVIDER and AURORA_DAWN_LINKAGE.
#
# After this module runs:
#   - dawn::webgpu_dawn target is available
#   - dawn::dawncpp_headers target is available (for tests)
#   - DAWN_ENABLE_* backend variables are set for the target platform
#   - AURORA_DAWN_IS_SHARED is set to TRUE/FALSE

# When using a non-vendored Dawn, we don't get DAWN_ENABLE_* from its build.
# Infer from the target platform instead.
function(_aurora_dawn_set_platform_backends)
  if (WIN32)
    set(DAWN_ENABLE_D3D12 ON CACHE INTERNAL "")
    set(DAWN_ENABLE_D3D11 ON CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN ON CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_OPENGLES OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL ON CACHE INTERNAL "")
  elseif (APPLE)
    set(DAWN_ENABLE_D3D12 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL ON CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_OPENGLES OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL ON CACHE INTERNAL "")
  elseif (ANDROID)
    set(DAWN_ENABLE_D3D12 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN ON CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_OPENGLES ON CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL ON CACHE INTERNAL "")
  else () # Linux / other
    set(DAWN_ENABLE_D3D12 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN ON CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL ON CACHE INTERNAL "")
    set(DAWN_ENABLE_OPENGLES ON CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL ON CACHE INTERNAL "")
  endif ()
endfunction()

# Prebuilt Dawn Android package has non-portable paths in it
# Will fix upstream but this works for now
function(_aurora_dawn_fix_android_link_interface)
  if (NOT ANDROID OR NOT TARGET dawn::webgpu_dawn)
    return()
  endif ()

  get_target_property(_dawn_link_libs dawn::webgpu_dawn INTERFACE_LINK_LIBRARIES)
  if (NOT _dawn_link_libs)
    return()
  endif ()

  set(_dawn_fixed_link_libs "")
  set(_dawn_replaced_log FALSE)
  foreach (_dawn_link_lib IN LISTS _dawn_link_libs)
    if (_dawn_link_lib MATCHES "^/.*/sysroot/usr/lib/[^/]+/[0-9]+/liblog\\.so$")
      list(APPEND _dawn_fixed_link_libs "$<LINK_ONLY:log>")
      set(_dawn_replaced_log TRUE)
    else ()
      list(APPEND _dawn_fixed_link_libs "${_dawn_link_lib}")
    endif ()
  endforeach ()

  if (_dawn_replaced_log)
    set_target_properties(dawn::webgpu_dawn PROPERTIES
      INTERFACE_LINK_LIBRARIES "${_dawn_fixed_link_libs}")
  endif ()
endfunction()

aurora_get_target_arch(_dawn_target_arch)
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _dawn_system)
string(TOLOWER "${_dawn_target_arch}" _dawn_arch)
set(_has_dawn_package FALSE)
if ("${_dawn_system}-${_dawn_arch}" MATCHES "^(windows-(amd64|arm64)|linux-(x86_64|aarch64)|darwin-(arm64|x86_64)|ios-arm64|android-aarch64)$")
  set(_has_dawn_package TRUE)
endif ()

# ── Auto: resolve provider based on platform availability ──
set(_aurora_dawn_provider "${AURORA_DAWN_PROVIDER}")
if (_aurora_dawn_provider STREQUAL "auto")
  # Prebuilt Dawn packages available for: windows-{amd64,arm64}, linux-{x86_64,aarch64}, darwin-{arm64,x86_64}, ios-arm64, android-aarch64
  if (_has_dawn_package)
    set(_aurora_dawn_provider "package")
  else ()
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    find_package(Dawn QUIET)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
    if (Dawn_FOUND)
      set(_aurora_dawn_provider "system")
    else ()
      set(_aurora_dawn_provider "vendor")
    endif ()
  endif ()
  set(AURORA_DAWN_PROVIDER "${_aurora_dawn_provider}" CACHE STRING "" FORCE)
  message(STATUS "aurora: Dawn auto-resolved provider: ${_aurora_dawn_provider}")
endif ()

if (_aurora_dawn_provider STREQUAL "vendor")
  # ── Vendor: FetchContent build from source ──
  if (NOT TARGET webgpu_dawn)
    message(STATUS "aurora: Fetching Dawn (provider=vendor, linkage=${AURORA_DAWN_LINKAGE})")

    if (AURORA_DAWN_LINKAGE STREQUAL "shared")
      set(DAWN_BUILD_MONOLITHIC_LIBRARY SHARED CACHE INTERNAL
        "Bundle all dawn components into a single shared library.")
    else ()
      set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC CACHE INTERNAL
        "Bundle all dawn components into a single static library.")
    endif ()
    set(DAWN_BUILD_SAMPLES OFF CACHE INTERNAL "Disable Dawn sample applications")
    set(DAWN_BUILD_BENCHMARKS OFF CACHE INTERNAL "Disable Dawn benchmarks")
    set(DAWN_SUPPORTS_GLFW_FOR_WINDOWING OFF CACHE INTERNAL "Disable Dawn GLFW windowing")
    set(DAWN_FETCH_DEPENDENCIES ON CACHE INTERNAL
      "Use fetch_dawn_dependencies.py as an alternative to using depot_tools")
    if (CMAKE_SYSTEM_NAME STREQUAL Linux)
      set(DAWN_USE_WAYLAND ON CACHE INTERNAL "Enable support for Wayland surface")
    endif ()
    set(TINT_BUILD_TESTS OFF CACHE INTERNAL "Build tests")
    set(TINT_BUILD_CMD_TOOLS OFF CACHE INTERNAL "Build the Tint command line tools")
    if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL$")
      set(ABSL_MSVC_STATIC_RUNTIME OFF CACHE INTERNAL "Link static runtime libraries")
    else ()
      set(ABSL_MSVC_STATIC_RUNTIME ON CACHE INTERNAL "Link static runtime libraries")
    endif ()

    include(FetchContent)
    FetchContent_Declare(dawn
      URL "https://github.com/encounter/dawn/archive/${AURORA_DAWN_REF}.tar.gz"
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(dawn)
    if (NOT TARGET webgpu_dawn)
      message(FATAL_ERROR "Failed to make dawn available")
    endif ()
  else ()
    message(STATUS "aurora: Using existing Dawn")
  endif ()

  if (AURORA_DAWN_LINKAGE STREQUAL "shared")
    set(AURORA_DAWN_IS_SHARED TRUE PARENT_SCOPE)
  else ()
    set(AURORA_DAWN_IS_SHARED FALSE PARENT_SCOPE)
  endif ()

elseif (_aurora_dawn_provider STREQUAL "system")
  # ── System: find_package(Dawn) ──
  message(STATUS "aurora: Using system Dawn (provider=system)")
  if (NOT Dawn_FOUND)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    find_package(Dawn REQUIRED)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
  endif ()
  if (NOT TARGET dawn::webgpu_dawn)
    message(FATAL_ERROR "find_package(Dawn) succeeded but dawn::webgpu_dawn target not found")
  endif ()
  _aurora_dawn_fix_android_link_interface()
  _aurora_dawn_set_platform_backends()

  get_target_property(_dawn_type dawn::webgpu_dawn TYPE)
  if (_dawn_type STREQUAL "SHARED_LIBRARY")
    set(AURORA_DAWN_IS_SHARED TRUE PARENT_SCOPE)
  else ()
    set(AURORA_DAWN_IS_SHARED FALSE PARENT_SCOPE)
  endif ()

elseif (_aurora_dawn_provider STREQUAL "package")
  # ── Package: download prebuilt Dawn install tree ──
  if (NOT AURORA_DAWN_PACKAGE_URL)
    if (NOT _has_dawn_package)
      message(FATAL_ERROR
        "AURORA_DAWN_PROVIDER=package requires AURORA_DAWN_PACKAGE_URL on this platform.\n"
        "No prebuilt Dawn package is available for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}"
        " with CMAKE_OSX_ARCHITECTURES='${CMAKE_OSX_ARCHITECTURES}'.")
    endif ()
    set(AURORA_DAWN_PACKAGE_URL
      "https://github.com/encounter/dawn/releases/download/${AURORA_DAWN_VERSION}/dawn-${_dawn_system}-${_dawn_arch}.tar.gz")
  endif ()
  message(STATUS "aurora: Fetching prebuilt Dawn package from ${AURORA_DAWN_PACKAGE_URL}")

  include(FetchContent)
  FetchContent_Declare(dawn_prebuilt
    URL "${AURORA_DAWN_PACKAGE_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
  FetchContent_MakeAvailable(dawn_prebuilt)

  # Detect package layout: may be nested in a subdirectory
  set(_dawn_pkg_dir "${dawn_prebuilt_SOURCE_DIR}")
  file(GLOB _dawn_subdirs
    "${dawn_prebuilt_SOURCE_DIR}/dawn-*"
    "${dawn_prebuilt_SOURCE_DIR}/Dawn-*"
  )
  foreach (_sd IN LISTS _dawn_subdirs)
    if (IS_DIRECTORY "${_sd}")
      set(_dawn_pkg_dir "${_sd}")
      break()
    endif ()
  endforeach ()
  if (IS_DIRECTORY "${dawn_prebuilt_SOURCE_DIR}/dawn-install")
    set(_dawn_pkg_dir "${dawn_prebuilt_SOURCE_DIR}/dawn-install")
  endif ()

  # Static Dawn packages may link Threads::Threads
  find_package(Threads QUIET)

  # Find DawnConfig.cmake in the package
  set(_dawn_cmake_found FALSE)
  foreach (_cmake_path
    "${_dawn_pkg_dir}/lib/cmake/Dawn"
    "${_dawn_pkg_dir}/lib64/cmake/Dawn"
    "${_dawn_pkg_dir}/cmake/Dawn"
    "${_dawn_pkg_dir}/cmake"
  )
    if (EXISTS "${_cmake_path}/DawnConfig.cmake")
      set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
      find_package(Dawn REQUIRED CONFIG
        PATHS "${_cmake_path}"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
      )
      set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
      set(_dawn_cmake_found TRUE)
      break()
    endif ()
  endforeach ()

  if (NOT _dawn_cmake_found OR NOT TARGET dawn::webgpu_dawn)
    message(FATAL_ERROR
      "Dawn package does not contain DawnConfig.cmake.\n"
      "Searched: ${_dawn_pkg_dir}/{lib,lib64,cmake}/cmake/Dawn/\n"
      "The package must be a Dawn install tree built with DAWN_ENABLE_INSTALL=ON.")
  endif ()

  _aurora_dawn_fix_android_link_interface()
  _aurora_dawn_set_platform_backends()

  get_target_property(_dawn_pkg_type dawn::webgpu_dawn TYPE)
  if (_dawn_pkg_type STREQUAL "SHARED_LIBRARY")
    set(AURORA_DAWN_IS_SHARED TRUE PARENT_SCOPE)

  else ()
    set(AURORA_DAWN_IS_SHARED FALSE PARENT_SCOPE)
  endif ()

else ()
  message(FATAL_ERROR "Invalid AURORA_DAWN_PROVIDER: ${AURORA_DAWN_PROVIDER} "
    "(must be auto, vendor, system, or package)")
endif ()

# Ensure dawn::dawncpp_headers target exists (needed by tests).
# When Dawn is vendored, this target is created by Dawn's CMake.
# For package/system providers, create it from the same include dirs.
if (NOT TARGET dawn::dawncpp_headers)
  add_library(dawn_dawncpp_headers INTERFACE IMPORTED GLOBAL)
  get_target_property(_dawn_inc dawn::webgpu_dawn INTERFACE_INCLUDE_DIRECTORIES)
  if (NOT _dawn_inc)
    if (TARGET dawn::dawn_public_config)
      get_target_property(_dawn_inc dawn::dawn_public_config INTERFACE_INCLUDE_DIRECTORIES)
    endif ()
  endif ()
  if (_dawn_inc)
    target_include_directories(dawn_dawncpp_headers INTERFACE ${_dawn_inc})
  else ()
    target_link_libraries(dawn_dawncpp_headers INTERFACE dawn::webgpu_dawn)
  endif ()
  add_library(dawn::dawncpp_headers ALIAS dawn_dawncpp_headers)
endif ()

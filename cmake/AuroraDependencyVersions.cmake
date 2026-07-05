# Single source of truth for aurora's pinned dependency versions. Included by
# aurora's own CMakeLists and standalone by out-of-tree consumers (e.g. the
# Dusklight mod SDK) that want to fetch the same prebuilt Dawn package without
# configuring the full aurora tree.
include_guard(GLOBAL)

# Specifies a cache string and previous values to forcibly migrate from
macro(_aurora_dependency_version name value doc)
  set(_aurora_old_defaults ${ARGN})
  if (DEFINED CACHE{${name}} AND ${name} IN_LIST _aurora_old_defaults)
    message(STATUS "aurora: Migrating ${name} from old default ${${name}} to ${value}")
    set(${name} "${value}" CACHE STRING "${doc}" FORCE)
  else ()
    set(${name} "${value}" CACHE STRING "${doc}")
  endif ()
  unset(_aurora_old_defaults)
endmacro()

# Dependency versions
_aurora_dependency_version(AURORA_DAWN_VERSION "v20260618.032059" "Dawn prebuilt version tag (https://github.com/encounter/dawn/releases)"
        "v20260523.201736" "v20260603.191052") # Previous versions
_aurora_dependency_version(AURORA_DAWN_REF "266c1cf8de969a364afa4fa49311631fc99a881e" "Dawn commit ref (https://github.com/encounter/dawn)"
        "9aa45f938d4b36626722bbfdc2f18447179337e6" "13abc3bc8ea2d3c2050f9e77a12d012108ceee24") # Previous versions
_aurora_dependency_version(AURORA_SDL3_VERSION "3.4.10" "SDL3 prebuilt version tag (https://github.com/libsdl-org/SDL/releases)")
_aurora_dependency_version(AURORA_SDL3_REF "refs/tags/release-3.4.10" "SDL3 commit ref (https://github.com/libsdl-org/SDL)")
_aurora_dependency_version(AURORA_NOD_VERSION "v2.0.0-alpha.10" "nod version tag (https://github.com/encounter/nod/releases)")

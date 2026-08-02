if (NOT DEFINED SDL_SOURCE_DIR)
  message(FATAL_ERROR "SDL_SOURCE_DIR is required")
endif ()

set(_activity "${SDL_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java")
if (NOT EXISTS "${_activity}")
  message(FATAL_ERROR "SDL Android activity source is missing: ${_activity}")
endif ()

file(READ "${_activity}" _source)
set(_old "} catch (FileNotFoundException e) {")
set(_new "} catch (FileNotFoundException | SecurityException e) {")

string(FIND "${_source}" "${_new}" _already_patched)
if (NOT _already_patched EQUAL -1)
  message(STATUS "SDL3 Android SecurityException patch already applied")
  return()
endif ()

string(FIND "${_source}" "${_old}" _patch_site)
if (_patch_site EQUAL -1)
  message(FATAL_ERROR "Failed to apply SDL3 Android SecurityException patch: expected catch clause not found")
endif ()

string(REPLACE "${_old}" "${_new}" _patched_source "${_source}")
file(WRITE "${_activity}" "${_patched_source}")
message(STATUS "Applied SDL3 Android SecurityException patch")

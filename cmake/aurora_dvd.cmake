include(FetchContent)

set(AURORA_NOD_GIT_REPOSITORY "https://github.com/encounter/nod.git" CACHE STRING
    "Git repository used for nod when AURORA_ENABLE_DVD is enabled")
set(AURORA_NOD_GIT_TAG "d2c7e0ec4094fbdbcff65e635033b21dd9fdbfd5" CACHE STRING
    "Git tag or commit used for nod when AURORA_ENABLE_DVD is enabled")
set(AURORA_NOD_SOURCE_DIR "" CACHE PATH
    "Optional local nod source directory override (uses FetchContent SOURCE_DIR)")

if (NOT TARGET nod::nod)
  message(STATUS "aurora: Building nod")

  if (AURORA_NOD_SOURCE_DIR)
    if (NOT EXISTS "${AURORA_NOD_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR "AURORA_NOD_SOURCE_DIR is set but '${AURORA_NOD_SOURCE_DIR}/CMakeLists.txt' was not found")
    endif ()
    FetchContent_Declare(aurora_nod SOURCE_DIR "${AURORA_NOD_SOURCE_DIR}")
  else ()
    FetchContent_Declare(aurora_nod
      GIT_REPOSITORY "${AURORA_NOD_GIT_REPOSITORY}"
      GIT_TAG "${AURORA_NOD_GIT_TAG}"
      GIT_SHALLOW TRUE
      EXCLUDE_FROM_ALL
    )
  endif ()

  FetchContent_MakeAvailable(aurora_nod)
  if (NOT TARGET nod::nod)
    message(FATAL_ERROR "Failed to make nod available")
  endif ()
else ()
  message(STATUS "aurora: Using existing nod")
endif ()

add_library(aurora_dvd STATIC lib/dolphin/dvd/dvd.cpp)
add_library(aurora::dvd ALIAS aurora_dvd)

target_compile_definitions(aurora_dvd PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_dvd PUBLIC include)
target_link_libraries(aurora_dvd PUBLIC nod::nod SDL3::SDL3-static)

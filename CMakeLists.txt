cmake_minimum_required(VERSION 3.30)

project(aurora LANGUAGES C CXX)
if (APPLE)
    enable_language(OBJC)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

option(AURORA_ENABLE_GX "Enable GX implementation and WebGPU renderer" ON)

include("cmake/get_cpm.cmake")
include("cmake/thirdparty.cmake")

include(cmake/aurora_core.cmake)
if (AURORA_ENABLE_GX)
  include(cmake/aurora_gx.cmake)
endif ()
include(cmake/aurora_pad.cmake)
include(cmake/aurora_si.cmake)
include(cmake/aurora_main.cmake)
include(cmake/aurora_mtx.cmake)
include(cmake/aurora_vi.cmake)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(examples)
endif ()

if (NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif ()

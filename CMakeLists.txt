cmake_minimum_required(VERSION 3.13)
project(aurora LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(extern)

include(cmake/aurora_core.cmake)
include(cmake/aurora_gx.cmake)
include(cmake/aurora_pad.cmake)
include(cmake/aurora_si.cmake)
include(cmake/aurora_main.cmake)
include(cmake/aurora_mtx.cmake)
include(cmake/aurora_vi.cmake)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(examples)
endif ()

add_subdirectory(tests)

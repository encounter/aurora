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

if (NOT MSVC)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(kabufuda STATIC
    include/kabufuda/Constants.hpp
    include/kabufuda/AsyncIO.hpp
    include/kabufuda/BlockAllocationTable.hpp lib/kabufuda/BlockAllocationTable.cpp
    include/kabufuda/Card.hpp lib/kabufuda/Card.cpp
    include/kabufuda/Directory.hpp lib/kabufuda/Directory.cpp
    include/kabufuda/File.hpp lib/kabufuda/File.cpp
    include/kabufuda/Util.hpp lib/kabufuda/Util.cpp
    include/kabufuda/SRAM.hpp lib/kabufuda/SRAM.cpp
)

if(WIN32)
  if (MSVC)
    target_compile_options(kabufuda PRIVATE
      # Enforce various standards compliant behavior.
      $<$<COMPILE_LANGUAGE:CXX>:/permissive->

      # Enable standard volatile semantics.
      $<$<COMPILE_LANGUAGE:CXX>:/volatile:iso>

      # Reports the proper value for the __cplusplus preprocessor macro.
      $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>

      # Use latest C++ standard.
      $<$<COMPILE_LANGUAGE:CXX>:/std:c++latest>
    )
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
      # Flags for MSVC (not clang-cl)
      target_compile_options(kabufuda PRIVATE
        # Allow constexpr variables to have explicit external linkage.
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:externConstexpr>

        # Assume that new throws exceptions, allowing better code generation.
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:throwingNew>
      )
    endif()
  endif()

  target_sources(kabufuda PRIVATE
    lib/kabufuda/AsyncIOWin32.cpp
  )
elseif(NX OR EMSCRIPTEN)
  target_sources(kabufuda PRIVATE
    lib/kabufuda/AsyncIONX.cpp
  )
else()
  target_sources(kabufuda PRIVATE
    lib/kabufuda/AsyncIOPosix.cpp
  )
  if(NOT APPLE)
    target_link_libraries(kabufuda PUBLIC rt)
  endif()
endif()

target_include_directories(kabufuda PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

add_subdirectory(test)

include(FetchContent)

if (NOT TARGET gtest)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(googletest)
endif ()

# GX FIFO round-trip tests
# Compile GX sources directly to avoid pulling in the full renderer/WebGPU runtime
add_executable(gx_fifo_tests
  gx_fifo_test.cpp
  gx_test_stubs.cpp
  # GX API implementations (encoders)
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXBump.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXCull.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXDispList.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXDraw.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXExtra.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXFifo.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXFrameBuffer.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXGeometry.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXGet.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXLighting.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXManage.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXPerf.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXPixel.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXTev.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXTexture.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXTransform.cpp
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx/GXVert.cpp
  # FIFO/command processor (encoder + decoder)
  ${CMAKE_SOURCE_DIR}/lib/gfx/fifo.cpp
  ${CMAKE_SOURCE_DIR}/lib/gfx/command_processor.cpp
)

target_include_directories(gx_fifo_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/lib
  ${CMAKE_SOURCE_DIR}/lib/dolphin/gx
)

target_compile_definitions(gx_fifo_tests PRIVATE AURORA TARGET_PC)

target_link_libraries(gx_fifo_tests PRIVATE
  gtest
  gtest_main
  fmt::fmt
  xxhash
  absl::flat_hash_map
  absl::btree
  dawn::dawncpp_headers
)

include(GoogleTest)
gtest_discover_tests(gx_fifo_tests)

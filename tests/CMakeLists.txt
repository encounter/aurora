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

if (AURORA_ENABLE_GX)
# GX FIFO round-trip tests
# Compile GX sources directly to avoid pulling in the full renderer/WebGPU runtime
add_executable(gx_fifo_tests
  gx_fifo_test.cpp
  gx_test_stubs.cpp
  # GX API implementations (encoders)
  ../lib/dolphin/gx/GXBump.cpp
  ../lib/dolphin/gx/GXCull.cpp
  ../lib/dolphin/gx/GXDispList.cpp
  ../lib/dolphin/gx/GXDraw.cpp
  ../lib/dolphin/gx/GXExtra.cpp
  ../lib/dolphin/gx/GXFifo.cpp
  ../lib/dolphin/gx/GXFrameBuffer.cpp
  ../lib/dolphin/gx/GXGeometry.cpp
  ../lib/dolphin/gx/GXGet.cpp
  ../lib/dolphin/gx/GXLighting.cpp
  ../lib/dolphin/gx/GXManage.cpp
  ../lib/dolphin/gx/GXPerf.cpp
  ../lib/dolphin/gx/GXPixel.cpp
  ../lib/dolphin/gx/GXTev.cpp
  ../lib/dolphin/gx/GXTexture.cpp
  ../lib/dolphin/gx/GXTransform.cpp
  ../lib/dolphin/gx/GXVert.cpp
  # FIFO/command processor (encoder + decoder)
  ../lib/gfx/fifo.cpp
  ../lib/gfx/command_processor.cpp
)

target_include_directories(gx_fifo_tests PRIVATE
  ../include
  ../lib
  ../lib/dolphin/gx
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
  SDL3::SDL3-static
)

include(GoogleTest)
gtest_discover_tests(gx_fifo_tests)
endif () # AURORA_ENABLE_GX

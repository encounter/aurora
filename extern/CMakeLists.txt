if (NOT EMSCRIPTEN)
  if (NOT TARGET dawn_native)
    if (CMAKE_SYSTEM_NAME STREQUAL Windows)
      set(DAWN_ENABLE_DESKTOP_GL ON CACHE BOOL "Enable compilation of the OpenGL backend" FORCE)
    endif ()
    if (CMAKE_SYSTEM_NAME STREQUAL Linux)
      set(DAWN_ENABLE_OPENGLES ON CACHE BOOL "Enable compilation of the OpenGL ES backend" FORCE)
    endif ()
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "Use fetch_dawn_dependencies.py as an alternative to using depot_tools" FORCE)
    add_subdirectory(dawn EXCLUDE_FROM_ALL)
    if (DAWN_ENABLE_VULKAN)
      target_compile_definitions(dawn_native PRIVATE
          DAWN_ENABLE_VULKAN_VALIDATION_LAYERS
          DAWN_VK_DATA_DIR="vulkandata")
    endif ()
    if (MSVC)
      target_compile_options(dawn_native PRIVATE /bigobj)
    else ()
      target_compile_options(SPIRV-Tools-static PRIVATE -Wno-implicit-fallthrough)
      target_compile_options(SPIRV-Tools-opt PRIVATE -Wno-implicit-fallthrough)
    endif ()
  endif ()

  if (NOT TARGET SDL3-static)
    if (WIN32)
      set(SDL_LIBC ON CACHE BOOL "Use the system C library" FORCE)
    endif ()
    add_subdirectory(SDL EXCLUDE_FROM_ALL)
    if (NOT MSVC)
      target_compile_options(SDL3-static PRIVATE -Wno-implicit-fallthrough -Wno-shadow)
    endif ()
  endif ()
else ()
  set(ABSL_PROPAGATE_CXX_STD ON)
  add_subdirectory(dawn/third_party/abseil-cpp EXCLUDE_FROM_ALL)
endif ()

if (NOT TARGET xxhash)
  add_subdirectory(xxhash EXCLUDE_FROM_ALL)
endif ()
if (NOT TARGET fmt)
  add_subdirectory(fmt EXCLUDE_FROM_ALL)
endif ()

if (NOT TARGET imgui)
  add_library(imgui
      imgui/imgui.cpp
      imgui/imgui_demo.cpp
      imgui/imgui_draw.cpp
      imgui/imgui_tables.cpp
      imgui/imgui_widgets.cpp
      imgui/misc/cpp/imgui_stdlib.cpp)
  target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
  if (CMAKE_COMPILER_IS_GNUCXX)
    # currently explicitly ignored for clang in imgui code, but not gcc (yet)
    target_compile_options(imgui PRIVATE -Wno-deprecated-enum-enum-conversion)
  endif ()

  # Optional, replaces stb_freetype if available
  find_package(Freetype)
  # Permit disabling for macOS universal builds
  option(IMGUI_USE_FREETYPE "Enable freetype with imgui" ON)
  if (FREETYPE_FOUND AND IMGUI_USE_FREETYPE)
    target_sources(imgui PRIVATE ../extern/imgui/misc/freetype/imgui_freetype.cpp)
    target_compile_definitions(imgui PRIVATE IMGUI_ENABLE_FREETYPE)
    target_link_libraries(imgui PRIVATE Freetype::Freetype)
  endif ()
endif ()

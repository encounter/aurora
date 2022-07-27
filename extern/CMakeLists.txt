if (CMAKE_SYSTEM_NAME STREQUAL Windows)
  set(DAWN_ENABLE_DESKTOP_GL ON CACHE BOOL "Enable compilation of the OpenGL backend" FORCE)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL Linux)
  set(DAWN_ENABLE_OPENGLES ON CACHE BOOL "Enable compilation of the OpenGL ES backend" FORCE)
endif ()
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

if (WIN32)
  set(SDL_LIBC ON CACHE BOOL "Use the system C library" FORCE)
endif ()
add_subdirectory(SDL EXCLUDE_FROM_ALL)
if (NOT MSVC)
  target_compile_options(SDL2-static PRIVATE -Wno-implicit-fallthrough -Wno-shadow)
endif ()

add_subdirectory(xxhash EXCLUDE_FROM_ALL)
add_subdirectory(fmt EXCLUDE_FROM_ALL)

add_library(imgui
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

# Building

## Prerequisites

- CMake 3.25 or later
- A C++20 compatible compiler (Clang, GCC, or MSVC)
- Git (for cloning)

## Building the Examples

To build Aurora's included examples:

```bash
git clone https://github.com/encounter/aurora.git
cd aurora
mkdir build && cd build
cmake ..
cmake --build . --target simple
```

The `simple` example demonstrates a minimal Aurora application with a blue screen. The built executable will be in `build/examples/simple`.

## Using Aurora in Your Project

Aurora is designed to be integrated as a library in GameCube/Wii decompilation projects.

**CMakeLists.txt example:**

```cmake
cmake_minimum_required(VERSION 3.25)
project(your_game)

# Add Aurora as a subdirectory
add_subdirectory(extern/aurora EXCLUDE_FROM_ALL)

# Create your executable
add_executable(your_game src/main.c)

# Link against Aurora components
target_link_libraries(your_game PRIVATE 
    aurora::core 
    aurora::gx 
    aurora::main 
    aurora::vi
)
```

See [examples/simple.c](../examples/simple.c) for a minimal application template.

## CMake Options

- `AURORA_ENABLE_GX` (default: ON) - Enable GX implementation and WebGPU renderer
- `AURORA_ENABLE_DVD` (default: OFF) - Enable DVD implementation backed by nod
- `AURORA_ENABLE_CARD` (default: ON) - Enable CARD implementation based on kabufuda
- `AURORA_ENABLE_RMLUI` (default: OFF) - Enable HTML/CSS based UI library
- `AURORA_CACHE_USE_ZSTD` (default: ON) - Compress WebGPU cache entries with zstd

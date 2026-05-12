<div align="center">
  <img src="assets/aurora.png" alt="Logo" width="640">
</div>
<br/>

Aurora is a source-level GameCube & Wii compatibility layer intended for use with game decompilation projects.

Originally developed for use in [Metaforce](https://github.com/AxioDL/metaforce), a Metroid Prime reverse engineering project.
It now powers several completed source ports, including [Dusk](https://github.com/TwilitRealm/dusk).

### Features

- Application layer using SDL3
  - Runs on Windows, Linux, macOS, iOS, tvOS, Android
- GX compatibility layer
  - Graphics API support: D3D12, Vulkan, Metal
  - Highly accurate and performant GX implementation
  - Robust pipeline cache system with "transferable" cache support for releases
  - Dolphin-compatible texture pack support
  - Widescreen & resolution scaling support
  - Custom APIs for offscreen rendering
- PAD compatibility layer
  - Utilizes `SDL_Gamepad` for wide controller support, including GameCube controller adapters
  - Automatically saves and loads controller bindings and port mappings
  - Gyro & mouse support
- DVD compatibility layer
  - Utilizes [nod](https://github.com/encounter/nod) to support all GameCube/Wii disc image types, including RVZ
- CARD compatibility layer
  - Full compatibility with Dolphin `.gci` and `.raw` for game saves
- [Dear ImGui](https://github.com/ocornut/imgui) built-in for simple debug UIs
- [RmlUi](https://github.com/mikke89/RmlUi) built-in for full-fledged HTML/CSS-based UIs

### Graphics

The GX compatibility layer is built on top of [WebGPU](https://www.w3.org/TR/webgpu/), a cross-platform graphics API
abstraction layer. WebGPU allows targeting all major platforms simultaneously with minimal overhead. The WebGPU
implementation used is Chromium's [Dawn](https://dawn.googlesource.com/dawn/).

![Screenshot](assets/screenshot.png)

### Building

#### Prerequisites

- CMake 3.25 or later
- A C++20 compatible compiler (Clang, GCC, or MSVC)
- Git (for cloning)

#### Building the Examples

To build Aurora's included examples:

```bash
git clone https://github.com/encounter/aurora.git
cd aurora
mkdir build && cd build
cmake ..
cmake --build . --target simple
```

The `simple` example demonstrates a minimal Aurora application with a blue screen. The built executable will be in `build/examples/simple`.

#### Using Aurora in Your Project

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

See [examples/simple.c](examples/simple.c) for a minimal application template.

#### CMake Options

- `AURORA_ENABLE_GX` (default: ON) - Enable GX implementation and WebGPU renderer
- `AURORA_ENABLE_DVD` (default: OFF) - Enable DVD implementation backed by nod
- `AURORA_ENABLE_CARD` (default: ON) - Enable CARD implementation based on kabufuda
- `AURORA_ENABLE_RMLUI` (default: OFF) - Enable HTML/CSS based UI library
- `AURORA_CACHE_USE_ZSTD` (default: ON) - Compress WebGPU cache entries with zstd

### License

Aurora is licensed under the [MIT License](LICENSE).

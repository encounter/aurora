# Aurora

Aurora is a source-level GameCube & Wii compatibility layer intended for use with game reverse engineering projects.

Originally developed for use in [Metaforce](https://github.com/AxioDL/metaforce), a Metroid Prime reverse engineering
project.

### Features

- GX compatibility layer
    - Graphics API support: D3D12, Vulkan, Metal, OpenGL 4.4+ and OpenGL ES 3.1+
    - *Planned: deko3d backend for Switch*
- Application layer using SDL
    - Runs on Windows, Linux, macOS, iOS, tvOS (Android coming soon)
    - Audio support with SDL_audio
- PAD compatibility layer
    - Utilizes SDL_GameController for wide controller support, including GameCube controllers.
    - *Planned: Wii remote support*
- [Dear ImGui](https://github.com/ocornut/imgui) built-in for UI

### GX

The GX compatibility layer is built on top of [WebGPU](https://www.w3.org/TR/webgpu/), a cross-platform graphics API
abstraction layer. WebGPU allows targeting all major platforms simultaneously with minimal overhead.

Currently, the WebGPU implementation used is Chromium's [Dawn](https://dawn.googlesource.com/dawn/).  

See [GX API support](GX.md) for more information.

### PAD

The PAD compatibility layer utilizes SDL_GameController to automatically support & provide mappings for hundreds of
controllers across all platforms.

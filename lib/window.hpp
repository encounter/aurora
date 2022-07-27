#pragma once

#include <aurora/aurora.h>
#include <aurora/event.h>

struct SDL_Window;
struct SDL_Renderer;

namespace aurora::window {
bool initialize();
void shutdown();
bool create_window(AuroraBackend backend);
bool create_renderer();
void destroy_window();
void show_window();
AuroraWindowSize get_window_size();
const AuroraEvent* poll_events();
SDL_Window* get_sdl_window();
SDL_Renderer* get_sdl_renderer();
}; // namespace aurora::window

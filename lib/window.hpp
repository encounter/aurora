#pragma once

#include <aurora/aurora.h>
#include <aurora/event.h>

struct SDL_Window;
struct SDL_Renderer;

#ifdef AURORA_ENABLE_GX
namespace wgpu {
class Instance;
class Surface;
} // namespace wgpu
#endif

namespace aurora::window {

enum class CustomEvent {
  FrameBufferChanged,
  End,
};
static Uint32 operator+(Uint32 lhs, CustomEvent rhs) { return lhs + static_cast<Uint32>(rhs); }

// On Android in particular, we need to hold a mutex around critical areas like surface creation
// and presentation, so that the SDLActivity doesn't destroy the surface out from underneath us.
class SurfaceLock {
public:
  explicit SurfaceLock(bool enabled = true) noexcept;
  ~SurfaceLock();

  SurfaceLock(const SurfaceLock&) = delete;
  SurfaceLock& operator=(const SurfaceLock&) = delete;

private:
  bool m_locked = false;
};

bool initialize();
bool initialize_event_watch();
bool push_custom_event(CustomEvent eventType);
void shutdown();
bool create_window(AuroraBackend backend);
bool create_renderer();
#ifdef AURORA_ENABLE_GX
wgpu::Surface create_sdl_surface(const wgpu::Instance& instance, const char* label = nullptr);
#endif
void destroy_window();
void show_window();
AuroraWindowSize get_window_size();
AuroraWindowSize refresh_window_size();
void queue_size_events(const AuroraWindowSize& oldSize, const AuroraWindowSize& size);
const AuroraEvent* poll_events();
SDL_Window* get_sdl_window();
AuroraSurfaceType surface_type() noexcept;
void set_surface_type(AuroraSurfaceType type) noexcept;
SDL_Renderer* get_sdl_renderer();
bool is_paused() noexcept;
bool is_presentable() noexcept;
void set_surface_ready(bool ready) noexcept;
bool prepare_frame();
// Explicitly unbinding the primary target cancels automatic SDL restoration.
void clear_surface_restore() noexcept;
void set_title(const char* title);
void set_fullscreen(bool fullscreen);
bool get_fullscreen();
void set_window_size(uint32_t width, uint32_t height);
void set_window_position(uint32_t x, uint32_t y);
void center_window();
void notify_frame_buffer_change();
void set_frame_buffer_scale(float scale);
void set_frame_buffer_aspect_fit(bool fit);
void set_background_input(bool value);
}; // namespace aurora::window

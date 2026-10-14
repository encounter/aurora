#include <aurora/aurora.h>
#include <aurora/time.hpp>

#ifdef AURORA_ENABLE_GX
#include "gfx/resources.hpp"
#include "gfx/frame.hpp"
#include "gfx/efb.hpp"
#include "gfx/present_target.hpp"
#include "gfx/recording.hpp"
#include "gfx/render_worker.hpp"
#include "gx/command_processor.hpp"
#include "gx/fifo.hpp"
#include "gx/gx.hpp"
#include "gx/texture.hpp"
#include "imgui.hpp"
#include "webgpu/gpu.hpp"
#include "webgpu/gpu_prof.hpp"
#include <webgpu/webgpu_cpp.h>
#endif

#ifdef AURORA_ENABLE_RMLUI
#include "rmlui.hpp"
#endif

#include "gamepad.hpp"
#include "internal.hpp"
#include "thread.hpp"
#include "window.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <magic_enum.hpp>
#include <mutex>

#include "system_info.hpp"
#include "tracy/Tracy.hpp"

namespace aurora {
AuroraConfig g_config;
uint32_t g_sdlCustomEventsStart;
char g_gameName[4];

namespace {
constexpr Module Log{"aurora"};

#ifdef AURORA_ENABLE_GX
// GPU
using webgpu::g_device;
using webgpu::g_queue;

gx::fifo::ProcessingMode to_fifo_processing_mode(AuroraGxProcessingMode mode) noexcept {
  switch (mode) {
  case GX_PROCESSING_INLINE:
    return gx::fifo::ProcessingMode::Inline;
  case GX_PROCESSING_DRAIN:
    return gx::fifo::ProcessingMode::Drain;
  case GX_PROCESSING_THREAD:
  default:
    return gx::fifo::ProcessingMode::Thread;
  }
}
#endif

#ifdef AURORA_ENABLE_GX
constexpr std::array PreferredBackendOrder{
#ifdef ENABLE_BACKEND_WEBGPU
    BACKEND_WEBGPU,
#endif
#ifdef DAWN_ENABLE_BACKEND_D3D12
    BACKEND_D3D12,
#endif
#ifdef DAWN_ENABLE_BACKEND_METAL
    BACKEND_METAL,
#endif
#ifdef DAWN_ENABLE_BACKEND_VULKAN
    BACKEND_VULKAN,
#endif
#ifdef DAWN_ENABLE_BACKEND_D3D11
    BACKEND_D3D11,
#endif
// #ifdef DAWN_ENABLE_BACKEND_DESKTOP_GL
//     BACKEND_OPENGL,
// #endif
#ifdef DAWN_ENABLE_BACKEND_OPENGLES
    BACKEND_OPENGLES,
#endif
#ifdef DAWN_ENABLE_BACKEND_NULL
    BACKEND_NULL,
#endif
};
#else
constexpr std::array<AuroraBackend, 0> PreferredBackendOrder{};
#endif

std::mutex g_surfaceChangeMutex;
bool g_initialFrame = false;
bool g_gamepadInitialized = false;
#ifdef AURORA_ENABLE_GX
bool g_imguiFrame = false;

void initialize_ui(const AuroraWindowSize& size) {
  if (gfx::xfb_target() == gfx::InvalidPresentTarget) {
    return;
  }

  if (!imgui::is_initialized() && (window::get_sdl_window() || g_config.imGuiInitCallback)) {
    imgui::create_context();
    if (g_config.imGuiInitCallback) {
      g_config.imGuiInitCallback(&size);
    }
    imgui::initialize();
  }

#ifdef AURORA_ENABLE_RMLUI
  if (!rmlui::is_initialized()) {
    rmlui::initialize(size);
  }
#endif
}
#endif

AuroraInfo initialize(int argc, char* argv[], const AuroraConfig& config) noexcept {
  g_config = config;
  Log.info("Aurora initializing");
  log_system_information();
  if (g_config.appName == nullptr) {
    g_config.appName = "Aurora";
  } else {
    g_config.appName = strdup(g_config.appName);
  }
  if (g_config.userPath == nullptr) {
    g_config.userPath = SDL_GetPrefPath(nullptr, g_config.appName);
  } else {
    g_config.userPath = strdup(g_config.userPath);
  }
  if (g_config.cachePath == nullptr) {
    g_config.cachePath = SDL_GetPrefPath(nullptr, g_config.appName);
  } else {
    g_config.cachePath = strdup(g_config.cachePath);
  }
  if (g_config.resourcesPath == nullptr) {
    g_config.resourcesPath = SDL_GetBasePath();
  } else {
    g_config.resourcesPath = strdup(g_config.resourcesPath);
  }
  if (g_config.msaa == 0) {
    g_config.msaa = 1;
  }
  if (g_config.maxTextureAnisotropy == 0) {
    g_config.maxTextureAnisotropy = 16;
  }
  const bool managedWindow = g_config.surface.type == AURORA_SURFACE_SDL_MANAGED;
#ifdef AURORA_ENABLE_GX
  gfx::efb::initialize(g_config.surface);
#endif
  AURORA_ASSERT(window::initialize(), "Error initializing window");

  g_sdlCustomEventsStart = 0;
  if (SDL_WasInit(SDL_INIT_EVENTS)) {
    g_sdlCustomEventsStart = SDL_RegisterEvents(static_cast<int>(window::CustomEvent::End));
    AURORA_ASSERT(g_sdlCustomEventsStart != static_cast<uint32_t>(-1), "Failed to allocate user events: {}",
                  SDL_GetError());
  }
  AURORA_ASSERT(window::initialize_event_watch(), "Error initializing SDL event watch");

#ifdef AURORA_ENABLE_GX
  /* Attempt to create a window using the calling application's desired backend */
  AuroraBackend selectedBackend = config.desiredBackend;
  const auto tryBackend = [&](AuroraBackend backend) {
    if (managedWindow && !window::create_window(backend)) {
      return false;
    }
    if (webgpu::initialize(backend, config.allowCpuAdapter)) {
      return true;
    }
    if (managedWindow) {
      window::destroy_window();
    }
    return false;
  };
  bool initialized = selectedBackend != BACKEND_AUTO && tryBackend(selectedBackend);
  if (!initialized) {
    for (const auto backend : PreferredBackendOrder) {
      selectedBackend = backend;
      if (tryBackend(backend)) {
        initialized = true;
        break;
      }
    }
  }
  AURORA_ASSERT(initialized, "Error initializing graphics for surface type {}", static_cast<int>(config.surface.type));

  // Initialize SDL_Renderer for ImGui when we can't use a Dawn backend
  if (managedWindow && webgpu::g_backendType == wgpu::BackendType::Null) {
    AURORA_ASSERT(window::create_renderer(), "Failed to initialize SDL renderer: {}", SDL_GetError());
  }
#else
  AuroraBackend selectedBackend = BACKEND_NULL;
  if (managedWindow) {
    AURORA_ASSERT(window::create_window(BACKEND_NULL), "Error creating window: {}", SDL_GetError());
    AURORA_ASSERT(window::create_renderer(), "Failed to initialize SDL renderer: {}", SDL_GetError());
  }
#endif

  window::show_window();
  thread::set_current({
      .name = "Main thread",
      .affinity = thread::Affinity::SharedCache,
  });

#ifdef AURORA_ENABLE_GX
  gfx::initialize();
  gx::fifo::init(to_fifo_processing_mode(g_config.gxProcessingMode));
#endif
  const auto size = window::get_window_size();
  Log.info("Using framebuffer size {}x{} scale {}", size.fb_width, size.fb_height, size.scale);
#ifdef AURORA_ENABLE_GX
  initialize_ui(size);
#endif

  g_initialFrame = true;
  g_config.desiredBackend = selectedBackend;
  return {
      .backend = selectedBackend,
      .userPath = g_config.userPath,
      .cachePath = g_config.cachePath,
      .window = window::get_sdl_window(),
      .windowSize = size,
  };
}

void shutdown() noexcept {
#ifdef AURORA_ENABLE_GX
  gx::fifo::shutdown();
  gfx::render_worker::synchronize();
#ifdef AURORA_ENABLE_RMLUI
  rmlui::shutdown();
#endif
  imgui::shutdown();
  gfx::shutdown();
  webgpu::shutdown();
#endif
  if (g_gamepadInitialized) {
    gamepad::shutdown();
  }
  g_gamepadInitialized = false;
  window::shutdown();
}

const AuroraEvent* update() noexcept {
  ZoneScoped;
  if (g_initialFrame) {
    g_initialFrame = false;
    if (window::surface_type() != AURORA_SURFACE_HEADLESS) {
      gamepad::initialize();
      g_gamepadInitialized = true;
    }
  }
  return window::poll_events();
}

bool begin_frame() noexcept {
  ZoneScoped;
#ifdef AURORA_ENABLE_GX
  if (!window::prepare_frame()) {
    return false;
  }

  // Apply pending viewport policy before beginning the frame
  gx::update();
  if (!gfx::begin_frame()) {
    return false;
  }

  const auto size = window::get_window_size();
  initialize_ui(size);
  g_imguiFrame = gfx::efb::applied_state().xfb.state && imgui::is_initialized();
  if (g_imguiFrame) {
    imgui::new_frame(size);
  }
  gx::fifo::begin_frame();
#endif
  return true;
}

void end_frame() noexcept {
  ZoneScoped;
#ifdef AURORA_ENABLE_GX
  gx::fifo::drain();
  gx::fifo::end_frame();
  gx::texture::end_frame();
  gfx::finish();
  imgui::DrawData drawData;
  if (g_imguiFrame) {
    drawData = imgui::freeze();
    g_imguiFrame = false;
  }
  const auto efb = gfx::efb::applied_state();
  wgpu::BindGroup rmlBindGroup;
  bool rmlOverlay = false;
#ifdef AURORA_ENABLE_RMLUI
  if (efb.xfb.state && efb.xfb.size.native_fb_width && efb.xfb.size.native_fb_height && rmlui::is_initialized()) {
    const auto& source = efb.present_source();
    const auto viewport = webgpu::calculate_present_viewport(
        efb.xfb.size.native_fb_width, efb.xfb.size.native_fb_height, source.size.width, source.size.height);
    auto rmlFrame = rmlui::record_frame(viewport, source);
    rmlBindGroup = std::move(rmlFrame.bindGroup);
    rmlOverlay = rmlFrame.overlay;
  }
#endif
  gfx::detail::record_xfb_present(std::move(drawData), std::move(rmlBindGroup), rmlOverlay);
  gfx::end_frame([](wgpu::CommandEncoder& encoder, std::vector<gfx::AfterSubmitCallback> callbacks) {
    constexpr wgpu::CommandBufferDescriptor cmdBufDescriptor{.label = "Redraw command buffer"};
    const auto commands = encoder.Finish(&cmdBufDescriptor);
    {
      ZoneScopedN("Queue Submit");
      g_queue.Submit(1, &commands);
    }
    for (auto& callback : callbacks) {
      if (callback) {
        callback();
      }
    }

    TracyPlotConfig("aurora: lastVertSize", tracy::PlotFormatType::Memory, false, true, 0);
    TracyPlotConfig("aurora: lastUniformSize", tracy::PlotFormatType::Memory, false, true, 0);
    TracyPlotConfig("aurora: lastIndexSize", tracy::PlotFormatType::Memory, false, true, 0);
    TracyPlotConfig("aurora: lastStorageSize", tracy::PlotFormatType::Memory, false, true, 0);
    TracyPlotConfig("aurora: lastTextureUploadSize", tracy::PlotFormatType::Memory, false, true, 0);

    const auto& stats = gfx::detail::resources().stats;
    TracyPlot("aurora: queuedPipelines", static_cast<int64_t>(stats.queuedPipelines));
    TracyPlot("aurora: createdPipelines", static_cast<int64_t>(stats.createdPipelines));
    TracyPlot("aurora: drawCallCount", static_cast<int64_t>(stats.drawCallCount));
    TracyPlot("aurora: mergedDrawCallCount", static_cast<int64_t>(stats.mergedDrawCallCount));
    TracyPlot("aurora: lastVertSize", static_cast<int64_t>(stats.lastVertSize));
    TracyPlot("aurora: lastUniformSize", static_cast<int64_t>(stats.lastUniformSize));
    TracyPlot("aurora: lastIndexSize", static_cast<int64_t>(stats.lastIndexSize));
    TracyPlot("aurora: lastStorageSize", static_cast<int64_t>(stats.lastStorageSize));
    TracyPlot("aurora: lastTextureUploadSize", static_cast<int64_t>(stats.lastTextureUploadSize));
  });

#endif
}
} // namespace
} // namespace aurora

// C API bindings
AuroraInfo aurora_initialize(int argc, char* argv[], const AuroraConfig* config) {
  return aurora::initialize(argc, argv, *config);
}
void aurora_shutdown() { aurora::shutdown(); }
const AuroraEvent* aurora_update() { return aurora::update(); }
bool aurora_begin_frame() { return aurora::begin_frame(); }
void aurora_end_frame() { aurora::end_frame(); }
SDL_Window* aurora_get_window() { return aurora::window::get_sdl_window(); }
AuroraWindowSize aurora_get_window_size() { return aurora::window::get_window_size(); }

void aurora_set_surface_size(uint32_t width, uint32_t height, float scale) {
#ifdef AURORA_ENABLE_GX
  if (aurora::window::surface_type() == AURORA_SURFACE_SDL_MANAGED) {
    aurora::Log.warn("aurora_set_surface_size: use VISetWindowSize for SDL managed windows");
    return;
  }

  const auto oldSize = aurora::gfx::efb::pending_size();
  aurora::gfx::resize_present_target(aurora::gfx::xfb_target(), width, height, scale);
  aurora::gfx::efb::set_display_size(width, height, scale);
  aurora::window::queue_size_events(oldSize, aurora::gfx::efb::pending_size());
#endif
}

bool aurora_set_surface(const AuroraSurfaceDesc* desc) {
#ifdef AURORA_ENABLE_GX
  std::lock_guard lock{aurora::g_surfaceChangeMutex};
  const AuroraSurfaceDesc next = desc ? *desc : AuroraSurfaceDesc{.type = AURORA_SURFACE_HEADLESS};
  if (!aurora::webgpu::g_device) {
    return false;
  }

  const auto oldSize = aurora::gfx::efb::pending_size();
  if (!aurora::gfx::present::replace_xfb_surface(next)) {
    return false;
  }

  aurora::window::set_surface_type(next.type);
  aurora::window::queue_size_events(oldSize, aurora::gfx::efb::pending_size());
  return true;
#else
  return false;
#endif
}

AuroraBackend aurora_get_backend() { return aurora::g_config.desiredBackend; }
const AuroraBackend* aurora_get_available_backends(size_t* count) {
  if (count != nullptr) {
    *count = aurora::PreferredBackendOrder.size();
  }
  return aurora::PreferredBackendOrder.data();
}
void aurora_set_log_level(AuroraLogLevel level) { aurora::g_config.logLevel = level; }
void aurora_set_pause_on_focus_lost(bool value) { aurora::g_config.pauseOnFocusLost = value; }
void aurora_set_background_input(bool value) {
  aurora::g_config.allowJoystickBackgroundEvents = value;
  aurora::window::set_background_input(value);
}
void aurora_set_resampler(AuroraSampler sampler) {
#ifdef AURORA_ENABLE_GX
  aurora::webgpu::set_resampler(sampler);
#else
  (void)sampler;
#endif
}
void aurora_set_timescale(float scale) { aurora::time::set_scale(scale); }
float aurora_get_timescale() { return aurora::time::scale(); }

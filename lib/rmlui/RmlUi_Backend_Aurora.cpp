#include <RmlUi_Backend.h>
#include <RmlUi_Platform_SDL.h>

#include "WebGPURenderInterface.hpp"
#include "SystemInterface_Aurora.h"

namespace Backend {

aurora::rmlui::WebGPURenderInterface* g_RenderInterface = nullptr;
aurora::rmlui::SystemInterface_Aurora* g_SystemInterface = nullptr;

bool Initialize(const char* window_name, int width, int height, bool allow_resize) {
  g_RenderInterface = new aurora::rmlui::WebGPURenderInterface();
  g_SystemInterface = new aurora::rmlui::SystemInterface_Aurora();
  return true;
}

void Shutdown() {
  delete g_SystemInterface;
  delete g_RenderInterface;
}

Rml::SystemInterface* GetSystemInterface() { return g_SystemInterface; }

Rml::RenderInterface* GetRenderInterface() { return g_RenderInterface; }

bool ProcessEvents(Rml::Context* context, KeyDownCallback key_down_callback, bool power_save) {
  return true; // events handled directly by aurora
}

void RequestExit() {}

void BeginFrame() {}

void PresentFrame() {}

} // namespace Backend

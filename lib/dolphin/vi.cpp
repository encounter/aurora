#include <dolphin/vi.h>

#include "../window.hpp"

void VIInit() {}
u32 VIGetTvFormat() { return 0; }
void VIFlush() {}

void VISetWindowTitle(const char* title) { aurora::window::set_title(title); }
void VISetWindowFullscreen(bool fullscreen) { aurora::window::set_fullscreen(fullscreen); }
bool VIGetWindowFullscreen() { return aurora::window::get_fullscreen(); }

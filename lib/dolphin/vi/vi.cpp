#include <dolphin/vi.h>

#include "../../window.hpp"

extern "C" {
void VIInit() {}
u32 VIGetTvFormat() { return 0; }
void VIFlush() {}

void VISetWindowTitle(const char* title) { aurora::window::set_title(title); }
void VISetWindowFullscreen(bool fullscreen) { aurora::window::set_fullscreen(fullscreen); }
bool VIGetWindowFullscreen() { return aurora::window::get_fullscreen(); }
void VILockAspectRatio(int width, int height) { return aurora::window::lock_aspect_ratio(width, height); }
void VIUnlockAspectRatio() { return aurora::window::unlock_aspect_ratio(); }
}

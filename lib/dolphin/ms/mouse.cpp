#include "dolphin/ms.h"
#include <SDL3/SDL_mouse.h>

#include "../../input.hpp"

extern "C" {
static MSStatus gStatus{};

void MSPoll() {
  const uint32_t buttons = SDL_GetGlobalMouseState(&gStatus.x, &gStatus.y);
  SDL_GetRelativeMouseState(&gStatus.xrel, &gStatus.yrel);
  aurora::input::get_mouse_scroll(&gStatus.scrollX, &gStatus.scrollY);

  gStatus.buttons = 0;
  if (buttons & SDL_BUTTON_LEFT) {
    gStatus.buttons |= MS_BUTTON_LEFT;
  }
  if (buttons & SDL_BUTTON_MIDDLE) {
    gStatus.buttons |= MS_BUTTON_MIDDLE;
  }
  if (buttons & SDL_BUTTON_RIGHT) {
    gStatus.buttons |= MS_BUTTON_RIGHT;
  }
  if (buttons & SDL_BUTTON_X1) {
    gStatus.buttons |= MS_BUTTON_X1;
  }
  if (buttons & SDL_BUTTON_X2) {
    gStatus.buttons |= MS_BUTTON_X2;
  }
}

void MSRead(MSStatus* status) { *status = gStatus; }
}
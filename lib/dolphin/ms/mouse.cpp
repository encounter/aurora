#include "dolphin/ms.h"
#include <SDL3/SDL_mouse.h>

#include "../../input.hpp"

extern "C" {
void MSRead(MSStatus* status) {
  const uint32_t buttons = SDL_GetGlobalMouseState(&status->x, &status->y);
  SDL_GetRelativeMouseState(&status->xrel, &status->yrel);
  aurora::input::get_mouse_scroll(&status->scrollX, &status->scrollY);

  status->buttons = 0;
  if (buttons & SDL_BUTTON_LEFT) {
    status->buttons |= MS_BUTTON_LEFT;
  }
  if (buttons & SDL_BUTTON_MIDDLE) {
    status->buttons |= MS_BUTTON_MIDDLE;
  }
  if (buttons & SDL_BUTTON_RIGHT) {
    status->buttons |= MS_BUTTON_RIGHT;
  }
  if (buttons & SDL_BUTTON_X1) {
    status->buttons |= MS_BUTTON_X1;
  }
  if (buttons & SDL_BUTTON_X2) {
    status->buttons |= MS_BUTTON_X2;
  }
}
}
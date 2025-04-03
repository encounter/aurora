#ifndef AURORA_EVENT_H
#define AURORA_EVENT_H

#include "aurora.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_joystick.h>

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#endif

typedef enum {
  AURORA_NONE,
  AURORA_EXIT,
  AURORA_SDL_EVENT,
  AURORA_WINDOW_MOVED,
  AURORA_WINDOW_RESIZED,
  AURORA_CONTROLLER_ADDED,
  AURORA_CONTROLLER_REMOVED,
  AURORA_PAUSED,
  AURORA_UNPAUSED,
} AuroraEventType;

struct AuroraEvent {
  AuroraEventType type;
  union {
    SDL_Event sdl;
    AuroraWindowPos windowPos;
    AuroraWindowSize windowSize;
    SDL_JoystickID controller;
  };
};

#ifdef __cplusplus
}
#endif

#endif

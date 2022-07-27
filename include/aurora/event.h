#ifndef AURORA_EVENT_H
#define AURORA_EVENT_H

#include "aurora.h"

#include <SDL_events.h>

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
    AuroraWindowSize windowSize;
    int32_t controller;
  };
};

#ifdef __cplusplus
}
#endif

#endif

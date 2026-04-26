#ifndef AURORA_GFX_H
#define AURORA_GFX_H

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#endif

#ifndef NDEBUG
#define AURORA_GFX_DEBUG_GROUPS
#endif

void push_debug_group(const char* label);
void pop_debug_group();

typedef struct {
  uint32_t queuedPipelines;
  uint32_t createdPipelines;
  uint32_t drawCallCount;
  uint32_t mergedDrawCallCount;
  uint32_t lastVertSize;
  uint32_t lastUniformSize;
  uint32_t lastIndexSize;
  uint32_t lastStorageSize;
  uint32_t lastTextureUploadSize;
} AuroraStats;

const AuroraStats* aurora_get_stats();

void aurora_enable_vsync(bool enabled);

void aurora_set_enhanced_lighting(bool enabled);
bool aurora_get_enhanced_lighting();

void aurora_set_specular_lighting(bool enabled);
bool aurora_get_specular_lighting();
void aurora_set_rim_lighting(bool enabled);
bool aurora_get_rim_lighting();
void aurora_set_specular_intensity(float intensity);
float aurora_get_specular_intensity();
void aurora_set_rim_intensity(float intensity);
float aurora_get_rim_intensity();
void aurora_set_ambient_multiplier(float multiplier);
float aurora_get_ambient_multiplier();
void aurora_set_diffuse_multiplier(float multiplier);
float aurora_get_diffuse_multiplier();

#ifdef __cplusplus
}
#endif

#endif
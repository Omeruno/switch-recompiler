#ifndef FREAKOUT_NATIVE_GPU_H
#define FREAKOUT_NATIVE_GPU_H

#include "game.h"
#include "shader.h"

#ifdef __SWITCH__
typedef struct {
    float position[4];
    ShaderVaryings varying;
} GpuVertex;

bool gpu_start(Game *game);
void gpu_close(Game *game);
void gpu_clear(Game *game, unsigned flags, uint32_t color, float depth);
bool gpu_present(Game *game);
bool gpu_fallback(Game *game, const char *reason);
bool gpu_draw(Game *game, const GpuVertex *vertices, unsigned count,
              const Surface *const textures[16], unsigned sampler_mask);
#endif

#endif

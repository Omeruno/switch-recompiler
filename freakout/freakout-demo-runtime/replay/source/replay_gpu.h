#ifndef FREAKOUT_REPLAY_GPU_H
#define FREAKOUT_REPLAY_GPU_H

#include <deko3d.h>

enum { REPLAY_SHADER_STRIDE = 0x10000 };

DkMemBlock replay_memory(DkDevice device, uint32_t bytes, uint32_t flags);
bool replay_load_shader(DkMemBlock code, DkShader *shader, uint32_t offset, const char *path);

#endif

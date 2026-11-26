#ifndef FREAKOUT_NATIVE_SHADER_H
#define FREAKOUT_NATIVE_SHADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef float ShaderVec __attribute__((vector_size(16)));

enum { SHADER_COLORS = 2, SHADER_TEXCOORDS = 8, SHADER_VARYINGS = SHADER_COLORS + SHADER_TEXCOORDS };
typedef struct { ShaderVec value[SHADER_VARYINGS]; } ShaderVaryings;

typedef struct {
    uint32_t instruction, destination, source[3];
    uint8_t relative[3];
} ShaderInstruction;

typedef struct {
    ShaderVec constants[256];
    ShaderInstruction instructions[256];
    uint16_t inputs[16];
    unsigned count, samplers, major;
    bool vertex;
} Shader;

typedef ShaderVec (*ShaderSampler)(void *, unsigned, ShaderVec);

bool shader_prepare(Shader *shader, const uint32_t *tokens, size_t bytes, bool vertex, const void *constants);
bool shader_vertex(const Shader *shader, const ShaderVec inputs[16], ShaderVec *position, ShaderVaryings *output);
ShaderVec shader_pixel(const Shader *shader, const ShaderVaryings *input, ShaderSampler sample, void *context);
#endif

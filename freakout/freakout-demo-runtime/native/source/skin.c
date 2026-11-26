#include "game.h"
#include <math.h>
#include <string.h>

#define SKIN_VTABLE 0x006b7f50u

static bool word(Game *g, uint32_t address, uint32_t *value)
{
    const uint8_t *source = game_bytes(g, address, 4);
    if (!source)
        return false;
    memcpy(value, source, 4);
    return true;
}

static bool observed(const Game *g, uint32_t skin)
{
    for (unsigned i = 0; i < g->deformed_count; ++i)
        if (g->deformed_skins[i] == skin)
            return true;
    return false;
}

void skin_observe(Game *g)
{
    if (g->deformed_frame != g->presents) {
        g->deformed_frame = g->presents;
        g->deformed_count = 0;
    }
    if (g->cpu.eip == FO_SKIN_DEFORM) {
        uint32_t skin = g->cpu.ecx, type;
        if (skin && word(g, skin, &type) && type == SKIN_VTABLE && !observed(g, skin) &&
            g->deformed_count < sizeof g->deformed_skins / sizeof *g->deformed_skins)
            g->deformed_skins[g->deformed_count++] = skin;
        return;
    }

    uint32_t skin, mode, renderer;
    if (!g->deformed_count || !word(g, g->cpu.ebp + 0x14, &skin) || !observed(g, skin) ||
        !word(g, skin + 0x28, &mode) || mode || !word(g, g->cpu.edx + 0x34, &renderer) || !renderer)
        return;
    float matrices[3][16];
    const uint8_t *raw = game_bytes(g, renderer + 0x780, sizeof matrices);
    if (!raw)
        return;
    memcpy(matrices, raw, sizeof matrices);
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned k = 0; k < 16; ++k)
            if (!isfinite(matrices[i][k]))
                return;

    // The guest's software path has already posed the mesh in object space.
    // Supply the world transform as the only bone and upload view times projection.
    for (unsigned a = 0; a < 4; ++a) {
        for (unsigned b = 0; b < 4; ++b) {
            double sum = 0;
            for (unsigned i = 0; i < 4; ++i)
                sum += (double)matrices[1][b * 4 + i] * matrices[2][i * 4 + a];
            g->software_skin_constants[a][b] = sum;
            if (a < 3)
                g->software_skin_constants[a + 4][b] = matrices[0][b * 4 + a];
        }
    }
    g->software_skin_draw = g->draws + 1;
}

bool skin_constants(Game *g, void *constants, uint32_t vertex_shader)
{
    static const uint8_t expected[32] = {
        0xe4, 0x66, 0x11, 0xcd, 0x55, 0x2c, 0x09, 0x1a, 0x81, 0x74, 0xa4, 0xb6, 0x7e, 0x2c, 0x66, 0x24,
        0xed, 0x6a, 0x8e, 0x55, 0xdb, 0xc3, 0x40, 0x80, 0x2a, 0x59, 0xe0, 0x8e, 0x6c, 0xbd, 0x71, 0x2c,
    };
    if (g->software_skin_draw != g->draws + 1 || g->deformed_frame != g->presents || g->stride != 72)
        return false;
    size_t bytes;
    const void *tokens = program_data(g, vertex_shader, &bytes);
    if (!tokens || bytes != 1232)
        return false;
    uint8_t digest[32];
    hash_bytes(tokens, bytes, digest);
    if (memcmp(digest, expected, sizeof digest))
        return false;
    g->software_skin_draw = 0;
    memcpy(constants, g->software_skin_constants, 64);
    memcpy((uint8_t *)constants + 12 * 16, g->software_skin_constants + 4, 48);
    if (g->program_draws < 8)
        fputs("Using the guest's software-skinned vertex stream\n", g->log);
    return true;
}

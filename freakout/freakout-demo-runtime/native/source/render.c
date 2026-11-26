#include "game.h"
#include "shader.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float position[4], u, v, tint[4]; ShaderVaryings varying; } ClipVertex;
typedef struct { float x, y, z, inv_w, u, v, tint[4]; ShaderVaryings varying; } Vertex;

typedef struct {
    Shader vertex, pixel;
    struct { uint16_t offset; uint8_t type; bool present; } attributes[16];
    const Surface *textures[16];
    const Game *game;
    unsigned fragments;
    bool software_skin;
} ShaderDraw;

typedef struct {
    uint32_t *color;
    float *depth;
    unsigned width, height, depth_width;
    bool alpha;
} DrawTarget;

static uint32_t rgba(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha)
{
    return red | green << 8 | blue << 16 | alpha << 24;
}

bool render_allocate(Game *g)
{
    if (g->pixels && g->depth_pixels)
        return true;
    if (!g->back.width || !g->back.height || g->back.width > 1920 || g->back.height > 1080)
        return false;
    size_t count = (size_t)g->back.width * g->back.height;
    g->pixels = calloc(count, sizeof *g->pixels);
    g->depth_pixels = malloc(count * sizeof *g->depth_pixels);
    if (!g->pixels || !g->depth_pixels)
        return false;
    for (size_t i = 0; i < count; ++i)
        g->depth_pixels[i] = 1.0f;
    return true;
}

static bool draw_target(Game *g, DrawTarget *target, bool need_depth)
{
    Surface *color = d3d_surface_lookup(g, g->color_target, NULL);
    if (!color || (color->format != 21 && color->format != 22))
        return false;
    *target = (DrawTarget){.width = color->width, .height = color->height, .alpha = color->format == 21};
    if (color == &g->back) {
        if (!render_allocate(g))
            return false;
        target->color = g->pixels;
    } else {
        if (!color->decoded)
            color->decoded = calloc((size_t)color->width * color->height, sizeof *color->decoded);
        if (!color->decoded)
            return false;
        target->color = color->decoded;
    }
    if (!need_depth)
        return true;
    Surface *depth = d3d_surface_lookup(g, g->depth_target, NULL);
    if (!depth || depth->width < color->width || depth->height < color->height)
        return false;
    target->depth_width = depth->width;
    if (depth == &g->depth) {
        if (!render_allocate(g))
            return false;
        target->depth = g->depth_pixels;
    } else {
        if (!depth->depth_samples) {
            size_t count = (size_t)depth->width * depth->height;
            depth->depth_samples = malloc(count * sizeof(float));
            if (!depth->depth_samples)
                return false;
            for (size_t i = 0; i < count; ++i)
                depth->depth_samples[i] = 1.f;
        }
        target->depth = depth->depth_samples;
    }
    return true;
}

bool render_clear(Game *g, unsigned flags, uint32_t color, float depth)
{
    DrawTarget target;
    if (!draw_target(g, &target, flags & 2))
        return false;
    size_t count = (size_t)target.width * target.height;
    if (flags & 1) {
        uint32_t value = rgba((color >> 16) & 255, (color >> 8) & 255, color & 255, target.alpha ? color >> 24 : 255);
        for (size_t i = 0; i < count; ++i)
            target.color[i] = value;
    }
    if (flags & 2)
        for (unsigned y = 0; y < target.height; ++y)
            for (unsigned x = 0; x < target.width; ++x)
                target.depth[y * target.depth_width + x] = depth;
    return true;
}

static uint32_t rgb565(unsigned value)
{
    unsigned red = (value >> 11) & 31, green = (value >> 5) & 63, blue = value & 31;
    return rgba((red << 3) | (red >> 2), (green << 2) | (green >> 4),
                (blue << 3) | (blue >> 2), 255);
}

static uint32_t mix_color(uint32_t a, uint32_t b, unsigned wa, unsigned wb)
{
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        unsigned first = (a >> shift) & 255, second = (b >> shift) & 255;
        result |= ((wa * first + wb * second) / (wa + wb)) << shift;
    }
    return result;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void decode_block(const uint8_t *data, unsigned mode, uint32_t block[16])
{
    const uint8_t *rgb = data + (mode == 1 ? 0 : 8);
    unsigned first = get16(rgb), second = get16(rgb + 2);
    uint32_t palette[4] = {rgb565(first), rgb565(second)};
    if (mode != 1 || first > second) {
        palette[2] = mix_color(palette[0], palette[1], 2, 1);
        palette[3] = mix_color(palette[0], palette[1], 1, 2);
    } else {
        palette[2] = mix_color(palette[0], palette[1], 1, 1);
    }
    unsigned alpha[8] = {data[0], data[1]};
    uint64_t codes = 0;
    if (mode == 5) {
        unsigned steps = alpha[0] > alpha[1] ? 7 : 5;
        for (unsigned i = 1; i < steps; ++i)
            alpha[i + 1] = ((steps - i) * alpha[0] + i * alpha[1]) / steps;
        if (steps == 5)
            alpha[7] = 255;
        for (unsigned i = 0; i < 6; ++i)
            codes |= (uint64_t)data[2 + i] << (8 * i);
    }
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t color = palette[(rgb[4 + i / 4] >> ((i % 4) * 2)) & 3];
        if (mode == 3)
            color = (color & 0xffffff) | (((data[i / 2] >> ((i & 1) * 4) & 15) * 17u) << 24);
        if (mode == 5)
            color = (color & 0xffffff) | alpha[(codes >> (3 * i)) & 7] << 24;
        block[i] = color;
    }
}

uint32_t *decode_texture(Game *g, const Surface *s)
{
    if (!s || !s->data || !s->width || !s->height)
        return NULL;
    unsigned mode = s->format == 0x31545844u ? 1 : s->format == 0x33545844u ? 3 :
                    s->format == 0x35545844u ? 5 : 0;
    if (!mode && s->format != 20 && s->format != 21 && s->format != 22)
        return NULL;
    uint32_t *pixels = malloc((size_t)s->width * s->height * sizeof *pixels);
    if (!pixels)
        return NULL;
    const uint8_t *data = game_bytes(g, s->data, surface_bytes(s));
    if (!data) {
        free(pixels);
        return NULL;
    }
    if (!mode) {
        unsigned pitch = surface_pitch(s), bytes = s->format == 20 ? 3 : 4;
        for (unsigned y = 0; y < s->height; ++y) {
            const uint8_t *row = data + y * pitch;
            for (unsigned x = 0; x < s->width; ++x, row += bytes)
                pixels[y * s->width + x] = rgba(row[2], row[1], row[0], s->format == 21 ? row[3] : 255);
        }
        return pixels;
    }
    for (unsigned y = 0; y < s->height; y += 4) {
        for (unsigned x = 0; x < s->width; x += 4) {
            uint32_t block[16];
            decode_block(data, mode, block);
            data += mode == 1 ? 8 : 16;
            for (unsigned dy = 0; dy < 4 && y + dy < s->height; ++dy)
                for (unsigned dx = 0; dx < 4 && x + dx < s->width; ++dx)
                    pixels[(y + dy) * s->width + x + dx] = block[dy * 4 + dx];
        }
    }
    return pixels;
}

static int coord(int value, int size, unsigned mode)
{
    if (mode == 3)
        return value < 0 ? 0 : value >= size ? size - 1 : value;
    int period = size * (mode == 2 ? 2 : 1);
    int remainder = value % period;
    if (remainder < 0)
        remainder += period;
    return remainder < size ? remainder : period - remainder - 1;
}

static uint32_t sample(const Game *g, unsigned slot, const Surface *t, const uint32_t *image, float u, float v)
{
    const float x = u * t->width - 0.5f, y = v * t->height - 0.5f;
    const int ix = floorf(x), iy = floorf(y);
    unsigned u_mode = g->samplers[slot][1], v_mode = g->samplers[slot][2];
    unsigned ax = coord(ix, t->width, u_mode), bx = coord(ix + 1, t->width, u_mode);
    unsigned ay = coord(iy, t->height, v_mode) * t->width, by = coord(iy + 1, t->height, v_mode) * t->width;
    unsigned fx = (x - ix) * 256, fy = (y - iy) * 256;
    uint32_t top = mix_color(image[ay + ax], image[ay + bx], 256 - fx, fx);
    uint32_t bottom = mix_color(image[by + ax], image[by + bx], 256 - fx, fx);
    return mix_color(top, bottom, 256 - fy, fy);
}

static Surface *texture_at(Game *g, unsigned slot)
{
    uint32_t handle = g->texture_slots[slot];
    for (unsigned i = 0; handle && i < MAX_TEXTURES; ++i) {
        if (g->textures[i].handle == handle) {
            Surface *surface = &g->textures[i].mip[0];
            if (!surface->decoded)
                surface->decoded = decode_texture(g, surface);
            return surface->decoded ? surface : NULL;
        }
    }
    return NULL;
}

static ShaderVec shade_sample(void *context, unsigned slot, ShaderVec uv)
{
    const ShaderDraw *draw = context;
    const Surface *surface = draw->textures[slot];
    if (!isfinite(uv[0]) || !isfinite(uv[1]))
        return (ShaderVec){0};
    for (unsigned i = 0; i < 2; ++i) {
        switch (draw->game->samplers[slot][1 + i]) {
        case 2: {
            float fraction = uv[i] * 0.5f - floorf(uv[i] * 0.5f);
            uv[i] = 1.f - fabsf(1.f - 2.f * fraction);
            break;
        }
        case 3: uv[i] = fminf(fmaxf(uv[i], 0.f), 1.f); break;
        default: uv[i] -= floorf(uv[i]); break;
        }
    }
    uint32_t rgba;
    if (draw->game->samplers[slot][5] == 1 && draw->game->samplers[slot][6] == 1) {
        unsigned x = fminf(uv[0] * surface->width, surface->width - 1);
        unsigned y = fminf(uv[1] * surface->height, surface->height - 1);
        rgba = surface->decoded[y * surface->width + x];
    } else {
        rgba = sample(draw->game, slot, surface, surface->decoded, uv[0], uv[1]);
    }
    return (ShaderVec){rgba & 255, rgba >> 8 & 255, rgba >> 16 & 255, rgba >> 24} * (1.f / 255.f);
}

static bool prepare_shaders(Game *g, ShaderDraw *draw)
{
    draw->game = g;
    size_t bytes;
    const uint32_t *vertex = program_data(g, g->vertex_shader, &bytes);
    if (!shader_prepare(&draw->vertex, vertex, bytes, true, g->vertex_constants)) {
        fprintf(g->log, "Unsupported vertex shader bytecode: %08x (%zu bytes)\n", g->vertex_shader, bytes);
        return false;
    }
    draw->software_skin = skin_constants(g, draw->vertex.constants, g->vertex_shader);
    const uint32_t *pixel = program_data(g, g->pixel_shader, &bytes);
    if (!shader_prepare(&draw->pixel, pixel, bytes, false, g->pixel_constants)) {
        fprintf(g->log, "Unsupported pixel shader bytecode: %08x (%zu bytes)\n", g->pixel_shader, bytes);
        return false;
    }
    const uint8_t *declaration = program_data(g, g->declaration, &bytes);
    if (!declaration || bytes < 16)
        return false;
    for (unsigned i = 0; i < 16; ++i) {
        unsigned binding = draw->vertex.inputs[i];
        if (!binding)
            continue;
        const uint8_t *element = declaration, *end = declaration + bytes - 8;
        while (element < end && (element[6] != (binding & 15) || element[7] != (binding >> 4 & 15)))
            element += 8;
        unsigned offset = get16(element + 2), type = element[4];
        unsigned length = type == 4 ? 4 : (type + 1) * 4;
        if (element == end || get16(element) || element[5] || type > 4 || offset + length > g->stride) {
            fprintf(g->log, "Unsupported vertex element v%u: stream=%u offset=%u type=%u semantic=%02x\n",
                    i, get16(element), offset, type, binding & 255);
            return false;
        }
        draw->attributes[i].present = true;
        draw->attributes[i].type = type;
        draw->attributes[i].offset = offset;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (!(draw->pixel.samplers & (1u << i)))
            continue;
        unsigned u = g->samplers[i][1], v = g->samplers[i][2];
        draw->textures[i] = texture_at(g, i);
        if (!draw->textures[i] || u > 3 || v > 3) {
            fprintf(g->log, "Unsupported shader sampler %u: texture=%08x address=%u,%u\n",
                    i, g->texture_slots[i], u, v);
            return false;
        }
    }
    return true;
}

static bool read_program_vertex(const ShaderDraw *draw, const uint8_t *source, ClipVertex *out)
{
    ShaderVec values[16] = {{0}}, position;
    uint8_t skinned[72];
    if (draw->software_skin) {
        memcpy(skinned, source, sizeof skinned);
        memset(skinned + 12, 0, 16);
        source = skinned;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (!draw->attributes[i].present)
            continue;
        const uint8_t *field = source + draw->attributes[i].offset;
        if (draw->attributes[i].type == 4)
            values[i] = (ShaderVec){field[2], field[1], field[0], field[3]} * (1.f / 255.f);
        else {
            values[i][3] = 1.f;
            memcpy(&values[i], field, 4 * (draw->attributes[i].type + 1));
        }
    }
    if (!shader_vertex(&draw->vertex, values, &position, &out->varying))
        return false;
    memcpy(out->position, &position, sizeof position);
    return true;
}

static void transform(float vertex[4], const uint32_t *state)
{
    float matrix[16], result[4];
    memcpy(matrix, state, sizeof matrix);
    for (unsigned col = 0; col < 4; ++col)
        result[col] = vertex[0] * matrix[col] + vertex[1] * matrix[col + 4] +
                      vertex[2] * matrix[col + 8] + vertex[3] * matrix[col + 12];
    memcpy(vertex, result, sizeof result);
}

static bool read_vertex(const Game *g, const uint8_t *source, ClipVertex *out)
{
    float position[] = {0, 0, 0, 1}, uv[] = {0, 0, 1, 0};
    memcpy(position, source, 12);
    bool colored = (g->fvf & 0x40) != 0;
    bool screen = (g->fvf & 0x0e) == 4;
    unsigned offset = screen ? 16 : (g->fvf & 0x10) ? 24 : 12;
    memcpy(uv, source + offset + (colored ? 4 : 0), 8);
    uint32_t tint = UINT32_MAX;
    if (colored) {
        uint32_t argb;
        memcpy(&argb, source + offset, 4);
        tint = rgba(argb >> 16 & 255, argb >> 8 & 255, argb & 255, argb >> 24);
    }
    if (screen) {
        float reciprocal, near, far;
        memcpy(&reciprocal, source + 12, 4);
        memcpy(&near, &g->viewport[4], 4);
        memcpy(&far, &g->viewport[5], 4);
        if (!(reciprocal > 0) || !g->viewport[2] || !g->viewport[3] || !(far > near))
            return false;
        position[3] = 1.f / reciprocal;
        position[0] = ((position[0] + 0.5f - g->viewport[0]) * 2.f / g->viewport[2] - 1.f) * position[3];
        position[1] = (1.f - (position[1] + 0.5f - g->viewport[1]) * 2.f / g->viewport[3]) * position[3];
        position[2] = (position[2] - near) / (far - near) * position[3];
    } else {
        transform(position, g->transforms[256]);
        transform(position, g->transforms[2]);
        transform(position, g->transforms[3]);
    }
    if (g->stages[0][24] == 2)
        transform(uv, g->transforms[16]);
    for (unsigned i = 0; i < 4; ++i) {
        if (!isfinite(position[i]))
            return false;
        out->position[i] = position[i];
        out->tint[i] = (tint >> (i * 8)) & 255;
    }
    out->u = uv[0];
    out->v = uv[1];
    return isfinite(out->u) && isfinite(out->v) && fabsf(out->u) < 4096 && fabsf(out->v) < 4096;
}

static uint32_t modulate(uint32_t color, const Vertex a, const Vertex b, const Vertex c,
                         float wa, float wb, float wc)
{
    uint32_t out = 0;
    for (unsigned lane = 0; lane < 4; ++lane) {
        unsigned shift = 8 * lane;
        float tint = a.tint[lane] * wa + b.tint[lane] * wb + c.tint[lane] * wc;
        unsigned value = ((color >> shift) & 255) * fminf(tint, 255.0f) * (1.0f / 255);
        out |= value << shift;
    }
    return out;
}

static unsigned blend_factor(unsigned code, unsigned src, unsigned dst, unsigned src_alpha, unsigned dst_alpha)
{
    switch (code) {
    case 1: return 0;
    case 2: return 255;
    case 3: return src;
    case 4: return 255 - src;
    case 5: return src_alpha;
    case 6: return 255 - src_alpha;
    case 7: return dst_alpha;
    case 8: return 255 - dst_alpha;
    case 9: return dst;
    case 10: return 255 - dst;
    default: return src_alpha < 255 - dst_alpha ? src_alpha : 255 - dst_alpha;
    }
}

static uint32_t blend(const Game *g, uint32_t src, uint32_t dst)
{
    uint32_t packed = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        unsigned a = src >> shift & 255, b = dst >> shift & 255;
        unsigned sa = blend_factor(g->raster[19], a, b, src >> 24, dst >> 24);
        unsigned sb = blend_factor(g->raster[20], a, b, src >> 24, dst >> 24);
        unsigned value = (a * sa + b * sb) / 255;
        packed |= (value > 255 ? 255 : value) << shift;
    }
    return packed;
}

static Vertex viewport_vertex(const Game *g, const ClipVertex *input, bool programmable)
{
    const float *p = input->position;
    float inv = 1.0f / p[3];
    Vertex out = {.x = g->viewport[0] + (p[0] * inv * 0.5f + 0.5f) * g->viewport[2],
                  .y = g->viewport[1] + (0.5f - p[1] * inv * 0.5f) * g->viewport[3],
                  .z = p[2] * inv, .inv_w = inv, .u = input->u, .v = input->v};
    memcpy(out.tint, input->tint, sizeof out.tint);
    if (programmable)
        out.varying = input->varying;
    return out;
}

static float plane_distance(const ClipVertex *vertex, unsigned plane)
{
    const float *p = vertex->position;
    switch (plane) {
    case 0: return p[3] - 0.000001f;
    case 1: return p[3] + p[0];
    case 2: return p[3] - p[0];
    case 3: return p[3] + p[1];
    case 4: return p[3] - p[1];
    case 5: return p[2];
    default: return p[3] - p[2];
    }
}

static ClipVertex interpolate(const ClipVertex *a, const ClipVertex *b, float weight, bool programmable)
{
    ClipVertex out = {.u = a->u + (b->u - a->u) * weight,
                      .v = a->v + (b->v - a->v) * weight};
    for (unsigned i = 0; i < 4; ++i) {
        out.position[i] = a->position[i] + (b->position[i] - a->position[i]) * weight;
        out.tint[i] = a->tint[i] + (b->tint[i] - a->tint[i]) * weight;
    }
    if (programmable)
        for (unsigned i = 0; i < SHADER_VARYINGS; ++i)
            out.varying.value[i] = a->varying.value[i] + (b->varying.value[i] - a->varying.value[i]) * weight;
    return out;
}

static void triangle(Game *g, const DrawTarget *target, const Surface *texture, ShaderDraw *shader, Vertex a, Vertex b, Vertex c);

static void clip_triangle(Game *g, const DrawTarget *target, const Surface *surface, ShaderDraw *shader, const ClipVertex input[3])
{
    enum { PLANES = 7, MAX_POLYGON = 3 + PLANES };
    ClipVertex records[2][MAX_POLYGON];
    memcpy(records[0], input, 3 * sizeof *input);
    unsigned count = 3;
    for (unsigned plane = 0; plane < PLANES && count >= 3; ++plane) {
        ClipVertex *vertices = records[plane & 1], *next = records[(plane + 1) & 1];
        unsigned used = 0;
        const ClipVertex *previous = &vertices[count - 1];
        float previous_distance = plane_distance(previous, plane);
        for (unsigned i = 0; i < count; ++i) {
            const ClipVertex *current = &vertices[i];
            float distance = plane_distance(current, plane);
            if ((distance < 0) != (previous_distance < 0) && distance && previous_distance)
                next[used++] = interpolate(previous, current, previous_distance / (previous_distance - distance), shader != NULL);
            if (distance >= 0)
                next[used++] = *current;
            previous = current;
            previous_distance = distance;
        }
        count = used;
    }
    if (count < 3)
        return;
    const ClipVertex *vertices = records[PLANES & 1];
    Vertex first = viewport_vertex(g, &vertices[0], shader != NULL);
    Vertex previous = viewport_vertex(g, &vertices[1], shader != NULL);
    for (unsigned i = 2; i < count; ++i) {
        Vertex current = viewport_vertex(g, &vertices[i], shader != NULL);
        triangle(g, target, surface, shader, first, previous, current);
        previous = current;
    }
}

static void triangle(Game *g, const DrawTarget *target, const Surface *t, ShaderDraw *shader, Vertex a, Vertex b, Vertex c)
{
    float bx = b.x - a.x, by = b.y - a.y, cx = c.x - a.x, cy = c.y - a.y;
    float area = bx * cy - by * cx;
    if (!isfinite(area) || area == 0 || (g->raster[22] == 2 && area > 0) || (g->raster[22] == 3 && area < 0))
        return;
    uint32_t color_mask = 0;
    for (unsigned channel = 0; channel < 4; ++channel)
        if (g->raster[D3D_COLOR_WRITE_ENABLE] & (1u << channel))
            color_mask |= 255u << (channel * 8);
    float inverse = 1.0f / area;
    int x0 = fmaxf(0, fminf(target->width, floorf(fminf(a.x, fminf(b.x, c.x)))));
    int x1 = fmaxf(0, fminf(target->width, ceilf(fmaxf(a.x, fmaxf(b.x, c.x)))));
    int y0 = fmaxf(0, fminf(target->height, floorf(fminf(a.y, fminf(b.y, c.y)))));
    int y1 = fmaxf(0, fminf(target->height, ceilf(fmaxf(a.y, fmaxf(b.y, c.y)))));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - a.x, dy = y + 0.5f - a.y;
            float wb = (cy * dx - cx * dy) * inverse, wc = (bx * dy - by * dx) * inverse;
            float wa = 1.0f - wb - wc;
            if (wa < 0 || wb < 0 || wc < 0)
                continue;
            float z = a.z * wa + b.z * wb + c.z * wc;
            unsigned pixel = y * target->width + x, depth_index = y * target->depth_width + x;
            if (z < 0 || z > 1.000001f || (target->depth && z > target->depth[depth_index] + 0.0000001f))
                continue;
            wa *= a.inv_w;
            wb *= b.inv_w;
            wc *= c.inv_w;
            float weight = 1.0f / (wa + wb + wc);
            uint32_t value;
            if (shader) {
                ShaderVaryings varyings;
                for (unsigned i = 0; i < SHADER_VARYINGS; ++i)
                    varyings.value[i] = (a.varying.value[i] * wa + b.varying.value[i] * wb + c.varying.value[i] * wc) * weight;
                ShaderVec color = shader_pixel(&shader->pixel, &varyings, shade_sample, shader);
                value = rgba(fminf(fmaxf(color[0], 0.f), 1.f) * 255.f,
                             fminf(fmaxf(color[1], 0.f), 1.f) * 255.f,
                             fminf(fmaxf(color[2], 0.f), 1.f) * 255.f,
                             fminf(fmaxf(color[3], 0.f), 1.f) * 255.f);
            } else {
                value = t ? sample(g, 0, t, t->decoded, (a.u * wa + b.u * wb + c.u * wc) * weight,
                                   (a.v * wa + b.v * wb + c.v * wc) * weight) : UINT32_MAX;
                if (g->fvf & 0x40)
                    value = modulate(value, a, b, c, wa * weight, wb * weight, wc * weight);
            }
            if (g->raster[15]) {
                unsigned alpha = value >> 24, reference = g->raster[24] & 255;
                unsigned equality = g->raster[25];
                if (equality == 1 || (equality == 2 && alpha >= reference) ||
                    (equality == 3 && alpha != reference) || (equality == 4 && alpha > reference) ||
                    (equality == 5 && alpha <= reference) || (equality == 6 && alpha == reference) ||
                    (equality == 7 && alpha < reference))
                    continue;
            }
            if (g->raster[27])
                value = blend(g, value, target->color[pixel]);
            if (color_mask) {
                value = (value & color_mask) | (target->color[pixel] & ~color_mask);
                target->color[pixel] = target->alpha ? value : value | 0xff000000u;
            }
            if (shader)
                ++shader->fragments;
            if (target->depth && g->raster[14])
                target->depth[depth_index] = z;
        }
    }
}

bool render_geometry(Game *g, const Buffer *vb, const Buffer *ib)
{
    unsigned type = arg(g, 1), n = arg(g, ib ? 6 : 3);
    if (type < 4 || type > 6 || !n || n > 65536)
        return false;
    unsigned index_count = type == 4 ? 3 * n : n + 2;
    unsigned minimum = ib ? arg(g, 3) : 0, count = ib ? arg(g, 4) : index_count;
    unsigned first = ib ? arg(g, 5) : 0;
    int64_t base = ib ? (int32_t)arg(g, 2) + (int64_t)minimum : (int64_t)arg(g, 2);
    bool screen = (g->fvf & 0x0e) == 4;
    unsigned stride = 20 + (screen ? 4 : (g->fvf & 0x10) ? 12 : 0) + ((g->fvf & 0x40) ? 4 : 0);
    bool fixed = (g->fvf & ~0x50) == 0x102 || ((g->fvf & ~0x40) == 0x104);
    bool programmable = g->vertex_shader || g->pixel_shader;
    if (!g->stride ||
        (!programmable && (!fixed || g->stride != stride)) ||
        (ib && ib->format != 101 && ib->format != 102) || !count || count > 65536 || base < 0 ||
        (g->raster[7] && g->raster[23] != 4) ||
        (g->raster[27] && (g->raster[19] < 1 || g->raster[19] > 11 ||
                           g->raster[20] < 1 || g->raster[20] > 11 || g->raster[171] > 1)))
        return false;
    uint64_t start = g->stream_offset + base * g->stride, end = start + (uint64_t)count * g->stride;
    unsigned index_size = !ib ? 0 : ib->format == 102 ? 4 : 2;
    if (end > vb->bytes || (ib && ((uint64_t)first + index_count) * index_size > ib->bytes))
        return false;
    DrawTarget target;
    if (!draw_target(g, &target, g->raster[7]))
        return false;
    ShaderDraw shaders = {0};
    Surface *texture = programmable ? NULL : texture_at(g, 0);
    if (programmable ? !prepare_shaders(g, &shaders) : g->texture_slots[0] && !texture)
        return false;

    const uint8_t *indices = ib ? game_bytes(g, ib->data + first * index_size, index_count * index_size) : NULL;
    const uint8_t *vertices = game_bytes(g, vb->data + (uint32_t)start, count * g->stride);
    if (!vertices || (ib && !indices))
        return false;
#ifdef __SWITCH__
    if (g->presents < 8)
#endif
    {
        fprintf(g->log, "Draw #%u type=%u vertices=%u triangles=%u vb=", g->draws + 1, type, count, n);
        log_hash(g->log, vertices, count * g->stride);
        if (ib) {
            fputs(" ib=", g->log);
            log_hash(g->log, indices, index_count * index_size);
        }
        fputc('\n', g->log);
    }

    ClipVertex *cache = programmable ? calloc(count, sizeof *cache) : NULL;
    uint8_t *visible_cache = cache ? malloc(count) : NULL;
    if (cache && visible_cache)
        for (unsigned i = 0; i < count; ++i)
            visible_cache[i] = read_program_vertex(&shaders, vertices + i * g->stride, &cache[i]);
    bool valid = true;
    for (unsigned i = 0; i < n; ++i) {
        unsigned offset = type == 4 ? i * 3 : i;
        unsigned j = type == 5 ? i & 1 : 0;
        unsigned position[] = {type == 6 ? 0 : offset + j, offset + (1 ^ j), offset + 2};
        ClipVertex v[3] = {0};
        bool visible = true;
        for (unsigned k = 0; k < 3; ++k) {
            uint32_t id = position[k];
            if (ib) {
                const uint8_t *index = indices + position[k] * index_size;
                id = get16(index);
                if (index_size == 4)
                    id |= (uint32_t)get16(index + 2) << 16;
            }
            if (id < minimum || id - minimum >= count) {
                valid = false;
                break;
            }
            unsigned row = id - minimum;
            if (visible_cache) {
                visible &= visible_cache[row] != 0;
                v[k] = cache[row];
                continue;
            }
            const uint8_t *vertex = vertices + row * g->stride;
            if (!(programmable ? read_program_vertex(&shaders, vertex, &v[k]) : read_vertex(g, vertex, &v[k]))) {
                visible = false;
            }
        }
        if (!valid)
            break;
        if (visible)
            clip_triangle(g, &target, texture, programmable ? &shaders : NULL, v);
    }
    free(visible_cache);
    free(cache);
    if (!valid)
        return false;
    if (programmable) {
        ++g->program_draws;
        if (g->program_draws <= 8)
            fprintf(g->log, "Shader draw at Present #%u: %u shaded fragments\n", g->presents, shaders.fragments);
    }
    return true;
}

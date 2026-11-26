#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float position[4], u, v, tint[4]; } ClipVertex;
typedef struct { float x, y, z, inv_w, u, v, tint[4]; } Vertex;

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

bool render_clear(Game *g, unsigned flags, uint32_t color, float depth)
{
    if (!render_allocate(g))
        return false;
    size_t count = (size_t)g->back.width * g->back.height;
    if (flags & 1) {
        uint32_t value = rgba((color >> 16) & 255, (color >> 8) & 255, color & 255, 255);
        for (size_t i = 0; i < count; ++i)
            g->pixels[i] = value;
    }
    if (flags & 2)
        for (size_t i = 0; i < count; ++i)
            g->depth_pixels[i] = depth;
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

static void decode_block(const uint8_t *data, bool bc3, uint32_t block[16])
{
    const uint8_t *rgb = data + (bc3 ? 8 : 0);
    unsigned first = get16(rgb), second = get16(rgb + 2);
    uint32_t palette[4] = {rgb565(first), rgb565(second)};
    if (bc3 || first > second) {
        palette[2] = mix_color(palette[0], palette[1], 2, 1);
        palette[3] = mix_color(palette[0], palette[1], 1, 2);
    } else {
        palette[2] = mix_color(palette[0], palette[1], 1, 1);
    }
    unsigned alpha[8] = {data[0], data[1]};
    uint64_t codes = 0;
    if (bc3) {
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
        block[i] = bc3 ? (color & 0xffffff) | alpha[(codes >> (3 * i)) & 7] << 24 : color;
    }
}

static uint32_t *decode_texture(Game *g, const Surface *s)
{
    if (!s || !s->data || !s->width || !s->height)
        return NULL;
    bool bc3 = s->format == 0x35545844u;
    bool compressed = bc3 || s->format == 0x31545844u;
    if (!compressed && s->format != 21 && s->format != 22)
        return NULL;
    uint32_t *pixels = malloc((size_t)s->width * s->height * sizeof *pixels);
    if (!pixels)
        return NULL;
    const uint8_t *data = game_bytes(g, s->data, 1);
    if (!compressed) {
        for (size_t i = 0, count = (size_t)s->width * s->height; i < count; ++i, data += 4)
            pixels[i] = rgba(data[2], data[1], data[0], s->format == 21 ? data[3] : 255);
        return pixels;
    }
    for (unsigned y = 0; y < s->height; y += 4) {
        for (unsigned x = 0; x < s->width; x += 4) {
            uint32_t block[16];
            decode_block(data, bc3, block);
            data += bc3 ? 16 : 8;
            for (unsigned dy = 0; dy < 4 && y + dy < s->height; ++dy)
                for (unsigned dx = 0; dx < 4 && x + dx < s->width; ++dx)
                    pixels[(y + dy) * s->width + x + dx] = block[dy * 4 + dx];
        }
    }
    return pixels;
}

static int coord(int value, int size, bool clamp)
{
    if (clamp)
        return value < 0 ? 0 : value >= size ? size - 1 : value;
    int remainder = value % size;
    return remainder < 0 ? remainder + size : remainder;
}

static uint32_t sample(const Game *g, const Surface *t, const uint32_t *image, float u, float v)
{
    const float x = u * t->width - 0.5f, y = v * t->height - 0.5f;
    const int ix = floorf(x), iy = floorf(y);
    bool clamp_u = g->samplers[0][1] == 3, clamp_v = g->samplers[0][2] == 3;
    unsigned ax = coord(ix, t->width, clamp_u), bx = coord(ix + 1, t->width, clamp_u);
    unsigned ay = coord(iy, t->height, clamp_v) * t->width, by = coord(iy + 1, t->height, clamp_v) * t->width;
    unsigned fx = (x - ix) * 256, fy = (y - iy) * 256;
    uint32_t top = mix_color(image[ay + ax], image[ay + bx], 256 - fx, fx);
    uint32_t bottom = mix_color(image[by + ax], image[by + bx], 256 - fx, fx);
    return mix_color(top, bottom, 256 - fy, fy);
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
    bool colored = g->fvf == 0x152;
    memcpy(uv, source + (colored ? 28 : 24), 8);
    uint32_t tint = UINT32_MAX;
    if (colored) {
        uint32_t argb;
        memcpy(&argb, source + 24, 4);
        tint = rgba(argb >> 16 & 255, argb >> 8 & 255, argb & 255, argb >> 24);
    }
    transform(position, g->transforms[256]);
    transform(position, g->transforms[2]);
    transform(position, g->transforms[3]);
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

static Vertex viewport_vertex(const Game *g, const ClipVertex *input)
{
    const float *p = input->position;
    float inv = 1.0f / p[3];
    Vertex out = {
        g->viewport[0] + (p[0] * inv * 0.5f + 0.5f) * g->viewport[2],
        g->viewport[1] + (0.5f - p[1] * inv * 0.5f) * g->viewport[3],
        p[2] * inv, inv, input->u, input->v, {0},
    };
    memcpy(out.tint, input->tint, sizeof out.tint);
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

static ClipVertex interpolate(const ClipVertex *a, const ClipVertex *b, float weight)
{
    ClipVertex out = {.u = a->u + (b->u - a->u) * weight,
                      .v = a->v + (b->v - a->v) * weight};
    for (unsigned i = 0; i < 4; ++i) {
        out.position[i] = a->position[i] + (b->position[i] - a->position[i]) * weight;
        out.tint[i] = a->tint[i] + (b->tint[i] - a->tint[i]) * weight;
    }
    return out;
}

static void triangle(Game *g, const Surface *texture, const uint32_t *image, Vertex a, Vertex b, Vertex c);

static void clip_triangle(Game *g, const Surface *surface, const uint32_t *image, const ClipVertex input[3])
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
            if ((distance < 0) != (previous_distance < 0))
                next[used++] = interpolate(previous, current, previous_distance / (previous_distance - distance));
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
    Vertex first = viewport_vertex(g, &vertices[0]), previous = viewport_vertex(g, &vertices[1]);
    for (unsigned i = 2; i < count; ++i) {
        Vertex current = viewport_vertex(g, &vertices[i]);
        triangle(g, surface, image, first, previous, current);
        previous = current;
    }
}

static void triangle(Game *g, const Surface *t, const uint32_t *image, Vertex a, Vertex b, Vertex c)
{
    float bx = b.x - a.x, by = b.y - a.y, cx = c.x - a.x, cy = c.y - a.y;
    float area = bx * cy - by * cx;
    if (!isfinite(area) || area == 0 || (g->raster[22] == 2 && area > 0) || (g->raster[22] == 3 && area < 0))
        return;
    float inverse = 1.0f / area;
    int x0 = fmaxf(0, fminf(g->back.width, floorf(fminf(a.x, fminf(b.x, c.x)))));
    int x1 = fmaxf(0, fminf(g->back.width, ceilf(fmaxf(a.x, fmaxf(b.x, c.x)))));
    int y0 = fmaxf(0, fminf(g->back.height, floorf(fminf(a.y, fminf(b.y, c.y)))));
    int y1 = fmaxf(0, fminf(g->back.height, ceilf(fmaxf(a.y, fmaxf(b.y, c.y)))));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - a.x, dy = y + 0.5f - a.y;
            float wb = (cy * dx - cx * dy) * inverse, wc = (bx * dy - by * dx) * inverse;
            float wa = 1.0f - wb - wc;
            if (wa < 0 || wb < 0 || wc < 0)
                continue;
            float z = a.z * wa + b.z * wb + c.z * wc;
            unsigned pixel = y * g->back.width + x;
            if (z < 0 || z > 1.000001f || (g->raster[7] && z > g->depth_pixels[pixel] + 0.0000001f))
                continue;
            wa *= a.inv_w;
            wb *= b.inv_w;
            wc *= c.inv_w;
            float weight = 1.0f / (wa + wb + wc);
            uint32_t value = sample(g, t, image, (a.u * wa + b.u * wb + c.u * wc) * weight,
                                   (a.v * wa + b.v * wb + c.v * wc) * weight);
            if (g->fvf == 0x152)
                value = modulate(value, a, b, c, wa * weight, wb * weight, wc * weight);
            if (g->raster[15]) {
                unsigned alpha = value >> 24, reference = g->raster[24] & 255;
                unsigned equality = g->raster[25];
                if (equality == 1 || (equality == 2 && alpha >= reference) ||
                    (equality == 3 && alpha != reference) || (equality == 4 && alpha > reference) ||
                    (equality == 5 && alpha <= reference) || (equality == 6 && alpha == reference) ||
                    (equality == 7 && alpha < reference))
                    continue;
            }
            if (g->raster[27]) {
                unsigned alpha = value >> 24;
                value = mix_color(value, g->pixels[pixel], alpha, 255 - alpha);
            }
            g->pixels[pixel] = value | 0xff000000u;
            if (g->raster[14])
                g->depth_pixels[pixel] = z;
        }
    }
}

bool render_indexed(Game *g, const Buffer *vb, const Buffer *ib)
{
    unsigned type = arg(g, 1), minimum = arg(g, 3), count = arg(g, 4), first = arg(g, 5), n = arg(g, 6);
    int64_t base = (int32_t)arg(g, 2) + (int64_t)minimum;
    if (!g->pixels || !((g->fvf == 0x112 && g->stride == 32) || (g->fvf == 0x152 && g->stride == 36)) ||
        ib->format != 101 ||
        (type != 4 && type != 5) || !count || count > 65536 || !n || n > 65536 || base < 0 ||
        (g->raster[7] && g->raster[23] != 4) ||
        (g->raster[27] && (g->raster[19] != 5 || g->raster[20] != 6)))
        return false;
    uint64_t start = g->stream_offset + base * g->stride, end = start + (uint64_t)count * g->stride;
    unsigned index_count = type == 4 ? 3 * n : n + 2;
    if (end > vb->bytes || ((uint64_t)first + index_count) * 2 > ib->bytes)
        return false;
    Surface *texture = NULL;
    for (unsigned i = 0; i < MAX_TEXTURES; ++i)
        if (g->texture0 && g->textures[i].handle == g->texture0) {
            texture = &g->textures[i].mip[0];
            break;
        }
    if (!texture)
        return false;
    if (!texture->decoded)
        texture->decoded = decode_texture(g, texture);
    const uint32_t *image = texture->decoded;
    if (!image)
        return false;

    const uint8_t *indices = game_bytes(g, ib->data + first * 2, index_count * 2);
    const uint8_t *vertices = game_bytes(g, vb->data + (uint32_t)start, count * g->stride);
    uint8_t vertex_hash[32], index_hash[32];
    hash_bytes(vertices, count * g->stride, vertex_hash);
    hash_bytes(indices, index_count * 2, index_hash);
    fprintf(g->log, "Draw #%u type=%u vertices=%u triangles=%u vb=", g->draws + 1, type, count, n);
    for (unsigned i = 0; i < 32; ++i)
        fprintf(g->log, "%02x", (unsigned)vertex_hash[i]);
    fputs(" ib=", g->log);
    for (unsigned i = 0; i < 32; ++i)
        fprintf(g->log, "%02x", (unsigned)index_hash[i]);
    fputc('\n', g->log);
    for (unsigned i = 0; i < n; ++i) {
        unsigned offset = type == 4 ? i * 3 : i;
        unsigned j = type == 5 ? i & 1 : 0;
        unsigned id[] = {get16(indices + 2 * (offset + j)), get16(indices + 2 * (offset + (1 ^ j))),
                         get16(indices + 2 * (offset + 2))};
        ClipVertex v[3];
        bool visible = true;
        for (unsigned k = 0; k < 3; ++k) {
            if (id[k] < minimum || id[k] - minimum >= count)
                return false;
            if (!read_vertex(g, vertices + (id[k] - minimum) * g->stride, &v[k])) {
                visible = false;
                break;
            }
        }
        if (visible)
            clip_triangle(g, texture, image, v);
    }
    return true;
}

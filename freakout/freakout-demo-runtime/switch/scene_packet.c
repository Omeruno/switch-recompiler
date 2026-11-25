#include "scene_packet.h"
#include <string.h>

static uint32_t u32(const uint8_t *word)
{
    return (uint32_t)word[0] | ((uint32_t)word[1] << 8) |
           ((uint32_t)word[2] << 16) | ((uint32_t)word[3] << 24);
}

static float f32(const uint8_t *word)
{
    uint32_t bits = u32(word);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static const uint8_t *take(const uint8_t **cursor, const uint8_t *end, size_t size)
{
    if (!size || size > (size_t)(end - *cursor))
        return NULL;
    const uint8_t *start = *cursor;
    *cursor += size;
    return start;
}

static bool finite_vectors(const uint8_t *data, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += 4)
        if ((u32(data + i) & 0x7f800000u) == 0x7f800000u)
            return false;
    return true;
}

static bool parse_shader(FoSceneShader *shader, const uint8_t **cursor, const uint8_t *end)
{
    const uint8_t *record = take(cursor, end, 8);
    if (!record)
        return false;
    shader->stage = u32(record);
    shader->size = u32(record + 4);
    if (shader->stage > 1 || shader->size < 512 || shader->size > 65536)
        return false;
    const uint8_t *data = take(cursor, end, shader->size);
    if (!data || memcmp(data, "DKSH", 4) || u32(data + 4) != 24 || u32(data + 20) != 1)
        return false;
    uint32_t control = u32(data + 8), code = u32(data + 12), table = u32(data + 16);
    if (control < 256 || control >= shader->size || code != shader->size - control ||
        ((control | code) & 255) || table < 24 || table % 4 || table > control - 64)
        return false;
    const uint8_t *program = data + table;
    uint32_t const_offset = u32(program + 12), const_size = u32(program + 16);
    if (u32(program) != shader->stage || u32(program + 4) >= code ||
        const_offset > code || const_size > code - const_offset ||
        (shader->stage == 0 && u32(program + 24) >= code))
        return false;
    shader->data = data;
    return true;
}

static bool parse_clear(FoSceneClear *clear, uint32_t draws, const uint8_t **cursor, const uint8_t *end)
{
    const uint8_t *data = take(cursor, end, 16);
    if (!data)
        return false;
    *clear = (FoSceneClear){ u32(data), u32(data + 4), u32(data + 8), f32(data + 12) };
    return clear->at <= draws && clear->flags && !(clear->flags & ~3u) &&
           clear->depth >= 0.0f && clear->depth <= 1.0f;
}

static bool parse_draw(FoSceneDraw *draw, const FoScenePacket *packet,
                       const uint8_t **cursor, const uint8_t *end)
{
    const uint8_t *data = take(cursor, end, 108);
    if (!data)
        return false;
    *draw = (FoSceneDraw){
        .vs = u32(data), .ps = u32(data + 4), .primitive = u32(data + 8),
        .vertex_count = u32(data + 12), .index_count = u32(data + 16), .attributes = u32(data + 20),
        .flags = u32(data + 24), .depth_compare = u32(data + 28), .cull = u32(data + 32),
        .src_blend = u32(data + 36), .dst_blend = u32(data + 40), .alpha_compare = u32(data + 44),
        .alpha_ref = u32(data + 48), .x = u32(data + 52), .y = u32(data + 56),
        .w = u32(data + 60), .h = u32(data + 64), .near = f32(data + 68), .far = f32(data + 72),
    };
    if (draw->vs >= packet->shader_count || draw->ps >= packet->shader_count ||
        packet->shaders[draw->vs].stage != 0 || packet->shaders[draw->ps].stage != 1 ||
        draw->primitive < 4 || draw->primitive > 6 ||
        !draw->vertex_count || draw->vertex_count > 65536 ||
        draw->index_count < 3 || draw->index_count > 65536 ||
        (draw->primitive == 4 && draw->index_count % 3) ||
        !draw->attributes || draw->attributes >> FO_SCENE_ATTRIBUTES || draw->flags & ~15u ||
        draw->depth_compare < 1 || draw->depth_compare > 8 || draw->cull < 1 || draw->cull > 3 ||
        draw->src_blend < 1 || draw->src_blend > 11 || draw->dst_blend < 1 || draw->dst_blend > 11 ||
        draw->alpha_compare < 1 || draw->alpha_compare > 8 || draw->alpha_ref > 255 ||
        !draw->w || !draw->h || draw->x >= 1024 || draw->w > 1024 - draw->x ||
        draw->y >= 768 || draw->h > 768 - draw->y ||
        !(draw->near >= 0.0f && draw->near <= draw->far && draw->far <= 1.0f))
        return false;
    for (uint32_t i = 0; i < FO_SCENE_TEXTURE_SLOTS; i++) {
        uint32_t texture = u32(data + 76 + i * 8), settings = u32(data + 80 + i * 8);
        if ((texture == UINT32_MAX && settings != 0) ||
            (texture != UINT32_MAX && (texture >= packet->texture_count || settings > 255 ||
             (settings & 3) == 3 || (settings >> 2 & 3) == 3 || (settings >> 6) == 3)))
            return false;
        draw->samplers[i] = (FoSceneSampler){ texture, settings };
    }
    if (draw->flags & FO_SCENE_PROGRAM) {
        draw->constants = take(cursor, end, FO_SCENE_VERTEX_CONSTANT_BYTES + FO_SCENE_PIXEL_CONSTANT_BYTES);
        if (!draw->constants || !finite_vectors(draw->constants,
                FO_SCENE_VERTEX_CONSTANT_BYTES + FO_SCENE_PIXEL_CONSTANT_BYTES))
            return false;
    }
    draw->stride = 16 * (uint32_t)__builtin_popcount(draw->attributes);
    draw->vertices = take(cursor, end, draw->stride * draw->vertex_count);
    draw->indices = take(cursor, end, 2 * draw->index_count);
    if (!draw->vertices || !draw->indices ||
        !finite_vectors(draw->vertices, draw->stride * draw->vertex_count))
        return false;
    for (uint32_t i = 0; i < draw->index_count; i++) {
        const uint8_t *index = draw->indices + 2 * i;
        if ((index[0] | ((uint32_t)index[1] << 8)) >= draw->vertex_count)
            return false;
    }
    return true;
}

bool fo_scene_parse(FoScenePacket *packet, const uint8_t *data, size_t size)
{
    if (size < FO_SCENE_HEADER_SIZE || size > FO_SCENE_MAX_SIZE ||
        memcmp(data, "FOS3", 4) || u32(data + 4) != 1)
        return false;
    packet->texture_count = u32(data + 8);
    packet->shader_count = u32(data + 12);
    packet->draw_count = u32(data + 16);
    packet->clear_count = u32(data + 20);
    if (!packet->texture_count || packet->texture_count > FO_SCENE_MAX_TEXTURES ||
        packet->shader_count < 2 || packet->shader_count > FO_SCENE_MAX_SHADERS ||
        !packet->draw_count || packet->draw_count > FO_SCENE_MAX_DRAWS ||
        packet->clear_count > FO_SCENE_MAX_CLEARS)
        return false;
    const uint8_t *cursor = data + FO_SCENE_HEADER_SIZE, *end = data + size;
    for (uint32_t i = 0; i < packet->texture_count; i++)
        if (!fo_frame_parse_texture(&packet->textures[i], &cursor, end))
            return false;
    for (uint32_t i = 0; i < packet->shader_count; i++)
        if (!parse_shader(&packet->shaders[i], &cursor, end))
            return false;
    for (uint32_t i = 0; i < packet->clear_count; i++)
        if (!parse_clear(&packet->clears[i], packet->draw_count, &cursor, end) ||
            (i && packet->clears[i].at < packet->clears[i - 1].at))
            return false;
    for (uint32_t i = 0; i < packet->draw_count; i++)
        if (!parse_draw(&packet->draws[i], packet, &cursor, end))
            return false;
    return cursor == end;
}

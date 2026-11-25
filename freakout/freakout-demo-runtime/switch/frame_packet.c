#include "frame_packet.h"
#include <string.h>

enum { D3D_BGRA8 = 21, D3D_DXT1 = 0x31545844, D3D_DXT5 = 0x35545844 };

static uint32_t u32(const uint8_t *word)
{
    return (uint32_t)word[0] | ((uint32_t)word[1] << 8) |
           ((uint32_t)word[2] << 16) | ((uint32_t)word[3] << 24);
}

uint32_t fo_frame_mip_size(uint32_t format, uint32_t width, uint32_t height)
{
    if (!width || !height || width > 4096 || height > 4096)
        return 0;
    switch (format) {
        case D3D_BGRA8: return width * height * 4;
        case D3D_DXT1: return ((width + 3) / 4) * ((height + 3) / 4) * 8;
        case D3D_DXT5: return ((width + 3) / 4) * ((height + 3) / 4) * 16;
        default: return 0;
    }
}

static const uint8_t *take(const uint8_t **cursor, const uint8_t *end, size_t bytes)
{
    if (!bytes || bytes > (size_t)(end - *cursor))
        return NULL;
    const uint8_t *data = *cursor;
    *cursor += bytes;
    return data;
}

bool fo_frame_parse_texture(FoFrameTexture *texture, const uint8_t **cursor, const uint8_t *end)
{
    const uint8_t *header = take(cursor, end, 16);
    if (!header)
        return false;
    texture->width = u32(header);
    texture->height = u32(header + 4);
    texture->format = u32(header + 8);
    texture->level_count = u32(header + 12);
    if (!texture->level_count || texture->level_count > FO_FRAME_MAX_MIPS)
        return false;
    uint32_t width = texture->width, height = texture->height;
    for (uint32_t i = 0; i < texture->level_count; i++) {
        texture->levels[i] = take(cursor, end, fo_frame_mip_size(texture->format, width, height));
        if (!texture->levels[i])
            return false;
        if (width == 1 && height == 1 && i + 1 < texture->level_count)
            return false;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return true;
}

static bool parse_draw(FoFrameDraw *draw, uint32_t texture_count, uint32_t flags_mask,
                       const uint8_t **cursor, const uint8_t *end)
{
    const uint8_t *header = take(cursor, end, 20);
    if (!header)
        return false;
    draw->texture = u32(header);
    draw->primitive = u32(header + 4);
    draw->vertex_count = u32(header + 8);
    draw->index_count = u32(header + 12);
    draw->flags = u32(header + 16);
    if (draw->texture >= texture_count || !draw->vertex_count || draw->vertex_count > 65536 ||
        draw->index_count < 3 || draw->index_count > 65536 || (draw->flags & ~flags_mask))
        return false;
    if ((draw->primitive != 4 && draw->primitive != 5) ||
        (draw->primitive == 4 && draw->index_count % 3))
        return false;
    size_t stride = draw->flags & FO_FRAME_VERTEX_COLOR ? FO_FRAME_COLORED_VERTEX_SIZE : FO_FRAME_VERTEX_SIZE;
    size_t vertex_bytes = draw->vertex_count * stride;
    draw->vertices = take(cursor, end, vertex_bytes);
    draw->indices = take(cursor, end, draw->index_count * 2);
    if (!draw->vertices || !draw->indices)
        return false;
    for (size_t i = 0; i < vertex_bytes; i += 4)
        if ((u32(draw->vertices + i) & 0x7f800000u) == 0x7f800000u)
            return false;
    for (uint32_t i = 0; i < draw->index_count; i++) {
        const uint8_t *index = draw->indices + i * 2;
        if ((index[0] | ((uint32_t)index[1] << 8)) >= draw->vertex_count)
            return false;
    }
    return true;
}

bool fo_frame_parse(FoFramePacket *packet, const uint8_t *data, size_t size)
{
    if (size < FO_FRAME_HEADER_SIZE || size > FO_FRAME_MAX_SIZE ||
        memcmp(data, "FOFR", 4) || u32(data + 16) != size - FO_FRAME_HEADER_SIZE)
        return false;
    uint32_t version = u32(data + 4);
    if (version < 1 || version > 3)
        return false;
    packet->texture_count = u32(data + 8);
    packet->draw_count = u32(data + 12);
    if (!packet->texture_count || packet->texture_count > FO_FRAME_MAX_TEXTURES ||
        !packet->draw_count || packet->draw_count > FO_FRAME_MAX_DRAWS)
        return false;

    const uint8_t *cursor = data + FO_FRAME_HEADER_SIZE, *end = data + size;
    packet->clear_argb = 0xff000000u;
    packet->clear_depth = 1.0f;
    if (version >= 2) {
        const uint8_t *clear = take(&cursor, end, 8);
        if (!clear)
            return false;
        packet->clear_argb = u32(clear);
        uint32_t bits = u32(clear + 4);
        memcpy(&packet->clear_depth, &bits, sizeof bits);
        if (!(packet->clear_depth >= 0.0f && packet->clear_depth <= 1.0f))
            return false;
    }
    for (uint32_t i = 0; i < packet->texture_count; i++)
        if (!fo_frame_parse_texture(&packet->textures[i], &cursor, end))
            return false;
    for (uint32_t i = 0; i < packet->draw_count; i++)
        if (!parse_draw(&packet->draws[i], packet->texture_count, (1u << (version + 4)) - 1u, &cursor, end))
            return false;
    return cursor == end;
}

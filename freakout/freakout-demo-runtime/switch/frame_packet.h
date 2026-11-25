#ifndef FREAKOUT_FRAME_PACKET_H
#define FREAKOUT_FRAME_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FO_FRAME_HEADER_SIZE = 52,
    FO_FRAME_MAX_SIZE = 8 * 1024 * 1024,
    FO_FRAME_MAX_DRAWS = 32,
    FO_FRAME_MAX_TEXTURES = 16,
    FO_FRAME_MAX_MIPS = 13,
    FO_FRAME_VERTEX_SIZE = 24,
    FO_FRAME_COLORED_VERTEX_SIZE = 40,
    FO_FRAME_BLEND = 1,
    FO_FRAME_DEPTH_WRITE = 2,
    FO_FRAME_CLAMP_U = 4,
    FO_FRAME_CLAMP_V = 8,
    FO_FRAME_LINEAR_MIP = 16,
    FO_FRAME_DEPTH_RESET = 32,
    FO_FRAME_VERTEX_COLOR = 64,
};

typedef struct {
    uint32_t width, height, format, level_count;
    const uint8_t *levels[FO_FRAME_MAX_MIPS];
} FoFrameTexture;

typedef struct {
    uint32_t texture, primitive, vertex_count, index_count, flags;
    const uint8_t *vertices, *indices;
} FoFrameDraw;

typedef struct {
    uint32_t texture_count, draw_count;
    uint32_t clear_argb;
    float clear_depth;
    FoFrameTexture textures[FO_FRAME_MAX_TEXTURES];
    FoFrameDraw draws[FO_FRAME_MAX_DRAWS];
} FoFramePacket;

uint32_t fo_frame_mip_size(uint32_t format, uint32_t width, uint32_t height);
bool fo_frame_parse_texture(FoFrameTexture *texture, const uint8_t **cursor, const uint8_t *end);
bool fo_frame_parse(FoFramePacket *packet, const uint8_t *data, size_t size);

#endif

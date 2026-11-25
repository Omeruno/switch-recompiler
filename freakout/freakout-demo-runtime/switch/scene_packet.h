#ifndef FREAKOUT_SCENE_PACKET_H
#define FREAKOUT_SCENE_PACKET_H

#include "frame_packet.h"

enum {
    FO_SCENE_HEADER_SIZE = 56,
    FO_SCENE_MAX_SIZE = 96 * 1024 * 1024,
    FO_SCENE_MAX_TEXTURES = 64,
    FO_SCENE_MAX_SHADERS = 32,
    FO_SCENE_MAX_DRAWS = 256,
    FO_SCENE_MAX_CLEARS = 32,
    FO_SCENE_TEXTURE_SLOTS = 4,
    FO_SCENE_ATTRIBUTES = 16,
    FO_SCENE_VERTEX_CONSTANT_BYTES = 256 * 16,
    FO_SCENE_PIXEL_CONSTANT_BYTES = 32 * 16,
    FO_SCENE_PROGRAM = 1,
    FO_SCENE_DEPTH_TEST = 2,
    FO_SCENE_DEPTH_WRITE = 4,
    FO_SCENE_BLEND = 8,
};

typedef struct {
    uint32_t stage, size;
    const uint8_t *data;
} FoSceneShader;

typedef struct {
    uint32_t at, flags, argb;
    float depth;
} FoSceneClear;

typedef struct {
    uint32_t texture, settings;
} FoSceneSampler;

typedef struct {
    uint32_t vs, ps, primitive, vertex_count, index_count, attributes, flags;
    uint32_t depth_compare, cull, src_blend, dst_blend, alpha_compare, alpha_ref;
    uint32_t x, y, w, h;
    float near, far;
    FoSceneSampler samplers[FO_SCENE_TEXTURE_SLOTS];
    uint32_t stride;
    const uint8_t *constants, *vertices, *indices;
} FoSceneDraw;

typedef struct {
    uint32_t texture_count, shader_count, draw_count, clear_count;
    FoFrameTexture textures[FO_SCENE_MAX_TEXTURES];
    FoSceneShader shaders[FO_SCENE_MAX_SHADERS];
    FoSceneClear clears[FO_SCENE_MAX_CLEARS];
    FoSceneDraw draws[FO_SCENE_MAX_DRAWS];
} FoScenePacket;

bool fo_scene_parse(FoScenePacket *packet, const uint8_t *data, size_t size);

#endif

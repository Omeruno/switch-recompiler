#ifndef FREAKOUT_FRAME_REPLAY_H
#define FREAKOUT_FRAME_REPLAY_H

#include "frame_packet.h"
#include <deko3d.h>

typedef struct {
    uint32_t texture, primitive, flags, vertex_offset, vertex_bytes, index_offset, index_count;
} ReplayDraw;

typedef struct {
    DkMemBlock storage;
    DkImage image;
} ReplayTexture;

typedef struct {
    DkMemBlock code, descriptors, geometry;
    DkShader vertex_shader, fragment_shader;
    uint32_t texture_count, draw_count;
    uint32_t clear_argb;
    float clear_depth;
    ReplayTexture textures[FO_FRAME_MAX_TEXTURES];
    ReplayDraw draws[FO_FRAME_MAX_DRAWS];
} FrameReplay;

bool replay_upload_texture(ReplayTexture *texture, DkMemBlock descriptors, uint32_t index,
                           const FoFrameTexture *source, DkDevice device, DkQueue queue,
                           DkCmdBuf commands, DkMemBlock command_memory, uint32_t command_bytes);
const char *frame_replay_init(FrameReplay *replay, const char *path, DkDevice device, DkQueue queue,
                              DkCmdBuf commands, DkMemBlock command_memory, uint32_t command_bytes);
void frame_replay_draw(const FrameReplay *replay, DkCmdBuf commands);
void frame_replay_destroy(FrameReplay *replay);

#endif

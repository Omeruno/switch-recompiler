#ifndef FREAKOUT_SCENE_REPLAY_H
#define FREAKOUT_SCENE_REPLAY_H

#include "frame_replay.h"
#include "scene_packet.h"

typedef struct {
    FoSceneDraw state;
    uint32_t vertex_offset, vertex_bytes, index_offset, constant_offset;
    DkResHandle textures[FO_SCENE_TEXTURE_SLOTS];
} SceneReplayDraw;

typedef struct {
    DkMemBlock code, geometry, descriptors;
    uint32_t draw_count, texture_count, clear_count;
    DkShader modules[FO_SCENE_MAX_SHADERS];
    FoSceneClear clears[FO_SCENE_MAX_CLEARS];
    ReplayTexture textures[FO_SCENE_MAX_TEXTURES];
    SceneReplayDraw draws[FO_SCENE_MAX_DRAWS];
} SceneReplay;

const char *scene_replay_init(SceneReplay *replay, const char *path, DkDevice device, DkQueue queue,
                              DkCmdBuf commands, DkMemBlock command_memory, uint32_t command_bytes);
void scene_replay_draw(const SceneReplay *replay, DkCmdBuf commands);
void scene_replay_destroy(SceneReplay *replay);

#endif

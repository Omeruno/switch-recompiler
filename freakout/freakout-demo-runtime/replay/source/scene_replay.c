#include "scene_replay.h"
#include "replay_gpu.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t upload_flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
enum { SAMPLERS_OFFSET = FO_SCENE_MAX_TEXTURES * sizeof(DkImageDescriptor) };

static uint32_t align_256(uint32_t value)
{
    return (value + 255u) & ~255u;
}

static bool upload_shaders(SceneReplay *replay, const FoScenePacket *packet, DkDevice device)
{
    uint32_t total = DK_SHADER_CODE_UNUSABLE_SIZE;
    for (uint32_t i = 0; i < packet->shader_count; i++)
        total += packet->shaders[i].size;
    replay->code = replay_memory(device, total, upload_flags | DkMemBlockFlags_Code);
    if (!replay->code)
        return false;
    uint8_t *destination = dkMemBlockGetCpuAddr(replay->code);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < packet->shader_count; i++) {
        const FoSceneShader *source = &packet->shaders[i];
        memcpy(destination + offset, source->data, source->size);
        DkShaderMaker maker;
        dkShaderMakerDefaults(&maker, replay->code, offset);
        dkShaderInitialize(&replay->modules[i], &maker);
        if (!dkShaderIsValid(&replay->modules[i]) ||
            dkShaderGetStage(&replay->modules[i]) != (source->stage ? DkStage_Fragment : DkStage_Vertex))
            return false;
        offset += source->size;
    }
    return true;
}

static void upload_samplers(SceneReplayDraw *draw, uint32_t id, uint8_t *descriptors)
{
    for (uint32_t i = 0; i < FO_SCENE_TEXTURE_SLOTS; i++) {
        const FoSceneSampler *reference = &draw->state.samplers[i];
        uint32_t bits = reference->settings;
        uint32_t index = id * FO_SCENE_TEXTURE_SLOTS + i;
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.wrapMode[0] = bits & 3;
        sampler.wrapMode[1] = bits >> 2 & 3;
        sampler.magFilter = 1 + (bits >> 4 & 1);
        sampler.minFilter = 1 + (bits >> 5 & 1);
        sampler.mipFilter = 1 + (bits >> 6);
        DkSamplerDescriptor descriptor;
        dkSamplerDescriptorInitialize(&descriptor, &sampler);
        memcpy(descriptors + index * sizeof descriptor, &descriptor, sizeof descriptor);
        draw->textures[i] = dkMakeTextureHandle(reference->texture == UINT32_MAX ? 0 : reference->texture, index);
    }
}

static bool upload_draws(SceneReplay *replay, const FoScenePacket *packet, DkDevice device)
{
    const uint32_t constant_bytes = FO_SCENE_VERTEX_CONSTANT_BYTES + FO_SCENE_PIXEL_CONSTANT_BYTES;
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < packet->draw_count; i++) {
        const FoSceneDraw *source = &packet->draws[i];
        SceneReplayDraw *draw = &replay->draws[i];
        draw->state = *source;
        draw->state.constants = draw->state.vertices = draw->state.indices = NULL;
        draw->vertex_offset = bytes;
        draw->vertex_bytes = source->stride * source->vertex_count;
        draw->index_offset = bytes + align_256(draw->vertex_bytes);
        bytes = draw->index_offset + align_256(source->index_count * 2);
        draw->constant_offset = bytes;
        if (source->constants)
            bytes += constant_bytes;
    }
    replay->geometry = replay_memory(device, bytes, upload_flags);
    if (!replay->geometry)
        return false;
    uint8_t *geometry = dkMemBlockGetCpuAddr(replay->geometry);
    uint8_t *descriptors = dkMemBlockGetCpuAddr(replay->descriptors);
    for (uint32_t i = 0; i < packet->draw_count; i++) {
        SceneReplayDraw *draw = &replay->draws[i];
        const FoSceneDraw *source = &packet->draws[i];
        memcpy(geometry + draw->vertex_offset, source->vertices, draw->vertex_bytes);
        memcpy(geometry + draw->index_offset, source->indices, source->index_count * 2);
        if (source->constants)
            memcpy(geometry + draw->constant_offset, source->constants, constant_bytes);
        upload_samplers(draw, i, descriptors + SAMPLERS_OFFSET);
    }
    replay->draw_count = packet->draw_count;
    return true;
}

static const char *prepare(SceneReplay *replay, const FoScenePacket *packet, DkDevice device,
                           DkQueue queue, DkCmdBuf commands, DkMemBlock memory, uint32_t bytes)
{
    replay->descriptors = replay_memory(device, SAMPLERS_OFFSET +
        packet->draw_count * FO_SCENE_TEXTURE_SLOTS * sizeof(DkSamplerDescriptor), upload_flags);
    if (!replay->descriptors || !upload_shaders(replay, packet, device))
        return "Could not prepare the scene shaders and descriptors.";
    replay->texture_count = packet->texture_count;
    for (uint32_t i = 0; i < packet->texture_count; i++)
        if (!replay_upload_texture(&replay->textures[i], replay->descriptors, i, &packet->textures[i],
                                   device, queue, commands, memory, bytes))
            return "Could not upload a 3D scene texture.";
    if (!upload_draws(replay, packet, device))
        return "Could not upload the scene geometry and shader registers.";
    replay->clear_count = packet->clear_count;
    memcpy(replay->clears, packet->clears, packet->clear_count * sizeof(FoSceneClear));
    return NULL;
}

const char *scene_replay_init(SceneReplay *replay, const char *path, DkDevice device, DkQueue queue,
                              DkCmdBuf commands, DkMemBlock memory, uint32_t bytes)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return "Put the exported main-menu.fos3 beside the NRO.";
    const char *error = "Could not read the captured 3D scene.";
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        if (length >= FO_SCENE_HEADER_SIZE && length <= FO_SCENE_MAX_SIZE && fseek(file, 0, SEEK_SET) == 0) {
            uint8_t *data = malloc((size_t)length);
            FoScenePacket *packet = malloc(sizeof *packet);
            if (data && packet && fread(data, 1, (size_t)length, file) == (size_t)length) {
                uint8_t hash[32];
                sha256CalculateHash(hash, data + FO_SCENE_HEADER_SIZE, (size_t)length - FO_SCENE_HEADER_SIZE);
                error = "Scene packet has an invalid checksum or layout.";
                if (memcmp(hash, data + 24, sizeof hash) == 0 && fo_scene_parse(packet, data, (size_t)length))
                    error = prepare(replay, packet, device, queue, commands, memory, bytes);
            }
            free(packet);
            free(data);
        }
    }
    fclose(file);
    return error;
}

static void emit_clear(DkCmdBuf commands, const FoSceneClear *clear)
{
    if (clear->flags & 1) {
        float color[4] = { ((clear->argb >> 16) & 255) / 255.0f, ((clear->argb >> 8) & 255) / 255.0f,
                           (clear->argb & 255) / 255.0f, (clear->argb >> 24) / 255.0f };
        dkCmdBufClearColor(commands, 0, DkColorMask_RGBA, color);
    }
    if (clear->flags & 2)
        dkCmdBufClearDepthStencil(commands, true, clear->depth, 0, 0);
}

static void draw_state(DkCmdBuf commands, const FoSceneDraw *state)
{
    DkViewport viewport = { state->x, 768 - state->y - state->h, state->w, state->h, state->near, state->far };
    dkCmdBufSetViewports(commands, 0, &viewport, 1);
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = state->cull == 1 ? DkFace_None : DkFace_Back;
    rasterizer.frontFace = state->cull == 3 ? DkFrontFace_CW : DkFrontFace_CCW;
    dkCmdBufBindRasterizerState(commands, &rasterizer);

    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = (state->flags & FO_SCENE_DEPTH_TEST) != 0;
    depth.depthWriteEnable = (state->flags & FO_SCENE_DEPTH_WRITE) != 0;
    depth.depthCompareOp = (DkCompareOp)state->depth_compare;
    dkCmdBufBindDepthStencilState(commands, &depth);

    DkColorState color;
    dkColorStateDefaults(&color);
    dkColorStateSetBlendEnable(&color, 0, (state->flags & FO_SCENE_BLEND) != 0);
    color.alphaCompareOp = (DkCompareOp)state->alpha_compare;
    dkCmdBufBindColorState(commands, &color);
    dkCmdBufSetAlphaRef(commands, state->alpha_ref / 255.0f);
    if (state->flags & FO_SCENE_BLEND) {
        DkBlendState blend;
        dkBlendStateDefaults(&blend);
        dkBlendStateSetFactors(&blend, state->src_blend, state->dst_blend, state->src_blend, state->dst_blend);
        dkCmdBufBindBlendState(commands, 0, &blend);
    }
}

static void bind_geometry(DkCmdBuf commands, const SceneReplayDraw *draw, DkGpuAddr geometry)
{
    DkVtxAttribState attrs[FO_SCENE_ATTRIBUTES] = {0};
    uint32_t offset = 0;
    for (uint32_t i = 0; i < FO_SCENE_ATTRIBUTES; i++) {
        if (draw->state.attributes & (1u << i)) {
            attrs[i] = (DkVtxAttribState){.offset = offset, .size = DkVtxAttribSize_4x32, .type = DkVtxAttribType_Float};
            offset += 16;
        } else {
            attrs[i] = (DkVtxAttribState){.isFixed = true, .size = DkVtxAttribSize_1x32, .type = DkVtxAttribType_Float};
        }
    }
    const DkVtxBufferState buffer = { .stride = draw->state.stride };
    dkCmdBufBindVtxAttribState(commands, attrs, FO_SCENE_ATTRIBUTES);
    dkCmdBufBindVtxBufferState(commands, &buffer, 1);
    dkCmdBufBindVtxBuffer(commands, 0, geometry + draw->vertex_offset, draw->vertex_bytes);
    dkCmdBufBindIdxBuffer(commands, DkIdxFormat_Uint16, geometry + draw->index_offset);
}

void scene_replay_draw(const SceneReplay *replay, DkCmdBuf commands)
{
    DkGpuAddr descriptors = dkMemBlockGetGpuAddr(replay->descriptors);
    DkGpuAddr geometry = dkMemBlockGetGpuAddr(replay->geometry);
    dkCmdBufBindImageDescriptorSet(commands, descriptors, replay->texture_count);
    dkCmdBufBindSamplerDescriptorSet(commands, descriptors + SAMPLERS_OFFSET,
                                    replay->draw_count * FO_SCENE_TEXTURE_SLOTS);
    DkColorWriteState mask;
    dkColorWriteStateDefaults(&mask);
    dkCmdBufBindColorWriteState(commands, &mask);
    uint32_t clear = 0, vs = UINT32_MAX, ps = UINT32_MAX;
    for (uint32_t i = 0; i < replay->draw_count; i++) {
        while (clear < replay->clear_count && replay->clears[clear].at == i)
            emit_clear(commands, &replay->clears[clear++]);
        const SceneReplayDraw *draw = &replay->draws[i];
        const FoSceneDraw *state = &draw->state;
        if (vs != state->vs || ps != state->ps) {
            vs = state->vs;
            ps = state->ps;
            const DkShader *shaders[] = { &replay->modules[vs], &replay->modules[ps] };
            dkCmdBufBindShaders(commands, DkStageFlag_GraphicsMask, shaders, 2);
        }
        if (state->flags & FO_SCENE_PROGRAM) {
            DkGpuAddr constants = geometry + draw->constant_offset;
            dkCmdBufBindUniformBuffer(commands, DkStage_Vertex, 0, constants, FO_SCENE_VERTEX_CONSTANT_BYTES);
            dkCmdBufBindUniformBuffer(commands, DkStage_Fragment, 0, constants + FO_SCENE_VERTEX_CONSTANT_BYTES,
                                      FO_SCENE_PIXEL_CONSTANT_BYTES);
        }
        draw_state(commands, state);
        dkCmdBufBindTextures(commands, DkStage_Fragment, 0, draw->textures, FO_SCENE_TEXTURE_SLOTS);
        bind_geometry(commands, draw, geometry);
        dkCmdBufDrawIndexed(commands, (DkPrimitive)state->primitive, state->index_count, 1, 0, 0, 0);
    }
    while (clear < replay->clear_count)
        emit_clear(commands, &replay->clears[clear++]);
}

void scene_replay_destroy(SceneReplay *replay)
{
    for (uint32_t i = 0; i < replay->texture_count; i++)
        if (replay->textures[i].storage)
            dkMemBlockDestroy(replay->textures[i].storage);
    if (replay->geometry)
        dkMemBlockDestroy(replay->geometry);
    if (replay->descriptors)
        dkMemBlockDestroy(replay->descriptors);
    if (replay->code)
        dkMemBlockDestroy(replay->code);
}

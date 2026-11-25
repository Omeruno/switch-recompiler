#include "frame_replay.h"
#include "replay_gpu.h"
#include "freakout_texture.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SAMPLER_OFFSET = 0x400 };
static const uint32_t upload_flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
_Static_assert(FO_FRAME_MAX_TEXTURES * sizeof(DkImageDescriptor) <= SAMPLER_OFFSET,
               "image and sampler descriptor tables overlap");

static uint32_t align_256(uint32_t value)
{
    return (value + 255) & ~255u;
}

static uint8_t *read_capture(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    uint8_t *data = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        if (length >= FO_FRAME_HEADER_SIZE && length <= FO_FRAME_MAX_SIZE && fseek(file, 0, SEEK_SET) == 0) {
            data = malloc((size_t)length);
            if (data && fread(data, 1, (size_t)length, file) == (size_t)length) {
                *size = (size_t)length;
            } else {
                free(data);
                data = NULL;
            }
        }
    }
    fclose(file);
    return data;
}

bool replay_upload_texture(ReplayTexture *texture, DkMemBlock descriptors, uint32_t index,
                           const FoFrameTexture *source,
                           DkDevice device, DkQueue queue, DkCmdBuf commands,
                           DkMemBlock command_memory, uint32_t command_bytes)
{
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, device);
    maker.dimensions[0] = source->width;
    maker.dimensions[1] = source->height;
    maker.mipLevels = source->level_count;
    if (source->format == 21) {
        maker.format = DkImageFormat_BGRA8_Unorm;
    } else {
        FreakOutTextureLayout footprint;
        if (!freakout_dxt_layout(source->format, source->width, source->height, 0, &footprint))
            return false;
        maker.format = footprint.format;
    }
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &maker);
    texture->storage = replay_memory(device, dkImageLayoutGetSize(&layout),
                                     DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!texture->storage)
        return false;
    dkImageInitialize(&texture->image, &layout, texture->storage, 0);

    uint32_t sizes[FO_FRAME_MAX_MIPS], widths[FO_FRAME_MAX_MIPS], heights[FO_FRAME_MAX_MIPS];
    uint32_t width = source->width, height = source->height;
    uint32_t bytes = 0;
    for (uint32_t level = 0; level < source->level_count; level++) {
        widths[level] = width;
        heights[level] = height;
        sizes[level] = fo_frame_mip_size(source->format, width, height);
        bytes += align_256(sizes[level]);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    DkMemBlock staging = replay_memory(device, bytes, upload_flags);
    if (!staging)
        return false;
    uint8_t *cpu = dkMemBlockGetCpuAddr(staging);
    DkGpuAddr gpu = dkMemBlockGetGpuAddr(staging);
    DkImageView view;
    dkImageViewDefaults(&view, &texture->image);
    uint32_t offset = 0;
    for (uint32_t level = 0; level < source->level_count; level++) {
        memcpy(cpu + offset, source->levels[level], sizes[level]);
        view.mipLevelOffset = level;
        view.mipLevelCount = 1;
        const DkCopyBuf buffer = { .addr = gpu + offset };
        const DkImageRect rect = { 0, 0, 0, widths[level], heights[level], 1 };
        dkCmdBufCopyBufferToImage(commands, &buffer, &view, &rect, 0);
        offset += align_256(sizes[level]);
    }
    dkCmdBufBarrier(commands, DkBarrier_Full, DkInvalidateFlags_Image);
    DkCmdList list = dkCmdBufFinishList(commands);
    if (list) {
        dkQueueSubmitCommands(queue, list);
        dkQueueWaitIdle(queue);
    }
    dkCmdBufClear(commands);
    dkCmdBufAddMemory(commands, command_memory, 0, command_bytes);
    dkMemBlockDestroy(staging);
    if (!list)
        return false;

    DkImageDescriptor descriptor;
    dkImageViewDefaults(&view, &texture->image);
    dkImageDescriptorInitialize(&descriptor, &view, false, false);
    uint8_t *table = dkMemBlockGetCpuAddr(descriptors);
    memcpy(table + index * sizeof descriptor, &descriptor, sizeof descriptor);
    return true;
}

static bool upload_geometry(FrameReplay *replay, const FoFramePacket *packet, DkDevice device)
{
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < packet->draw_count; i++) {
        const FoFrameDraw *source = &packet->draws[i];
        uint32_t vertex_bytes = source->vertex_count * FO_FRAME_COLORED_VERTEX_SIZE;
        replay->draws[i] = (ReplayDraw){
            .texture = source->texture, .flags = source->flags, .primitive = source->primitive,
            .vertex_offset = bytes, .vertex_bytes = vertex_bytes,
            .index_offset = bytes + align_256(vertex_bytes), .index_count = source->index_count,
        };
        bytes = replay->draws[i].index_offset + align_256(source->index_count * 2);
    }
    replay->geometry = replay_memory(device, bytes, upload_flags);
    if (!replay->geometry)
        return false;
    uint8_t *geometry = dkMemBlockGetCpuAddr(replay->geometry);
    uint8_t *table = dkMemBlockGetCpuAddr(replay->descriptors);
    for (uint32_t i = 0; i < packet->draw_count; i++) {
        const ReplayDraw *draw = &replay->draws[i];
        const FoFrameDraw *source = &packet->draws[i];
        uint8_t *vertices = geometry + draw->vertex_offset;
        if (source->flags & FO_FRAME_VERTEX_COLOR) {
            memcpy(vertices, source->vertices, draw->vertex_bytes);
        } else {
            const float white[4] = { 1, 1, 1, 1 };
            for (uint32_t j = 0; j < source->vertex_count; j++) {
                memcpy(vertices, source->vertices + j * FO_FRAME_VERTEX_SIZE, FO_FRAME_VERTEX_SIZE);
                memcpy(vertices + FO_FRAME_VERTEX_SIZE, white, sizeof white);
                vertices += FO_FRAME_COLORED_VERTEX_SIZE;
            }
        }
        memcpy(geometry + draw->index_offset, packet->draws[i].indices, draw->index_count * 2);
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = sampler.magFilter = DkFilter_Linear;
        if (draw->flags & FO_FRAME_LINEAR_MIP)
            sampler.mipFilter = DkMipFilter_Linear;
        if (draw->flags & FO_FRAME_CLAMP_U)
            sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
        if (draw->flags & FO_FRAME_CLAMP_V)
            sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
        DkSamplerDescriptor descriptor;
        dkSamplerDescriptorInitialize(&descriptor, &sampler);
        memcpy(table + SAMPLER_OFFSET + i * sizeof descriptor, &descriptor, sizeof descriptor);
    }
    return true;
}

static const char *prepare(FrameReplay *replay, const FoFramePacket *packet, DkDevice device,
                           DkQueue queue, DkCmdBuf commands, DkMemBlock memory, uint32_t bytes)
{
    replay->code = replay_memory(device, REPLAY_SHADER_STRIDE * 2, upload_flags | DkMemBlockFlags_Code);
    replay->descriptors = replay_memory(device, DK_MEMBLOCK_ALIGNMENT, upload_flags);
    if (!replay->code || !replay->descriptors)
        return "Could not allocate shaders or descriptors for the captured frame.";
    if (!replay_load_shader(replay->code, &replay->vertex_shader, 0, "romfs:/frame_vsh.dksh") ||
        !replay_load_shader(replay->code, &replay->fragment_shader, REPLAY_SHADER_STRIDE,
                            "romfs:/frame_fsh.dksh"))
        return "Could not load capture shaders.";
    replay->texture_count = packet->texture_count;
    replay->draw_count = packet->draw_count;
    replay->clear_argb = packet->clear_argb;
    replay->clear_depth = packet->clear_depth;
    for (uint32_t i = 0; i < packet->texture_count; i++)
        if (!replay_upload_texture(&replay->textures[i], replay->descriptors, i, &packet->textures[i],
                                   device, queue, commands, memory, bytes))
            return "Could not upload a captured texture.";
    if (!upload_geometry(replay, packet, device))
        return "Could not upload captured geometry.";
    return NULL;
}

const char *frame_replay_init(FrameReplay *replay, const char *path, DkDevice device, DkQueue queue,
                              DkCmdBuf commands, DkMemBlock memory, uint32_t bytes)
{
    size_t size;
    uint8_t *data = read_capture(path, &size);
    if (!data)
        return "Put the exported FOFR file beside the NRO. See the README.";
    uint8_t hash[32];
    sha256CalculateHash(hash, data + FO_FRAME_HEADER_SIZE, size - FO_FRAME_HEADER_SIZE);
    FoFramePacket packet;
    const char *error = "Capture file has an invalid checksum or layout.";
    if (memcmp(hash, data + 20, sizeof hash) == 0 && fo_frame_parse(&packet, data, size))
        error = prepare(replay, &packet, device, queue, commands, memory, bytes);
    free(data);
    return error;
}

void frame_replay_draw(const FrameReplay *replay, DkCmdBuf commands)
{
    DkGpuAddr descriptors = dkMemBlockGetGpuAddr(replay->descriptors);
    DkGpuAddr geometry = dkMemBlockGetGpuAddr(replay->geometry);
    dkCmdBufBindImageDescriptorSet(commands, descriptors, replay->texture_count);
    dkCmdBufBindSamplerDescriptorSet(commands, descriptors + SAMPLER_OFFSET, replay->draw_count);
    const DkShader *shaders[] = { &replay->vertex_shader, &replay->fragment_shader };
    dkCmdBufBindShaders(commands, DkStageFlag_GraphicsMask, shaders, 2);
    const DkVtxAttribState attributes[] = {
        { .bufferId = 0, .offset = 0, .size = DkVtxAttribSize_4x32, .type = DkVtxAttribType_Float },
        { .bufferId = 0, .offset = 16, .size = DkVtxAttribSize_2x32, .type = DkVtxAttribType_Float },
        { .bufferId = 0, .offset = 24, .size = DkVtxAttribSize_4x32, .type = DkVtxAttribType_Float },
    };
    const DkVtxBufferState vertex = { .stride = FO_FRAME_COLORED_VERTEX_SIZE };
    dkCmdBufBindVtxAttribState(commands, attributes, 3);
    dkCmdBufBindVtxBufferState(commands, &vertex, 1);

    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    dkCmdBufBindRasterizerState(commands, &rasterizer);
    DkBlendState blend;
    dkBlendStateDefaults(&blend);
    dkBlendStateSetFactors(&blend, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha,
                           DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha);
    dkCmdBufBindBlendState(commands, 0, &blend);
    DkColorWriteState mask;
    dkColorWriteStateDefaults(&mask);
    dkCmdBufBindColorWriteState(commands, &mask);
    DkColorState color;
    dkColorStateDefaults(&color);
    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = true;
    depth.depthCompareOp = DkCompareOp_Lequal;
    for (uint32_t i = 0; i < replay->draw_count; i++) {
        const ReplayDraw *draw = &replay->draws[i];
        if (draw->flags & FO_FRAME_DEPTH_RESET)
            dkCmdBufClearDepthStencil(commands, true, 1.0f, 0, 0);
        dkColorStateSetBlendEnable(&color, 0, (draw->flags & FO_FRAME_BLEND) != 0);
        dkCmdBufBindColorState(commands, &color);
        depth.depthWriteEnable = (draw->flags & FO_FRAME_DEPTH_WRITE) != 0;
        dkCmdBufBindDepthStencilState(commands, &depth);
        dkCmdBufBindTexture(commands, DkStage_Fragment, 0, dkMakeTextureHandle(draw->texture, i));
        dkCmdBufBindVtxBuffer(commands, 0, geometry + draw->vertex_offset, draw->vertex_bytes);
        dkCmdBufBindIdxBuffer(commands, DkIdxFormat_Uint16, geometry + draw->index_offset);
        dkCmdBufDrawIndexed(commands, (DkPrimitive)draw->primitive, draw->index_count, 1, 0, 0, 0);
    }
}

void frame_replay_destroy(FrameReplay *replay)
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

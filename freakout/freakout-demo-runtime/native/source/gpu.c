#include "gpu.h"
#ifdef __SWITCH__
#include <deko3d.h>
#include <stdlib.h>
#include <string.h>

enum {
    COMMAND_BYTES = 1024 * 1024, UPLOAD_BYTES = 16 * 1024 * 1024,
    MAX_GPU_TEXTURES = MAX_TEXTURES, SAMPLER_COUNT = 18,
};
static const uint32_t upload_flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;

typedef struct { int source; const uint8_t *bytes; size_t size; } GpuCode;
#include "gpu_shaders.inc"
#define COUNT(a) (sizeof(a) / sizeof *(a))
typedef struct {
    DkMemBlock memory;
    DkImage image;
    const Surface *surface;
    uint32_t generation;
} GpuTexture;

struct NativeGpu {
    DkDevice device;
    DkQueue queue;
    DkCmdBuf commands;
    DkMemBlock commands_memory, upload, readback, code, descriptors, color_memory, depth_memory;
    DkImage color, depth;
    DkShader programs[COUNT(gpu_programs)];
    GpuTexture textures[MAX_GPU_TEXTURES];
    unsigned width, height, cursor, submitted_draws, frames;
};

static unsigned align_up(unsigned size, unsigned alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

static DkMemBlock memory(struct NativeGpu *gpu, unsigned size, unsigned flags)
{
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, gpu->device, align_up(size, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    return dkMemBlockCreate(&maker);
}

static DkMemBlock image(struct NativeGpu *gpu, DkImage *target, unsigned width, unsigned height,
                       DkImageFormat format, unsigned usage)
{
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, gpu->device);
    maker.dimensions[0] = width;
    maker.dimensions[1] = height;
    maker.format = format;
    maker.flags = usage;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &maker);
    DkMemBlock storage = memory(gpu, dkImageLayoutGetSize(&layout),
                               DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (storage)
        dkImageInitialize(target, &layout, storage, 0);
    return storage;
}

static unsigned sampler_offset(void)
{
    return align_up(MAX_GPU_TEXTURES * sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT);
}

static void begin(struct NativeGpu *gpu)
{
    dkCmdBufClear(gpu->commands);
    dkCmdBufAddMemory(gpu->commands, gpu->commands_memory, 0, COMMAND_BYTES);
    DkImageView color, depth;
    dkImageViewDefaults(&color, &gpu->color);
    dkImageViewDefaults(&depth, &gpu->depth);
    dkCmdBufBindRenderTarget(gpu->commands, &color, &depth);
    DkScissor scissor = {0, 0, gpu->width, gpu->height};
    dkCmdBufSetScissors(gpu->commands, 0, &scissor, 1);
    DkGpuAddr descriptors = dkMemBlockGetGpuAddr(gpu->descriptors);
    dkCmdBufBindImageDescriptorSet(gpu->commands, descriptors, MAX_GPU_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(gpu->commands, descriptors + sampler_offset(), SAMPLER_COUNT);
}

static bool submit(struct NativeGpu *gpu)
{
    DkCmdList list = dkCmdBufFinishList(gpu->commands);
    if (!list)
        return false;
    dkQueueSubmitCommands(gpu->queue, list);
    dkQueueWaitIdle(gpu->queue);
    if (dkQueueIsInErrorState(gpu->queue))
        return false;
    gpu->cursor = 0;
    begin(gpu);
    return true;
}

static bool init_programs(struct NativeGpu *gpu)
{
    unsigned bytes = DK_SHADER_CODE_UNUSABLE_SIZE;
    for (unsigned i = 0; i < COUNT(gpu_programs); ++i)
        bytes += align_up(gpu_programs[i].size, DK_SHADER_CODE_ALIGNMENT);
    gpu->code = memory(gpu, bytes, upload_flags | DkMemBlockFlags_Code);
    if (!gpu->code)
        return false;
    uint8_t *destination = dkMemBlockGetCpuAddr(gpu->code);
    unsigned offset = 0;
    for (unsigned i = 0; i < COUNT(gpu_programs); ++i) {
        memcpy(destination + offset, gpu_programs[i].bytes, gpu_programs[i].size);
        DkShaderMaker maker;
        dkShaderMakerDefaults(&maker, gpu->code, offset);
        dkShaderInitialize(&gpu->programs[i], &maker);
        if (!dkShaderIsValid(&gpu->programs[i]) ||
            dkShaderGetStage(&gpu->programs[i]) != (i ? DkStage_Fragment : DkStage_Vertex))
            return false;
        offset += align_up(gpu_programs[i].size, DK_SHADER_CODE_ALIGNMENT);
    }
    return true;
}

bool gpu_start(Game *game)
{
    if (game->back.format != 22 || !game->raster_divisor)
        return false;
    struct NativeGpu *gpu = calloc(1, sizeof *gpu);
    if (!gpu)
        return false;
    game->gpu = gpu;
    gpu->width = game->frame_width;
    gpu->height = game->frame_height;
    DkDeviceMaker device;
    dkDeviceMakerDefaults(&device);
    gpu->device = dkDeviceCreate(&device);
    if (!gpu->device)
        goto fail;
    DkQueueMaker queue;
    dkQueueMakerDefaults(&queue, gpu->device);
    queue.flags = DkQueueFlags_Graphics;
    DkCmdBufMaker commands;
    dkCmdBufMakerDefaults(&commands, gpu->device);
    gpu->queue = dkQueueCreate(&queue);
    gpu->commands = dkCmdBufCreate(&commands);
    gpu->commands_memory = memory(gpu, COMMAND_BYTES, upload_flags);
    gpu->upload = memory(gpu, UPLOAD_BYTES, upload_flags);
    gpu->readback = memory(gpu, 2 * gpu->width * gpu->height * 4 + 256, upload_flags);
    gpu->descriptors = memory(gpu, sampler_offset() + SAMPLER_COUNT * sizeof(DkSamplerDescriptor), upload_flags);
    gpu->color_memory = image(gpu, &gpu->color, gpu->width, gpu->height, DkImageFormat_RGBA8_Unorm,
                              DkImageFlags_UsageRender);
    gpu->depth_memory = image(gpu, &gpu->depth, gpu->width, gpu->height, DkImageFormat_ZF32,
                              DkImageFlags_UsageRender);
    if (!gpu->queue || !gpu->commands || !gpu->commands_memory || !gpu->upload || !gpu->readback ||
        !gpu->descriptors || !gpu->color_memory || !gpu->depth_memory || !init_programs(gpu))
        goto fail;

    uint8_t *table = dkMemBlockGetCpuAddr(gpu->descriptors);
    for (unsigned i = 0; i < SAMPLER_COUNT; ++i) {
        const DkWrapMode wrap[] = {DkWrapMode_Repeat, DkWrapMode_MirroredRepeat, DkWrapMode_ClampToEdge};
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.wrapMode[0] = wrap[i % 3];
        sampler.wrapMode[1] = wrap[(i / 3) % 3];
        sampler.minFilter = sampler.magFilter = i < 9 ? DkFilter_Linear : DkFilter_Nearest;
        DkSamplerDescriptor descriptor;
        dkSamplerDescriptorInitialize(&descriptor, &sampler);
        memcpy(table + sampler_offset() + i * sizeof descriptor, &descriptor, sizeof descriptor);
    }
    begin(gpu);
    gpu_clear(game, 3, 0xff000000u, 1.f);
    fprintf(game->log, "Live deko3d rasterizer initialized at %ux%u\n", gpu->width, gpu->height);
    return true;
fail:
    gpu_close(game);
    return false;
}

void gpu_close(Game *game)
{
    struct NativeGpu *gpu = game->gpu;
    if (!gpu)
        return;
    if (gpu->queue)
        dkQueueWaitIdle(gpu->queue);
    if (gpu->commands)
        dkCmdBufDestroy(gpu->commands);
    if (gpu->queue)
        dkQueueDestroy(gpu->queue);
    for (unsigned i = 0; i < MAX_GPU_TEXTURES; ++i)
        if (gpu->textures[i].memory)
            dkMemBlockDestroy(gpu->textures[i].memory);
    const DkMemBlock blocks[] = {gpu->commands_memory, gpu->upload, gpu->readback, gpu->code,
                                 gpu->descriptors, gpu->color_memory, gpu->depth_memory};
    for (unsigned i = 0; i < COUNT(blocks); ++i)
        if (blocks[i])
            dkMemBlockDestroy(blocks[i]);
    if (gpu->device)
        dkDeviceDestroy(gpu->device);
    free(gpu);
    game->gpu = NULL;
}

void gpu_clear(Game *game, unsigned flags, uint32_t color, float depth)
{
    DkCmdBuf commands = game->gpu->commands;
    if (flags & 1) {
        float rgba[] = {(color >> 16 & 255) / 255.f, (color >> 8 & 255) / 255.f,
                        (color & 255) / 255.f, (color >> 24) / 255.f};
        dkCmdBufClearColor(commands, 0, DkColorMask_RGBA, rgba);
    }
    if (flags & 2)
        dkCmdBufClearDepthStencil(commands, true, depth, 0, 0);
}

static bool read_frame(Game *game, bool copy_depth)
{
    struct NativeGpu *gpu = game->gpu;
    unsigned pixels = gpu->width * gpu->height, bytes = pixels * 4;
    unsigned depth_offset = align_up(bytes, 256);
    DkGpuAddr address = dkMemBlockGetGpuAddr(gpu->readback);
    DkImageRect rect = {0, 0, 0, gpu->width, gpu->height, 1};
    DkImageView view;
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, 0);
    dkImageViewDefaults(&view, &gpu->color);
    const DkCopyBuf color = {.addr = address};
    dkCmdBufCopyImageToBuffer(gpu->commands, &view, &rect, &color, 0);
    if (copy_depth) {
        dkImageViewDefaults(&view, &gpu->depth);
        const DkCopyBuf depth = {.addr = address + depth_offset};
        dkCmdBufCopyImageToBuffer(gpu->commands, &view, &rect, &depth, 0);
    }
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    if (!submit(gpu))
        return false;
    const uint8_t *data = dkMemBlockGetCpuAddr(gpu->readback);
    memcpy(game->pixels, data, bytes);
    if (copy_depth) {
        unsigned step = game->raster_divisor, stride = game->back.width;
        const float *depth = (const float *)(data + depth_offset);
        for (unsigned y = 0; y < gpu->height; ++y)
            for (unsigned x = 0; x < gpu->width; ++x)
                game->depth_pixels[(y * stride + x) * step] = depth[y * gpu->width + x];
    }
    return true;
}

bool gpu_present(Game *game)
{
    if (!read_frame(game, false))
        return false;
    struct NativeGpu *gpu = game->gpu;
    ++gpu->frames;
    if (gpu->frames == 1 || !(gpu->frames % 30))
        fprintf(game->log, "Live deko3d frame #%u: %u draws\n", game->presents, gpu->submitted_draws);
    gpu->submitted_draws = 0;
    return true;
}

bool gpu_fallback(Game *game, const char *reason)
{
    bool copied = read_frame(game, true);
    fprintf(game->log, "Live deko3d switched to CPU at Present #%u: %s\n", game->presents, reason);
    gpu_close(game);
    game->gpu_requested = false;
    return copied;
}

static bool reserve(struct NativeGpu *gpu, unsigned bytes, unsigned *offset)
{
    *offset = align_up(gpu->cursor, 256);
    if (bytes > UPLOAD_BYTES)
        return false;
    if (*offset > UPLOAD_BYTES - bytes) {
        if (!submit(gpu))
            return false;
        *offset = 0;
    }
    gpu->cursor = *offset + bytes;
    return true;
}

static int texture(struct NativeGpu *gpu, const Surface *source)
{
    unsigned i = 0;
    while (i < MAX_GPU_TEXTURES && gpu->textures[i].surface && gpu->textures[i].surface != source)
        ++i;
    if (i == MAX_GPU_TEXTURES || !source->gpu_generation || !source->decoded)
        return -1;
    GpuTexture *entry = &gpu->textures[i];
    if (entry->generation == source->gpu_generation)
        return i;
    if (entry->memory) {
        if (!submit(gpu))
            return -1;
        dkMemBlockDestroy(entry->memory);
        entry->memory = NULL;
        entry->generation = 0;
    }
    unsigned bytes = source->width * source->height * 4, offset;
    if (!reserve(gpu, bytes, &offset))
        return -1;
    entry->memory = image(gpu, &entry->image, source->width, source->height, DkImageFormat_RGBA8_Unorm, 0);
    if (!entry->memory)
        return -1;
    entry->surface = source;
    entry->generation = source->gpu_generation;
    uint8_t *upload = dkMemBlockGetCpuAddr(gpu->upload);
    memcpy(upload + offset, source->decoded, bytes);
    const DkCopyBuf buffer = {.addr = dkMemBlockGetGpuAddr(gpu->upload) + offset};
    DkImageView view;
    dkImageViewDefaults(&view, &entry->image);
    const DkImageRect rect = {0, 0, 0, source->width, source->height, 1};
    dkCmdBufCopyBufferToImage(gpu->commands, &buffer, &view, &rect, 0);
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    DkImageDescriptor descriptor;
    dkImageDescriptorInitialize(&descriptor, &view, false, false);
    uint8_t *table = dkMemBlockGetCpuAddr(gpu->descriptors);
    memcpy(table + i * sizeof descriptor, &descriptor, sizeof descriptor);
    return i;
}

bool gpu_draw(Game *game, const GpuVertex *vertices, unsigned count,
              const Surface *const textures[16], unsigned sampler_mask)
{
    if (!count)
        return true;
    struct NativeGpu *gpu = game->gpu;
    int source = game->pixel_shader ? program_source_index(game, game->pixel_shader) : textures[0] ? -1 : -2;
    if (game->pixel_shader && source < 0)
        return false;
    unsigned program = 1;
    while (program < COUNT(gpu_programs) && gpu_programs[program].source != source)
        ++program;
    if (program == COUNT(gpu_programs))
        return false;
    DkResHandle handles[16] = {0};
    for (unsigned slot = 0; slot < 16; ++slot) {
        if (!(sampler_mask & (1u << slot)))
            continue;
        unsigned u = game->samplers[slot][1], v = game->samplers[slot][2];
        if (u > 3 || v > 3 || !textures[slot])
            return false;
        int index = texture(gpu, textures[slot]);
        if (index < 0)
            return false;
        unsigned sampler = (u > 1 ? u - 1 : 0) + 3 * (v > 1 ? v - 1 : 0);
        if (game->pixel_shader && game->samplers[slot][5] == 1 && game->samplers[slot][6] == 1)
            sampler += 9;
        handles[slot] = dkMakeTextureHandle(index, sampler);
    }

    unsigned bytes = count * sizeof *vertices, geometry = align_up(bytes, 256), offset;
    if (!reserve(gpu, geometry + sizeof game->pixel_constants, &offset))
        return false;
    uint8_t *upload = dkMemBlockGetCpuAddr(gpu->upload);
    memcpy(upload + offset, vertices, bytes);
    memcpy(upload + offset + geometry, game->pixel_constants, sizeof game->pixel_constants);
    DkGpuAddr address = dkMemBlockGetGpuAddr(gpu->upload) + offset;
    DkCmdBuf commands = gpu->commands;
    const DkShader *programs[] = {&gpu->programs[0], &gpu->programs[program]};
    dkCmdBufBindShaders(commands, DkStageFlag_GraphicsMask, programs, 2);
    if (game->pixel_shader)
        dkCmdBufBindUniformBuffer(commands, DkStage_Fragment, 0, address + geometry, sizeof game->pixel_constants);
    if (sampler_mask)
        dkCmdBufBindTextures(commands, DkStage_Fragment, 0, handles, 16);
    DkVtxAttribState attributes[SHADER_VARYINGS + 1];
    for (unsigned i = 0; i < COUNT(attributes); ++i)
        attributes[i] = (DkVtxAttribState){.offset = i * 16, .size = DkVtxAttribSize_4x32,
                                          .type = DkVtxAttribType_Float};
    const DkVtxBufferState buffer = {.stride = sizeof *vertices};
    dkCmdBufBindVtxAttribState(commands, attributes, COUNT(attributes));
    dkCmdBufBindVtxBufferState(commands, &buffer, 1);
    dkCmdBufBindVtxBuffer(commands, 0, address, bytes);

    float scale = 1.f / game->raster_divisor;
    const DkViewport viewport = {game->viewport[0] * scale, game->viewport[1] * scale,
                                 game->viewport[2] * scale, game->viewport[3] * scale, 0.f, 1.f};
    dkCmdBufSetViewports(commands, 0, &viewport, 1);
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = game->raster[22] <= 1 ? DkFace_None : DkFace_Back;
    rasterizer.frontFace = game->raster[22] == 3 ? DkFrontFace_CW : DkFrontFace_CCW;
    dkCmdBufBindRasterizerState(commands, &rasterizer);
    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = game->raster[7] != 0;
    depth.depthWriteEnable = depth.depthTestEnable && game->raster[14];
    depth.depthCompareOp = DkCompareOp_Lequal;
    dkCmdBufBindDepthStencilState(commands, &depth);
    DkColorState color;
    dkColorStateDefaults(&color);
    dkColorStateSetBlendEnable(&color, 0, game->raster[27] != 0);
    if (game->raster[15] && game->raster[25] >= 1 && game->raster[25] <= 8) {
        color.alphaCompareOp = game->raster[25];
        dkCmdBufSetAlphaRef(commands, (game->raster[24] & 255) / 255.f);
    }
    dkCmdBufBindColorState(commands, &color);
    if (game->raster[27]) {
        DkBlendState blend;
        dkBlendStateDefaults(&blend);
        dkBlendStateSetFactors(&blend, game->raster[19], game->raster[20],
                               game->raster[19], game->raster[20]);
        dkCmdBufBindBlendState(commands, 0, &blend);
    }
    DkColorWriteState mask;
    dkColorWriteStateDefaults(&mask);
    dkColorWriteStateSetMask(&mask, 0, game->raster[D3D_COLOR_WRITE_ENABLE] & DkColorMask_RGB);
    dkCmdBufBindColorWriteState(commands, &mask);
    dkCmdBufDraw(commands, DkPrimitive_Triangles, count, 1, 0, 0);
    ++gpu->submitted_draws;
    return true;
}
#endif

#include "gpu.h"
#ifdef __SWITCH__
#include <deko3d.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

enum {
    COMMAND_BYTES = 1024 * 1024, UPLOAD_BYTES = 16 * 1024 * 1024,
    MAX_GPU_TEXTURES = MAX_TEXTURES + MAX_SURFACES, SAMPLER_COUNT = 18,
    HD_WIDTH = 1280, HD_HEIGHT = 720, FRAME_COUNT = 2,
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
    unsigned width, height;
    bool target, rendered;
} GpuTexture;

typedef struct {
    DkMemBlock memory;
    DkImage image;
    Surface *surface;
    unsigned width, height;
    bool rendered;
} GpuDepth;

struct NativeGpu {
    DkDevice device;
    DkQueue queue;
    DkSwapchain swapchain;
    DkCmdBuf commands;
    DkMemBlock commands_memory, upload, readback, code, descriptors, color_memory, depth_memory;
    DkMemBlock output_memory[FRAME_COUNT];
    DkImage color, depth, output[FRAME_COUNT];
    DkFence frame_fences[FRAME_COUNT];
    bool frame_pending[FRAME_COUNT];
    bool target_initialized;
    DkShader programs[COUNT(gpu_programs)];
    GpuTexture textures[MAX_GPU_TEXTURES];
    GpuDepth offscreen_depth[MAX_SURFACES];
    const DkImage *target_color, *target_depth;
    DkScissor target_area;
    bool target_alpha, target_dirty;
    unsigned scene_x, scene_y, scene_width, scene_height;
    unsigned arena, cursor, submitted_draws, frames;
    uint64_t geometry_bytes, expanded_bytes;
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

static void bind_target(struct NativeGpu *gpu)
{
    DkImageView color, depth;
    dkImageViewDefaults(&color, gpu->target_color);
    if (gpu->target_depth)
        dkImageViewDefaults(&depth, gpu->target_depth);
    dkCmdBufBindRenderTarget(gpu->commands, &color, gpu->target_depth ? &depth : NULL);
    dkCmdBufSetScissors(gpu->commands, 0, &gpu->target_area, 1);
    gpu->target_dirty = false;
}

static void begin(struct NativeGpu *gpu)
{
    dkCmdBufClear(gpu->commands);
    dkCmdBufAddMemory(gpu->commands, gpu->commands_memory, gpu->arena * COMMAND_BYTES, COMMAND_BYTES);
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, 0);
    bind_target(gpu);
    if (!gpu->target_initialized) {
        const DkScissor full = {0, 0, HD_WIDTH, HD_HEIGHT};
        const float black[] = {0, 0, 0, 1};
        dkCmdBufSetScissors(gpu->commands, 0, &full, 1);
        dkCmdBufClearColor(gpu->commands, 0, DkColorMask_RGBA, black);
        dkCmdBufClearDepthStencil(gpu->commands, true, 1.f, 0, 0);
        gpu->target_initialized = true;
    }
    dkCmdBufSetScissors(gpu->commands, 0, &gpu->target_area, 1);
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

static bool create_output(struct NativeGpu *gpu)
{
    const DkImage *images[FRAME_COUNT];
    for (unsigned i = 0; i < FRAME_COUNT; ++i) {
        gpu->output_memory[i] = image(gpu, &gpu->output[i], HD_WIDTH, HD_HEIGHT,
                                      DkImageFormat_RGBA8_Unorm,
                                      DkImageFlags_UsagePresent | DkImageFlags_UsageRender);
        if (!gpu->output_memory[i])
            return false;
        images[i] = &gpu->output[i];
    }
    DkSwapchainMaker swapchain;
    dkSwapchainMakerDefaults(&swapchain, gpu->device, nwindowGetDefault(), images, FRAME_COUNT);
    gpu->swapchain = dkSwapchainCreate(&swapchain);
    return gpu->swapchain != NULL;
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
    gpu->scene_width = HD_WIDTH;
    gpu->scene_height = HD_WIDTH * game->back.height / game->back.width;
    if (gpu->scene_height > HD_HEIGHT) {
        gpu->scene_height = HD_HEIGHT;
        gpu->scene_width = HD_HEIGHT * game->back.width / game->back.height;
    }
    gpu->scene_x = (HD_WIDTH - gpu->scene_width) / 2;
    gpu->scene_y = (HD_HEIGHT - gpu->scene_height) / 2;
    gpu->target_color = &gpu->color;
    gpu->target_depth = &gpu->depth;
    gpu->target_area = (DkScissor){gpu->scene_x, gpu->scene_y, gpu->scene_width, gpu->scene_height};
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
    gpu->commands_memory = memory(gpu, COMMAND_BYTES * FRAME_COUNT, upload_flags);
    gpu->upload = memory(gpu, UPLOAD_BYTES * FRAME_COUNT, upload_flags);
    gpu->descriptors = memory(gpu, sampler_offset() + SAMPLER_COUNT * sizeof(DkSamplerDescriptor), upload_flags);
    gpu->color_memory = image(gpu, &gpu->color, HD_WIDTH, HD_HEIGHT, DkImageFormat_RGBA8_Unorm,
                              DkImageFlags_UsageRender);
    gpu->depth_memory = image(gpu, &gpu->depth, HD_WIDTH, HD_HEIGHT, DkImageFormat_ZF32,
                              DkImageFlags_UsageRender);
    if (!gpu->queue || !gpu->commands || !gpu->commands_memory || !gpu->upload ||
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
    fprintf(game->log, "Live deko3d initialized: %ux%u output, %ux%u game viewport\n",
            HD_WIDTH, HD_HEIGHT, gpu->scene_width, gpu->scene_height);
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
    if (gpu->expanded_bytes)
        fprintf(game->log, "GPU geometry: %llu bytes uploaded, %llu with expanded triangles\n",
                (unsigned long long)gpu->geometry_bytes, (unsigned long long)gpu->expanded_bytes);
    if (gpu->queue)
        dkQueueWaitIdle(gpu->queue);
    if (gpu->swapchain)
        dkSwapchainDestroy(gpu->swapchain);
    if (gpu->commands)
        dkCmdBufDestroy(gpu->commands);
    if (gpu->queue)
        dkQueueDestroy(gpu->queue);
    for (unsigned i = 0; i < MAX_GPU_TEXTURES; ++i)
        if (gpu->textures[i].memory)
            dkMemBlockDestroy(gpu->textures[i].memory);
    for (unsigned i = 0; i < COUNT(gpu->offscreen_depth); ++i)
        if (gpu->offscreen_depth[i].memory)
            dkMemBlockDestroy(gpu->offscreen_depth[i].memory);
    for (unsigned i = 0; i < FRAME_COUNT; ++i)
        if (gpu->output_memory[i])
            dkMemBlockDestroy(gpu->output_memory[i]);
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

static bool read_frame(Game *game)
{
    struct NativeGpu *gpu = game->gpu;
    unsigned bytes = gpu->scene_width * gpu->scene_height * 4;
    unsigned depth_offset = align_up(bytes, 256);
    gpu->readback = memory(gpu, depth_offset + bytes, upload_flags);
    if (!gpu->readback)
        return false;
    DkGpuAddr address = dkMemBlockGetGpuAddr(gpu->readback);
    DkImageRect rect = {gpu->scene_x, gpu->scene_y, 0, gpu->scene_width, gpu->scene_height, 1};
    DkImageView view;
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, 0);
    dkImageViewDefaults(&view, &gpu->color);
    const DkCopyBuf color = {.addr = address};
    dkCmdBufCopyImageToBuffer(gpu->commands, &view, &rect, &color, 0);
    dkImageViewDefaults(&view, &gpu->depth);
    const DkCopyBuf depth = {.addr = address + depth_offset};
    dkCmdBufCopyImageToBuffer(gpu->commands, &view, &rect, &depth, 0);
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    if (!submit(gpu))
        return false;
    const uint8_t *data = dkMemBlockGetCpuAddr(gpu->readback);
    const uint32_t *colors = (const uint32_t *)data;
    const float *depths = (const float *)(data + depth_offset);
    unsigned step = game->raster_divisor, stride = game->back.width;
    for (unsigned y = 0; y < game->frame_height; ++y) {
        unsigned src_y = (2 * y + 1) * gpu->scene_height / (2 * game->frame_height);
        for (unsigned x = 0; x < game->frame_width; ++x) {
            unsigned src_x = (2 * x + 1) * gpu->scene_width / (2 * game->frame_width);
            unsigned source = src_y * gpu->scene_width + src_x;
            game->pixels[y * game->frame_width + x] = colors[source];
            game->depth_pixels[(y * stride + x) * step] = depths[source];
        }
    }
    return true;
}

static bool download(struct NativeGpu *gpu, const DkImage *image, unsigned width, unsigned height, void *destination)
{
    if (!destination)
        return false;
    size_t bytes = (size_t)width * height * 4;
    DkMemBlock storage = memory(gpu, bytes, upload_flags);
    if (!storage)
        return false;
    DkImageView view;
    dkImageViewDefaults(&view, image);
    const DkImageRect rect = {0, 0, 0, width, height, 1};
    const DkCopyBuf buffer = {.addr = dkMemBlockGetGpuAddr(storage)};
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, 0);
    dkCmdBufCopyImageToBuffer(gpu->commands, &view, &rect, &buffer, 0);
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    bool copied = submit(gpu);
    if (copied)
        memcpy(destination, dkMemBlockGetCpuAddr(storage), bytes);
    dkMemBlockDestroy(storage);
    return copied;
}

bool gpu_present(Game *game)
{
    struct NativeGpu *gpu = game->gpu;
    if (!gpu->swapchain && !create_output(gpu))
        return gpu_fallback(game, "could not create the 720p swapchain");
    int slot = dkQueueAcquireImage(gpu->queue, gpu->swapchain);
    if (slot < 0 || slot >= FRAME_COUNT)
        return gpu_fallback(game, "could not acquire a 720p output image");

    DkImageView src, dst;
    dkImageViewDefaults(&src, &gpu->color);
    dkImageViewDefaults(&dst, &gpu->output[slot]);
    DkImageRect full = {0, 0, 0, HD_WIDTH, HD_HEIGHT, 1};
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, 0);
    dkCmdBufCopyImage(gpu->commands, &src, &full, &dst, &full, 0);
    DkCmdList list = dkCmdBufFinishList(gpu->commands);
    if (!list)
        return false;
    dkQueueSubmitCommands(gpu->queue, list);
    if (dkQueueIsInErrorState(gpu->queue))
        return false;
    dkQueueSignalFence(gpu->queue, &gpu->frame_fences[gpu->arena], false);
    gpu->frame_pending[gpu->arena] = true;
    dkQueuePresentImage(gpu->queue, gpu->swapchain, slot);
    if (dkQueueIsInErrorState(gpu->queue))
        return false;
    gpu->arena = (gpu->arena + 1) % FRAME_COUNT;
    if (gpu->frame_pending[gpu->arena]) {
        if (dkFenceWait(&gpu->frame_fences[gpu->arena], -1) != DkResult_Success)
            return false;
        gpu->frame_pending[gpu->arena] = false;
    }
    gpu->cursor = 0;
    begin(gpu);
    ++gpu->frames;
    if (gpu->frames == 1 || !(gpu->frames % 30))
        fprintf(game->log, "Live deko3d frame #%u: %u draws, 1280x720 swapchain\n",
                game->presents, gpu->submitted_draws);
    gpu->submitted_draws = 0;
    return true;
}

unsigned gpu_output_width(const Game *game)
{
    return game->gpu ? HD_WIDTH : game->frame_width;
}

unsigned gpu_output_height(const Game *game)
{
    return game->gpu ? HD_HEIGHT : game->frame_height;
}

bool gpu_fallback(Game *game, const char *reason)
{
    struct NativeGpu *gpu = game->gpu;
    bool copied = read_frame(game);
    for (unsigned i = 0; copied && i < MAX_GPU_TEXTURES; ++i) {
        const GpuTexture *entry = &gpu->textures[i];
        if (entry->rendered)
            copied = download(gpu, &entry->image, entry->width, entry->height, entry->surface->decoded);
    }
    for (unsigned i = 0; copied && i < COUNT(gpu->offscreen_depth); ++i) {
        const GpuDepth *entry = &gpu->offscreen_depth[i];
        if (entry->rendered)
            copied = download(gpu, &entry->image, entry->width, entry->height, entry->surface->depth_samples);
    }
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
    *offset += gpu->arena * UPLOAD_BYTES;
    return true;
}

static int texture(struct NativeGpu *gpu, const Surface *source, bool as_target)
{
    unsigned i, available = MAX_GPU_TEXTURES;
    for (i = 0; i < MAX_GPU_TEXTURES; ++i) {
        if (gpu->textures[i].surface == source)
            break;
        if (!gpu->textures[i].surface && available == MAX_GPU_TEXTURES)
            available = i;
    }
    if (i == MAX_GPU_TEXTURES)
        i = available;
    if (i == MAX_GPU_TEXTURES || !source->decoded)
        return -1;
    GpuTexture *entry = &gpu->textures[i];
    if (entry->surface == source && (entry->rendered ||
        (source->gpu_generation && entry->generation == source->gpu_generation)))
        return i;
    if (!as_target && !source->gpu_generation)
        return -1;
    bool target = (source->usage & 1) != 0;
    if (entry->memory && (entry->target != target || entry->width != source->width || entry->height != source->height)) {
        if (!submit(gpu))
            return -1;
        dkMemBlockDestroy(entry->memory);
        entry->memory = NULL;
        entry->generation = 0;
    }
    unsigned bytes = source->width * source->height * 4, offset = 0;
    uint32_t generation = source->gpu_generation;
    if (generation && !reserve(gpu, bytes, &offset))
        return -1;
    bool fresh = !entry->memory;
    if (fresh) {
        entry->memory = image(gpu, &entry->image, source->width, source->height, DkImageFormat_RGBA8_Unorm,
                              target ? DkImageFlags_UsageRender : 0);
        if (!entry->memory)
            return -1;
        entry->width = source->width;
        entry->height = source->height;
        entry->target = target;
    }
    entry->surface = source;
    entry->generation = generation;
    DkImageView view;
    dkImageViewDefaults(&view, &entry->image);
    if (generation) {
        uint8_t *upload = dkMemBlockGetCpuAddr(gpu->upload);
        memcpy(upload + offset, source->decoded, bytes);
        const DkCopyBuf buffer = {.addr = dkMemBlockGetGpuAddr(gpu->upload) + offset};
        const DkImageRect rect = {0, 0, 0, source->width, source->height, 1};
        if (!fresh)
            dkCmdBufBarrier(gpu->commands, DkBarrier_Full, 0);
        dkCmdBufCopyBufferToImage(gpu->commands, &buffer, &view, &rect, 0);
    }
    if (fresh) {
        DkImageDescriptor descriptor;
        dkImageDescriptorInitialize(&descriptor, &view, false, false);
        uint8_t *table = dkMemBlockGetCpuAddr(gpu->descriptors);
        memcpy(table + i * sizeof descriptor, &descriptor, sizeof descriptor);
    }
    dkCmdBufBarrier(gpu->commands, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    return i;
}

static GpuDepth *depth_target(struct NativeGpu *gpu, Surface *surface)
{
    GpuDepth *entry = NULL;
    for (unsigned i = 0; i < COUNT(gpu->offscreen_depth); ++i) {
        GpuDepth *current = &gpu->offscreen_depth[i];
        if (current->surface == surface)
            return current;
        if (!entry && !current->surface)
            entry = current;
    }
    if (!entry)
        return NULL;
    if (entry->memory && (entry->width != surface->width || entry->height != surface->height)) {
        if (!submit(gpu))
            return NULL;
        dkMemBlockDestroy(entry->memory);
        entry->memory = NULL;
    }
    if (!entry->memory) {
        entry->memory = image(gpu, &entry->image, surface->width, surface->height,
                              DkImageFormat_ZF32, DkImageFlags_UsageRender);
        if (!entry->memory)
            return NULL;
        entry->width = surface->width;
        entry->height = surface->height;
    }
    entry->surface = surface;
    return entry;
}

bool gpu_set_target(Game *game, Surface *color, Surface *depth)
{
    struct NativeGpu *gpu = game->gpu;
    bool primary = color == &game->back;
    if ((depth == &game->depth && !primary) || (depth && depth != &game->depth && primary))
        return false;
    if (!primary && depth && (color->width != depth->width || color->height != depth->height))
        return false;
    GpuTexture *color_entry = NULL;
    GpuDepth *depth_entry = NULL;
    if (!primary) {
        int index = texture(gpu, color, true);
        if (index < 0)
            return false;
        color_entry = &gpu->textures[index];
        if (depth && !(depth_entry = depth_target(gpu, depth)))
            return false;
    }
    const DkImage *color_image = primary ? &gpu->color : &color_entry->image;
    const DkImage *depth_image = depth ? (primary ? &gpu->depth : &depth_entry->image) : NULL;
    if (gpu->target_dirty || color_image != gpu->target_color || depth_image != gpu->target_depth) {
        dkCmdBufBarrier(gpu->commands, DkBarrier_Full, DkInvalidateFlags_Image);
        gpu->target_color = color_image;
        gpu->target_depth = depth_image;
        gpu->target_area = primary ? (DkScissor){gpu->scene_x, gpu->scene_y, gpu->scene_width, gpu->scene_height}
                                   : (DkScissor){0, 0, color->width, color->height};
        gpu->target_alpha = color->format == 21;
        bind_target(gpu);
    }
    if (color_entry && !color_entry->rendered) {
        if (!color_entry->generation) {
            const float black[4] = {0, 0, 0, !gpu->target_alpha};
            dkCmdBufClearColor(gpu->commands, 0, DkColorMask_RGBA, black);
        }
        color_entry->rendered = true;
        fprintf(game->log, "Live deko3d offscreen target: %ux%u\n", color->width, color->height);
    }
    if (depth_entry && !depth_entry->rendered) {
        dkCmdBufClearDepthStencil(gpu->commands, true, 1.f, 0, 0);
        depth_entry->rendered = true;
    }
    return true;
}

void gpu_release_surface(Game *game, const Surface *surface)
{
    struct NativeGpu *gpu = game->gpu;
    bool bound = false;
    for (unsigned i = 0; i < MAX_GPU_TEXTURES; ++i) {
        GpuTexture *entry = &gpu->textures[i];
        if (entry->surface == surface) {
            entry->surface = NULL;
            entry->generation = 0;
            entry->rendered = false;
            bound = gpu->target_color == &entry->image;
            break;
        }
    }
    for (unsigned i = 0; i < COUNT(gpu->offscreen_depth); ++i) {
        GpuDepth *entry = &gpu->offscreen_depth[i];
        if (entry->surface == surface) {
            entry->surface = NULL;
            entry->rendered = false;
            bound |= gpu->target_depth == &entry->image;
            break;
        }
    }
    if (bound) {
        gpu->target_color = &gpu->color;
        gpu->target_depth = &gpu->depth;
        gpu->target_area = (DkScissor){gpu->scene_x, gpu->scene_y, gpu->scene_width, gpu->scene_height};
        gpu->target_alpha = false;
        gpu->target_dirty = true;
    }
}

bool gpu_draw(Game *game, const GpuVertex *vertices, unsigned count,
              const uint16_t *indices, unsigned index_count,
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
        int index = texture(gpu, textures[slot], false);
        if (index < 0 || &gpu->textures[index].image == gpu->target_color)
            return false;
        unsigned sampler = (u > 1 ? u - 1 : 0) + 3 * (v > 1 ? v - 1 : 0);
        if (game->pixel_shader && game->samplers[slot][5] == 1 && game->samplers[slot][6] == 1)
            sampler += 9;
        handles[slot] = dkMakeTextureHandle(index, sampler);
    }

    unsigned bytes = count * sizeof *vertices, index_bytes = index_count * sizeof *indices;
    unsigned index_offset = align_up(bytes, 256), constants_offset = align_up(index_offset + index_bytes, 256), offset;
    if (!reserve(gpu, constants_offset + sizeof game->pixel_constants, &offset))
        return false;
    uint8_t *upload = dkMemBlockGetCpuAddr(gpu->upload);
    memcpy(upload + offset, vertices, bytes);
    memcpy(upload + offset + index_offset, indices, index_bytes);
    memcpy(upload + offset + constants_offset, game->pixel_constants, sizeof game->pixel_constants);
    DkGpuAddr address = dkMemBlockGetGpuAddr(gpu->upload) + offset;
    DkCmdBuf commands = gpu->commands;
    const DkShader *programs[] = {&gpu->programs[0], &gpu->programs[program]};
    dkCmdBufBindShaders(commands, DkStageFlag_GraphicsMask, programs, 2);
    if (game->pixel_shader)
        dkCmdBufBindUniformBuffer(commands, DkStage_Fragment, 0, address + constants_offset, sizeof game->pixel_constants);
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

    bool main_target = gpu->target_color == &gpu->color;
    float sx = main_target ? (float)gpu->scene_width / game->back.width : 1.f;
    float sy = main_target ? (float)gpu->scene_height / game->back.height : 1.f;
    const DkViewport viewport = {gpu->target_area.x + game->viewport[0] * sx,
                                 gpu->target_area.y + game->viewport[1] * sy,
                                 game->viewport[2] * sx, game->viewport[3] * sy, 0.f, 1.f};
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
    unsigned channels = gpu->target_alpha ? DkColorMask_RGBA : DkColorMask_RGB;
    dkColorWriteStateSetMask(&mask, 0, game->raster[D3D_COLOR_WRITE_ENABLE] & channels);
    dkCmdBufBindColorWriteState(commands, &mask);
    dkCmdBufBindIdxBuffer(commands, DkIdxFormat_Uint16, address + index_offset);
    dkCmdBufDrawIndexed(commands, DkPrimitive_Triangles, index_count, 1, 0, 0, 0);
    gpu->geometry_bytes += bytes + index_bytes;
    gpu->expanded_bytes += index_count * sizeof *vertices;
    ++gpu->submitted_draws;
    return true;
}
#endif

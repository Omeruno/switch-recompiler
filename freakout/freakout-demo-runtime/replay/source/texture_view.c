#include "texture_view.h"
#include "replay_gpu.h"
#include "freakout_texture.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>

enum { PAYLOAD_OFFSET = 12593121, SAMPLER_OFFSET = 0x100 };
static const char archive_path[] = "sdmc:/switch/FreakOutGpuReplay/Data2.bnd";
static const uint8_t payload_hash[32] = {
    0x9c, 0xca, 0x3b, 0x05, 0x0f, 0xec, 0x4f, 0xc9,
    0x5e, 0x29, 0x73, 0x10, 0x69, 0xc8, 0x93, 0x19,
    0x2c, 0x6e, 0x7b, 0x26, 0xad, 0xd2, 0xc5, 0x9b,
    0xa8, 0x3f, 0x5d, 0xc8, 0xb8, 0x21, 0x3e, 0x28,
};

static const char *upload_texture(TextureView *view, DkDevice device, DkQueue queue,
                                  DkCmdBuf commands, DkMemBlock command_memory, uint32_t command_bytes)
{
    FreakOutTextureLayout footprint;
    if (!freakout_dxt_layout(0x35545844u, VIEW_WIDTH, VIEW_HEIGHT, 0, &footprint))
        return "Unsupported BC3 texture layout.";
    FILE *file = fopen(archive_path, "rb");
    if (!file)
        return "Put the demo Data2.bnd beside the NRO.";

    const uint32_t staging_flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    DkMemBlock staging = replay_memory(device, footprint.size, staging_flags);
    if (!staging) {
        fclose(file);
        return "Could not allocate texture staging memory.";
    }
    uint8_t *data = dkMemBlockGetCpuAddr(staging);
    bool read_ok = fseek(file, PAYLOAD_OFFSET, SEEK_SET) == 0 &&
                   fread(data, 1, footprint.size, file) == footprint.size;
    fclose(file);
    uint8_t hash[32];
    if (read_ok)
        sha256CalculateHash(hash, data, footprint.size);
    if (!read_ok || memcmp(hash, payload_hash, sizeof hash) != 0) {
        dkMemBlockDestroy(staging);
        return "Wrong Data2.bnd or truncated BC3 payload.";
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.format = footprint.format;
    layout_maker.dimensions[0] = VIEW_WIDTH;
    layout_maker.dimensions[1] = VIEW_HEIGHT;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layout_maker);
    view->storage = replay_memory(device, dkImageLayoutGetSize(&layout),
                                DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!view->storage) {
        dkMemBlockDestroy(staging);
        return "Could not allocate the BC3 GPU image.";
    }

    dkImageInitialize(&view->image, &layout, view->storage, 0);
    DkImageView image;
    dkImageViewDefaults(&image, &view->image);
    const DkCopyBuf source = { .addr = dkMemBlockGetGpuAddr(staging) };
    const DkImageRect rectangle = { 0, 0, 0, VIEW_WIDTH, VIEW_HEIGHT, 1 };
    dkCmdBufCopyBufferToImage(commands, &source, &image, &rectangle, 0);
    dkCmdBufBarrier(commands, DkBarrier_Full, DkInvalidateFlags_Image);
    dkQueueSubmitCommands(queue, dkCmdBufFinishList(commands));
    dkQueueWaitIdle(queue);
    dkCmdBufClear(commands);
    dkCmdBufAddMemory(commands, command_memory, 0, command_bytes);
    dkMemBlockDestroy(staging);

    DkImageDescriptor image_descriptor;
    dkImageDescriptorInitialize(&image_descriptor, &image, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
    DkSamplerDescriptor sampler_descriptor;
    dkSamplerDescriptorInitialize(&sampler_descriptor, &sampler);
    uint8_t *descriptor_memory = dkMemBlockGetCpuAddr(view->descriptors);
    memcpy(descriptor_memory, &image_descriptor, sizeof image_descriptor);
    memcpy(descriptor_memory + SAMPLER_OFFSET, &sampler_descriptor, sizeof sampler_descriptor);
    return NULL;
}

const char *texture_view_init(TextureView *view, DkDevice device, DkQueue queue,
                              DkCmdBuf commands, DkMemBlock command_memory, uint32_t command_bytes)
{
    uint32_t flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    view->code = replay_memory(device, REPLAY_SHADER_STRIDE * 2, flags | DkMemBlockFlags_Code);
    view->descriptors = replay_memory(device, DK_MEMBLOCK_ALIGNMENT, flags);
    if (!view->code || !view->descriptors)
        return "Could not allocate shader or descriptor memory.";
    if (!replay_load_shader(view->code, &view->vertex, 0, "romfs:/texture_vsh.dksh") ||
        !replay_load_shader(view->code, &view->fragment, REPLAY_SHADER_STRIDE, "romfs:/texture_fsh.dksh"))
        return "Could not load the diagnostic shaders.";
    return upload_texture(view, device, queue, commands, command_memory, command_bytes);
}

void texture_view_draw(const TextureView *view, DkCmdBuf commands)
{
    DkGpuAddr descriptors = dkMemBlockGetGpuAddr(view->descriptors);
    dkCmdBufBindImageDescriptorSet(commands, descriptors, 1);
    dkCmdBufBindSamplerDescriptorSet(commands, descriptors + SAMPLER_OFFSET, 1);
    const DkShader *shaders[] = { &view->vertex, &view->fragment };
    dkCmdBufBindShaders(commands, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindTexture(commands, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));

    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(commands, &rasterizer);
    DkColorState color;
    DkColorWriteState mask;
    dkColorStateDefaults(&color);
    dkColorWriteStateDefaults(&mask);
    dkCmdBufBindColorState(commands, &color);
    dkCmdBufBindColorWriteState(commands, &mask);
    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = false;
    depth.depthWriteEnable = false;
    dkCmdBufBindDepthStencilState(commands, &depth);
    dkCmdBufDraw(commands, DkPrimitive_Triangles, 3, 1, 0, 0);
}

void texture_view_destroy(TextureView *view)
{
    if (view->storage)
        dkMemBlockDestroy(view->storage);
    if (view->descriptors)
        dkMemBlockDestroy(view->descriptors);
    if (view->code)
        dkMemBlockDestroy(view->code);
}

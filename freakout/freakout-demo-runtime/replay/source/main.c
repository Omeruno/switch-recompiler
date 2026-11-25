#include <switch.h>
#include <deko3d.h>
#include <stdio.h>
#include <stdlib.h>
#include "freakout_clear.h"
#include "freakout_pad_nx.h"
#include "texture_view.h"
#include "frame_replay.h"
#include "replay_gpu.h"
#include "scene_replay.h"

enum { WIDTH = 1024, HEIGHT = 768, BUFFER_COUNT = 2, COMMAND_BYTES = 0x80000 };

typedef enum { ReplayClear, ReplayTextureView, ReplayGameFrame, ReplayMenu, ReplayScene } ReplayMode;

typedef struct {
    DkDevice device;
    DkQueue queue;
    DkSwapchain swapchain;
    DkCmdBuf commands;
    DkMemBlock color_memory, depth_memory, command_memory;
    DkImage color[BUFFER_COUNT], depth;
    DkCmdList frame[BUFFER_COUNT];
    TextureView texture;
    FrameReplay game_frame;
    SceneReplay *scene;
} Replay;

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static bool create_images(Replay *replay)
{
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, replay->device);
    maker.dimensions[0] = WIDTH;
    maker.dimensions[1] = HEIGHT;
    maker.format = DkImageFormat_RGBA8_Unorm;
    maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent;
    DkImageLayout color, depth;
    dkImageLayoutInitialize(&color, &maker);
    uint32_t stride = align_up(dkImageLayoutGetSize(&color), dkImageLayoutGetAlignment(&color));

    maker.format = DkImageFormat_ZF32;
    maker.flags = DkImageFlags_UsageRender;
    dkImageLayoutInitialize(&depth, &maker);

    uint32_t flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    replay->color_memory = replay_memory(replay->device, stride * BUFFER_COUNT, flags);
    replay->depth_memory = replay_memory(replay->device, dkImageLayoutGetSize(&depth), flags);
    if (!replay->color_memory || !replay->depth_memory)
        return false;

    const DkImage *images[BUFFER_COUNT];
    for (unsigned i = 0; i < BUFFER_COUNT; i++) {
        dkImageInitialize(&replay->color[i], &color, replay->color_memory, i * stride);
        images[i] = &replay->color[i];
    }
    dkImageInitialize(&replay->depth, &depth, replay->depth_memory, 0);
    DkSwapchainMaker swapchain;
    dkSwapchainMakerDefaults(&swapchain, replay->device, nwindowGetDefault(), images, BUFFER_COUNT);
    replay->swapchain = dkSwapchainCreate(&swapchain);
    return replay->swapchain != NULL;
}

static const char *record_frames(Replay *replay, ReplayMode mode)
{
    replay->command_memory = replay_memory(replay->device, COMMAND_BYTES,
                                         DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!replay->command_memory)
        return "Could not prepare frame commands.";
    DkCmdBufMaker maker;
    dkCmdBufMakerDefaults(&maker, replay->device);
    replay->commands = dkCmdBufCreate(&maker);
    if (!replay->commands)
        return "Could not prepare frame commands.";
    dkCmdBufAddMemory(replay->commands, replay->command_memory, 0, COMMAND_BYTES);

    if (mode == ReplayTextureView) {
        const char *error = texture_view_init(&replay->texture, replay->device, replay->queue,
                                              replay->commands, replay->command_memory, COMMAND_BYTES);
        if (error)
            return error;
    }

    if (mode == ReplayGameFrame || mode == ReplayMenu) {
        const char *path = mode == ReplayMenu ? "sdmc:/switch/FreakOutGpuReplay/menu-frame.fofr"
                                            : "sdmc:/switch/FreakOutGpuReplay/first-frame.fofr";
        const char *error = frame_replay_init(&replay->game_frame, path, replay->device, replay->queue,
                                               replay->commands, replay->command_memory, COMMAND_BYTES);
        if (error)
            return error;
    }

    if (mode == ReplayScene) {
        replay->scene = calloc(1, sizeof *replay->scene);
        if (!replay->scene)
            return "Could not allocate the 3D scene state.";
        const char *error = scene_replay_init(replay->scene, "sdmc:/switch/FreakOutGpuReplay/main-menu.fos3",
                                               replay->device, replay->queue, replay->commands,
                                               replay->command_memory, COMMAND_BYTES);
        if (error)
            return error;
    }

    DkImageView depth;
    dkImageViewDefaults(&depth, &replay->depth);
    DkViewport viewport = { 0, 0, WIDTH, HEIGHT, 0, 1 };
    DkScissor scissor = { 0, 0, WIDTH, HEIGHT };
    const FreakOutRect full = { 0, 0, WIDTH, HEIGHT };
    FreakOutClear clear = { 1, &full, 3, 0xFF000000u, 1.0f, 0 };
    if (mode == ReplayGameFrame || mode == ReplayMenu) {
        clear.argb = replay->game_frame.clear_argb;
        clear.depth = replay->game_frame.clear_depth;
    }
    for (unsigned i = 0; i < BUFFER_COUNT; i++) {
        DkImageView color;
        dkImageViewDefaults(&color, &replay->color[i]);
        dkCmdBufBindRenderTarget(replay->commands, &color, &depth);
        dkCmdBufSetViewports(replay->commands, 0, &viewport, 1);
        dkCmdBufSetScissors(replay->commands, 0, &scissor, 1);
        if (!freakout_clear_full_target(replay->commands, 1, WIDTH, HEIGHT, &clear))
            return "Could not prepare frame commands.";
        if (mode == ReplayTextureView) {
            const DkViewport image_viewport = { 0, (HEIGHT - VIEW_HEIGHT) / 2, VIEW_WIDTH, VIEW_HEIGHT, 0, 1 };
            dkCmdBufSetViewports(replay->commands, 0, &image_viewport, 1);
            texture_view_draw(&replay->texture, replay->commands);
        } else if (mode == ReplayGameFrame || mode == ReplayMenu) {
            frame_replay_draw(&replay->game_frame, replay->commands);
        } else if (mode == ReplayScene) {
            scene_replay_draw(replay->scene, replay->commands);
        }
        replay->frame[i] = dkCmdBufFinishList(replay->commands);
        if (!replay->frame[i])
            return "Could not prepare frame commands.";
    }
    return NULL;
}

static const char *start(Replay *replay, ReplayMode mode)
{
    DkDeviceMaker device;
    dkDeviceMakerDefaults(&device);
    if (mode == ReplayScene)
        device.flags |= DkDeviceFlags_OriginLowerLeft;
    replay->device = dkDeviceCreate(&device);
    if (!replay->device || !create_images(replay))
        return "Could not prepare frame commands.";

    DkQueueMaker queue;
    dkQueueMakerDefaults(&queue, replay->device);
    queue.flags = DkQueueFlags_Graphics;
    replay->queue = dkQueueCreate(&queue);
    return replay->queue ? record_frames(replay, mode) : "Could not create the graphics queue.";
}

static void destroy(Replay *replay)
{
    if (replay->queue) {
        dkQueueWaitIdle(replay->queue);
        dkQueueDestroy(replay->queue);
    }
    if (replay->commands)
        dkCmdBufDestroy(replay->commands);
    texture_view_destroy(&replay->texture);
    frame_replay_destroy(&replay->game_frame);
    if (replay->scene) {
        scene_replay_destroy(replay->scene);
        free(replay->scene);
    }
    if (replay->command_memory)
        dkMemBlockDestroy(replay->command_memory);
    if (replay->swapchain) {
        dkSwapchainDestroy(replay->swapchain);
        if (replay->scene)
            nwindowSetTransform(nwindowGetDefault(), 0);
    }
    if (replay->depth_memory)
        dkMemBlockDestroy(replay->depth_memory);
    if (replay->color_memory)
        dkMemBlockDestroy(replay->color_memory);
    if (replay->device)
        dkDeviceDestroy(replay->device);
}

static int run_frames(PadState *pad, ReplayMode mode, const char **error)
{
    Replay replay = {0};
    int submitted = -1;
    *error = start(&replay, mode);
    if (!*error) {
        submitted = 0;
        while (appletMainLoop()) {
            padUpdate(pad);
            if (padGetButtonsDown(pad) & (HidNpadButton_B | HidNpadButton_Plus))
                break;
            int slot = dkQueueAcquireImage(replay.queue, replay.swapchain);
            if (slot < 0 || slot >= BUFFER_COUNT) {
                *error = "Could not acquire the next output image.";
                break;
            }
            dkQueueSubmitCommands(replay.queue, replay.frame[slot]);
            dkQueuePresentImage(replay.queue, replay.swapchain, slot);
            submitted++;
        }
    }
    destroy(&replay);
    return submitted;
}

static void test_pad(PadState *pad)
{
    consoleClear();
    printf("FreakOut: controller test\n");
    printf("Left stick/D-pad: arrows. A: Enter. B or +: Escape. -: back.\n\n");
    while (appletMainLoop()) {
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Minus)
            break;
        FoXboxPad xbox = fo_nx_pad(pad);
        uint8_t keys[256];
        fo_pad_keyboard(&xbox, keys);
        printf("\x1b[4;1H\x1b[J");
        printf("Xbox buttons %04X    LT %3u  RT %3u\n",
               (unsigned)xbox.buttons, (unsigned)xbox.left_trigger, (unsigned)xbox.right_trigger);
        printf("Left  X %6d  Y %6d\n", xbox.lx, xbox.ly);
        printf("Right X %6d  Y %6d\n\n", xbox.rx, xbox.ry);
        printf("DirectInput: %s%s%s%s%s%s\n",
               keys[FO_DIK_UP] ? "Up " : "", keys[FO_DIK_DOWN] ? "Down " : "",
               keys[FO_DIK_LEFT] ? "Left " : "", keys[FO_DIK_RIGHT] ? "Right " : "",
               keys[FO_DIK_RETURN] ? "Enter " : "", keys[FO_DIK_ESCAPE] ? "Escape" : "");
        consoleUpdate(NULL);
    }
    consoleClear();
    printf("FreakOut: GPU frame replay\n");
    printf("ZR: 3D. ZL: language. Y: loading. X: texture. A: clear. -: controller. +: exit.\n");
}

int main(void)
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    Result romfs = romfsInit();
    consoleInit(NULL);
    printf("FreakOut: GPU frame replay\n");
    printf("ZR: 3D scene. ZL: language menu. Y: loading. X: texture. A: clear.\n");
    printf("-: controller test. +: exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus)
            break;
        if (down & HidNpadButton_Minus) {
            test_pad(&pad);
            continue;
        }
        if (down & (HidNpadButton_A | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_ZL | HidNpadButton_ZR)) {
            ReplayMode mode = ReplayClear;
            if (down & HidNpadButton_ZR)
                mode = ReplayScene;
            else if (down & HidNpadButton_ZL)
                mode = ReplayMenu;
            else if (down & HidNpadButton_Y)
                mode = ReplayGameFrame;
            else if (down & HidNpadButton_X)
                mode = ReplayTextureView;
            if (mode != ReplayClear && mode != ReplayScene && R_FAILED(romfs)) {
                printf("Shader filesystem unavailable.\n");
                continue;
            }
            const char *error = NULL;
            consoleExit(NULL);
            int frames = run_frames(&pad, mode, &error);
            consoleInit(NULL);
            if (frames >= 0)
                printf("Submitted %d frames to deko3d.\n", frames);
            if (error)
                printf("%s\n", error);
            printf("ZR: 3D. ZL: language. Y: loading. X: texture. A: clear. -: controller. +: exit.\n");
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    if (R_SUCCEEDED(romfs))
        romfsExit();
    return 0;
}

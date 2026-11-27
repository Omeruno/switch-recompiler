#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __SWITCH__
#include <switch.h>
#include "freakout_pad_nx.h"
#include "gpu.h"
#endif

typedef bool (*FrameHandler)(Game *, void *);

static int compare_frame_times(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

uint64_t native_now_ns(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick());
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + now.tv_nsec;
#endif
}

static bool run(Game *g, const char *assets, FILE *log, unsigned limit,
                FrameHandler on_frame, FrameHandler poll, void *data, unsigned raster_divisor, bool use_gpu)
{
    if (!game_init(g, assets, log))
        return false;
    g->raster_divisor = raster_divisor;
    g->gpu_requested = use_gpu;
    unsigned progress = 0;
    uint64_t sample_start = native_now_ns(), sample_render = 0, last_frame = sample_start;
    uint64_t frame_ns[30];
    unsigned over_budget = 0;
    while (!limit || g->presents < limit) {
        if (poll && !(progress % 256) && !poll(g, data)) {
            g->stop = "stopped by user";
            break;
        }
        unsigned before = g->presents;
        if (!game_step(g))
            break;
        if (g->presents != before) {
            progress = 0;
            audio_frame(g);
            if (on_frame && !on_frame(g, data)) {
                if (!g->stop)
                    g->stop = "stopped by user";
                break;
            }
            uint64_t now = native_now_ns();
            unsigned frame = (g->presents - 1) % 30;
            frame_ns[frame] = now - last_frame;
            over_budget += frame_ns[frame] > 33333334;
            last_frame = now;
            if (frame == 29) {
                qsort(frame_ns, 30, sizeof frame_ns[0], compare_frame_times);
                unsigned width = g->frame_width, height = g->frame_height;
#ifdef __SWITCH__
                width = gpu_output_width(g);
                height = gpu_output_height(g);
#endif
                fprintf(log, "Performance at Present #%u: %.1f ms/frame, %.1f ms CPU %s, %ux%u, "
                        "p95 %.1f ms, max %.1f ms, %u/30 above 33.3 ms\n",
                        g->presents, (now - sample_start) / 30000000.0,
                        (g->render_ns - sample_render) / 30000000.0, g->gpu ? "render setup" : "raster",
                        width, height, frame_ns[28] / 1e6, frame_ns[29] / 1e6, over_budget);
                fflush(log);
                sample_start = now;
                sample_render = g->render_ns;
                over_budget = 0;
            }
        } else if (++progress >= 2000000) {
            g->stop = "no Present within the dispatch budget";
            break;
        }
    }
    bool accepted = !g->stop && g->presented_draws && g->winmain && g->pixels;
    fprintf(log, "%u presents, %u imports, %llu bytes read, %u archive reads, %u texture uploads, %u draws, status: %s\n",
            g->presents, g->calls, (unsigned long long)g->file_bytes,
            g->archive_reads, g->uploads, g->presented_draws,
            accepted ? "first game-rendered frame" : g->stop ? g->stop : "no rendered frame");
    return accepted;
}

#ifdef __SWITCH__
#define DEMO_PATH "sdmc:/switch/FreakOutNative/demo"
#define LOG_PATH "sdmc:/switch/FreakOutNative/boot.log"

typedef struct {
    PadState pad;
    Framebuffer fb;
    bool framebuffer_open, console_open;
} Output;

static bool poll_switch(Game *game, void *data)
{
    Output *out = data;
    if (!appletMainLoop())
        return false;
    padUpdate(&out->pad);
    FoXboxPad xbox = fo_nx_pad(&out->pad);
    if ((xbox.buttons & (FO_XBOX_BACK | FO_XBOX_START)) == (FO_XBOX_BACK | FO_XBOX_START))
        return false;
    uint8_t keys[256];
    fo_pad_keyboard(&xbox, keys);
    game_keys(game, keys);
    return true;
}

static bool present_switch(Game *game, void *data)
{
    Output *out = data;
    if (!game->presented_draws)
        return true;
    if (out->console_open) {
        consoleExit(NULL);
        out->console_open = false;
    }
    if (game->gpu) {
        if (!gpu_present(game)) {
            game->stop = "deko3d could not present the live frame";
            return false;
        }
        if (game->gpu)
            return poll_switch(game, out);
    }
    if (!out->framebuffer_open) {
        if (R_FAILED(framebufferCreate(&out->fb, nwindowGetDefault(), game->frame_width, game->frame_height,
                                       PIXEL_FORMAT_RGBA_8888, 2))) {
            game->stop = "could not open the CPU framebuffer";
            return false;
        }
        if (R_FAILED(framebufferMakeLinear(&out->fb))) {
            framebufferClose(&out->fb);
            game->stop = "could not map the CPU framebuffer";
            return false;
        }
        out->framebuffer_open = true;
    }
    u32 stride;
    uint8_t *pixels = framebufferBegin(&out->fb, &stride);
    for (unsigned y = 0; y < game->frame_height; ++y)
        memcpy(pixels + y * stride, game->pixels + y * game->frame_width, game->frame_width * 4);
    framebufferEnd(&out->fb);
    return poll_switch(game, out);
}

int main(void)
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    Output out = {.console_open = true};
    padInitializeDefault(&out.pad);
    padUpdate(&out.pad);
    bool use_gpu = !(padGetButtons(&out.pad) & HidNpadButton_L);
    puts("FreakOut Extreme Freeride - ARM64");
    puts("Starting the recompiled Windows demo.");
    if (use_gpu)
        puts("Starting the deko3d 720p renderer.");
    consoleUpdate(NULL);
    FILE *log = fopen(LOG_PATH, "w");
    Game *game = calloc(1, sizeof *game);
    if (game) {
        if (log)
            fprintf(log, "FreakOut 1.1.0 startup renderer: %s\n", use_gpu ? "deko3d" : "CPU");
        run(game, DEMO_PATH, log ? log : stdout, 0, present_switch, poll_switch, &out, 2, use_gpu);
        gpu_close(game);
        game_close(game);
    }
    if (log)
        fclose(log);
    if (out.framebuffer_open)
        framebufferClose(&out.fb);
    if (!out.console_open)
        consoleInit(NULL);
    printf("FreakOut stopped at Present #%u.\n", game ? game->presents : 0);
    if (game && game->stop)
        printf("%s\n", game->stop);
    free(game);
    puts("Details: /switch/FreakOutNative/boot.log");
    puts("Press + to exit.");
    while (appletMainLoop()) {
        padUpdate(&out.pad);
        if (padGetButtonsDown(&out.pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
#else
static bool save_ppm(const Game *game, const char *path)
{
    FILE *image = fopen(path, "wb");
    if (!image)
        return false;
    fprintf(image, "P6\n%u %u\n255\n", game->frame_width, game->frame_height);
    for (unsigned i = 0; i < game->frame_width * game->frame_height; ++i) {
        uint32_t color = game->pixels[i];
        unsigned char rgb[] = {color, color >> 8, color >> 16};
        fwrite(rgb, 1, sizeof rgb, image);
    }
    bool saved = !ferror(image);
    return fclose(image) == 0 && saved;
}

typedef struct {
    FILE *fp;
    unsigned frame, scan;
    bool pressed, active;
    uint8_t keys[256];
} Script;

static void read_input(Script *script)
{
    if (!script->fp) {
        script->active = false;
        return;
    }
    unsigned pressed;
    script->active = fscanf(script->fp, "%u %x %u", &script->frame, &script->scan, &pressed) == 3 &&
                     script->scan < 256 && pressed <= 1;
    script->pressed = script->active && pressed;
}

static bool script_frame(Game *game, void *data)
{
    Script *script = data;
    while (script->active && script->frame <= game->presents) {
        script->keys[script->scan] = script->pressed ? 128 : 0;
        game_keys(game, script->keys);
        read_input(script);
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 5)
        return 1;
    unsigned frames = argc >= 4 ? strtoul(argv[3], NULL, 10) : 7;
    if (!frames)
        return 1;
    Game *game = calloc(1, sizeof *game);
    if (!game)
        return 1;
    Script script = {.fp = argc >= 5 ? fopen(argv[4], "r") : NULL};
    if (argc >= 5 && !script.fp) {
        free(game);
        return 1;
    }
    read_input(&script);
    const char *half_resolution = getenv("FREAKOUT_HALF_RESOLUTION");
    unsigned divisor = half_resolution && !strcmp(half_resolution, "1") ? 2 : 1;
    bool passed = run(game, argv[1], stdout, frames, script_frame, NULL, &script, divisor, false);
    if (passed && argc >= 3)
        passed = save_ppm(game, argv[2]);
    if (script.fp)
        fclose(script.fp);
    game_close(game);
    free(game);
    return passed ? 0 : 1;
}
#endif

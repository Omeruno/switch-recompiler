#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __SWITCH__
#include <switch.h>
#include "freakout_pad_nx.h"
#endif

typedef bool (*FrameHandler)(Game *, void *);

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
                FrameHandler on_frame, FrameHandler poll, void *data, unsigned raster_divisor)
{
    if (!game_init(g, assets, log))
        return false;
    g->raster_divisor = raster_divisor;
    unsigned progress = 0;
    uint64_t sample_start = native_now_ns(), sample_render = 0;
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
            if (on_frame && !on_frame(g, data)) {
                g->stop = "stopped by user";
                break;
            }
            if (!(g->presents % 30)) {
                uint64_t now = native_now_ns();
                fprintf(log, "Performance at Present #%u: %.1f ms/frame, %.1f ms CPU raster, %ux%u\n",
                        g->presents, (now - sample_start) / 30000000.0,
                        (g->render_ns - sample_render) / 30000000.0, g->frame_width, g->frame_height);
                sample_start = now;
                sample_render = g->render_ns;
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
    bool started;
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
    if (!out->started) {
        consoleExit(NULL);
        if (R_FAILED(framebufferCreate(&out->fb, nwindowGetDefault(), game->frame_width, game->frame_height,
                                       PIXEL_FORMAT_RGBA_8888, 2))) {
            consoleInit(NULL);
            return false;
        }
        if (R_FAILED(framebufferMakeLinear(&out->fb))) {
            framebufferClose(&out->fb);
            consoleInit(NULL);
            return false;
        }
        out->started = true;
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
    Output out = {0};
    padInitializeDefault(&out.pad);
    puts("FreakOut Extreme Freeride - ARM64");
    puts("Starting the recompiled Windows demo.");
    consoleUpdate(NULL);
    FILE *log = fopen(LOG_PATH, "w");
    Game *game = calloc(1, sizeof *game);
    if (game) {
        run(game, DEMO_PATH, log ? log : stdout, 0, present_switch, poll_switch, &out, 2);
        game_close(game);
    }
    if (log)
        fclose(log);
    if (out.started) {
        framebufferClose(&out.fb);
        consoleInit(NULL);
    }
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
    bool passed = run(game, argv[1], stdout, frames, script_frame, NULL, &script, divisor);
    if (passed && argc >= 3)
        passed = save_ppm(game, argv[2]);
    if (script.fp)
        fclose(script.fp);
    game_close(game);
    free(game);
    return passed ? 0 : 1;
}
#endif

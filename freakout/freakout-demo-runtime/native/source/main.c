#include "game.h"
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#include "freakout_pad_nx.h"
#endif

typedef struct { uint32_t width, height, draws; uint32_t *pixels; } FirstFrame;
typedef bool (*Poll)(Game *, void *);

static bool run(const char *assets, FILE *log, FirstFrame *frame, Poll poll, void *data)
{
    Game *game = calloc(1, sizeof *game);
    if (!game)
        return false;
    bool initialized = game_init(game, assets, log);
    if (initialized) {
        for (unsigned step = 0; step < 2000000 && !game->presented_draws; ++step) {
            if (poll && !(step % 256) && !poll(game, data)) {
                game->stop = "stopped by user";
                break;
            }
            if (!game_step(game))
                break;
        }
    }
    bool accepted = game->presented_draws && game->winmain && game->pixels;
    fprintf(log, "%u presents, %u imports, %llu bytes read, %u archive reads, %u texture uploads, %u draws, status: %s\n",
            game->presents, game->calls, (unsigned long long)game->file_bytes,
            game->archive_reads, game->uploads, game->presented_draws,
            accepted ? "first game-rendered frame" : game->stop ? game->stop : "step limit");
    if (accepted) {
        *frame = (FirstFrame){game->back.width, game->back.height, game->presented_draws, game->pixels};
        game->pixels = NULL;
    }
    game_close(game);
    free(game);
    return accepted;
}

#ifdef __SWITCH__
#define DEMO_PATH "sdmc:/switch/FreakOutNative/demo"
#define LOG_PATH "sdmc:/switch/FreakOutNative/boot.log"

static bool poll_switch(Game *game, void *data)
{
    PadState *pad = data;
    if (!appletMainLoop())
        return false;
    padUpdate(pad);
    FoXboxPad xbox = fo_nx_pad(pad);
    fo_pad_keyboard(&xbox, game->keys);
    return true;
}

static void view_frame(PadState *pad, FirstFrame frame)
{
    consoleExit(NULL);
    Framebuffer fb = {0};
    if (R_SUCCEEDED(framebufferCreate(&fb, nwindowGetDefault(), frame.width, frame.height,
                                      PIXEL_FORMAT_RGBA_8888, 2))) {
        if (R_SUCCEEDED(framebufferMakeLinear(&fb))) {
            while (appletMainLoop()) {
                padUpdate(pad);
                if (padGetButtonsDown(pad) & (HidNpadButton_B | HidNpadButton_Plus))
                    break;
                u32 stride;
                uint8_t *pixels = framebufferBegin(&fb, &stride);
                for (unsigned y = 0; y < frame.height; ++y)
                    memcpy(pixels + y * stride, frame.pixels + y * frame.width, frame.width * 4);
                framebufferEnd(&fb);
            }
        }
        framebufferClose(&fb);
    }
    consoleInit(NULL);
}

int main(void)
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    puts("FreakOut Extreme Freeride - ARM64 startup");
    puts("Starting the recompiled Windows demo.");
    consoleUpdate(NULL);
    FILE *log = fopen(LOG_PATH, "w");
    FirstFrame frame = {0};
    bool initialized = run(DEMO_PATH, log ? log : stdout, &frame, poll_switch, &pad);
    if (log)
        fclose(log);
    if (initialized)
        view_frame(&pad, frame);
    while (appletMainLoop()) {
        consoleClear();
        puts("FreakOut Extreme Freeride - ARM64 startup");
        if (initialized) {
            printf("First game-rendered frame: %ux%u, %u draws\n", frame.width, frame.height, frame.draws);
            puts("A: show the frame. B: return.");
        } else {
            puts("The demo stopped before drawing a frame.");
            if (!log)
                puts("Check the demo path and SD card access.");
        }
        if (log)
            puts("Details: /switch/FreakOutNative/boot.log");
        puts("Press + to exit.");
        consoleUpdate(NULL);
        do {
            padUpdate(&pad);
            u64 keys = padGetButtonsDown(&pad);
            if (keys & HidNpadButton_Plus)
                goto done;
            if (initialized && keys & HidNpadButton_A) {
                view_frame(&pad, frame);
                break;
            }
            svcSleepThread(16000000);
        } while (appletMainLoop());
    }
done:
    free(frame.pixels);
    consoleExit(NULL);
    return 0;
}
#else
int main(int argc, char **argv)
{
    FirstFrame frame = {0};
    if (argc < 2 || argc > 3 || !run(argv[1], stdout, &frame, NULL, NULL))
        return 1;
    if (argc == 3) {
        FILE *image = fopen(argv[2], "wb");
        if (!image) {
            free(frame.pixels);
            return 1;
        }
        fprintf(image, "P6\n%u %u\n255\n", frame.width, frame.height);
        for (unsigned i = 0; i < frame.width * frame.height; ++i) {
            uint32_t color = frame.pixels[i];
            unsigned char rgb[] = {color, color >> 8, color >> 16};
            fwrite(rgb, 1, sizeof rgb, image);
        }
        bool saved = !ferror(image);
        if (fclose(image) || !saved) {
            free(frame.pixels);
            return 1;
        }
    }
    free(frame.pixels);
    return 0;
}
#endif

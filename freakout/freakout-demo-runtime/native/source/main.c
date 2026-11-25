#include "game.h"
#include <stdlib.h>
#ifdef __SWITCH__
#include <switch.h>
#include "freakout_pad_nx.h"
#endif

typedef struct { uint32_t width, height, argb; } FirstFrame;
typedef bool (*Poll)(Game *, void *);

static bool run(const char *assets, FILE *log, FirstFrame *frame, Poll poll, void *data)
{
    Game *game = calloc(1, sizeof *game);
    if (!game)
        return false;
    bool initialized = game_init(game, assets, log);
    if (initialized) {
        for (unsigned step = 0; step < 1000000 && game->uploads < 3; ++step) {
            if (poll && !(step % 256) && !poll(game, data)) {
                game->stop = "stopped by user";
                break;
            }
            if (!game_step(game))
                break;
        }
    }
    bool accepted = game->presents == 6 && game->winmain && game->clears &&
                    game->archive_reads >= 6 && game->uploads == 3;
    fprintf(log, "%u presents, %u imports, %llu bytes read, %u archive reads, %u texture uploads, status: %s\n",
            game->presents, game->calls, (unsigned long long)game->file_bytes,
            game->archive_reads, game->uploads,
            accepted ? "game archive and texture checkpoint" : game->stop ? game->stop : "step limit");
    if (accepted)
        *frame = (FirstFrame){game->back.width, game->back.height, game->clear};
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
            uint32_t color = RGBA8_MAXALPHA(frame.argb >> 16, frame.argb >> 8, frame.argb);
            while (appletMainLoop()) {
                padUpdate(pad);
                if (padGetButtonsDown(pad) & (HidNpadButton_B | HidNpadButton_Plus))
                    break;
                u32 stride;
                uint8_t *pixels = framebufferBegin(&fb, &stride);
                for (unsigned y = 0; y < frame.height; ++y) {
                    uint32_t *row = (uint32_t *)(pixels + y * stride);
                    for (unsigned x = 0; x < frame.width; ++x)
                        row[x] = color;
                }
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
    while (appletMainLoop()) {
        consoleClear();
        puts("FreakOut Extreme Freeride - ARM64 startup");
        if (initialized) {
            printf("First live Present: %ux%u ARGB %08x\n", frame.width, frame.height, frame.argb);
            puts("Read Data2.bnd and uploaded three original DXT1 mips.\nA: show the startup frame. B: return.");
        } else {
            puts("The demo stopped before the texture checkpoint.");
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
    consoleExit(NULL);
    return 0;
}
#else
int main(int argc, char **argv)
{
    FirstFrame frame;
    return argc == 2 && run(argv[1], stdout, &frame, NULL, NULL) ? 0 : 1;
}
#endif

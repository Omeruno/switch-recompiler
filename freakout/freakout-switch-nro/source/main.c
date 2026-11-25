#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diagnostic.h"

#ifndef FREAKOUT_DEMO_PATH
#define FREAKOUT_DEMO_PATH "sdmc:/switch/FreakOutDemo/FreakOut.exe"
#endif

enum { DEMO_SIZE = 3325952, SAMPLE_COUNT = 10000 };

static const uint8_t demo_sha256[] = {
    0x69, 0x83, 0x0e, 0x27, 0x6d, 0xce, 0xeb, 0x10,
    0x17, 0xbe, 0x49, 0xa6, 0x19, 0x3c, 0x6c, 0xfa,
    0xa0, 0x90, 0x5c, 0x63, 0x97, 0xb8, 0xaa, 0x9c,
    0x29, 0x07, 0xe8, 0x70, 0x7f, 0x4a, 0xdc, 0xe9
};

static int load_demo(uint8_t *guest)
{
    const char *path = FREAKOUT_DEMO_PATH;
    FILE *file = fopen(path, "rb");
    if (!file) {
        printf("Missing %s\n", path);
        return 0;
    }

    uint8_t *data = malloc(DEMO_SIZE);
    uint8_t digest[sizeof(demo_sha256)];
    int success = 0;
    if (!data) {
        printf("Out of memory reading demo.\n");
    } else if (fread(data, 1, DEMO_SIZE, file) != DEMO_SIZE ||
               fgetc(file) != EOF || ferror(file)) {
        printf("Unexpected executable size.\n");
    } else {
        sha256CalculateHash(digest, data, DEMO_SIZE);
        if (memcmp(digest, demo_sha256, sizeof digest) != 0)
            printf("Expected the original Windows demo EXE.\n");
        else if (!map_demo_image(guest, data, DEMO_SIZE))
            printf("Invalid PE image layout.\n");
        else
            success = 1;
    }
    free(data);
    fclose(file);
    return success;
}

static uint32_t next_input(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static uint64_t reference(uint32_t a, uint32_t b)
{
    uint32_t difference = a - b;
    uint32_t sign = difference >> 31;
    uint32_t result = (difference >> 4) | (sign ? 0xF0000000u : 0);
    uint32_t parity = __builtin_parity(result & 0xFFu) == 0;
    uint32_t flags = (sign << 7) | ((result == 0) << 6) |
                     (parity << 2) | ((difference >> 3) & 1);
    return ((uint64_t)flags << 32) | result;
}

static void run_diagnostics(void)
{
    uint8_t *guest = calloc(1, GUEST_SIZE);
    if (!guest) {
        printf("Out of memory creating guest image.\n");
        return;
    }
    if (!load_demo(guest)) {
        free(guest);
        return;
    }
    printf("Demo SHA-256 and PE mapping: PASS\n");

    check_startup(guest);
    static const uint32_t expected_calls[] = {
        0x100000, 0x5AF5B8, 0, 0x8FF64, 0x8FFFC, 0x8FFEC,
        0x100010, 0x5AF615, 2, 0x8FF64, 0x8FFFC, 0x8FFEC,
    };
    int start_ok = memcmp(guest + STARTUP_RESULT, expected_calls, sizeof expected_calls) == 0;
    printf("Startup to CRT boundary: %s\n", start_ok ? "PASS" : "FAIL");

    uint32_t state = 304540;
    unsigned matched = 0;
    for (unsigned index = 0; index < SAMPLE_COUNT; index++) {
        uint32_t a = next_input(&state);
        uint32_t b = next_input(&state);
        matched += check_leaf(guest, a, b) == reference(a, b);
    }
    printf("Recompiled function: %u / %u passed\n", matched, SAMPLE_COUNT);
    free(guest);
}

int main(void)
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("FreakOut demo: ARM64 diagnostics\n\n");
    run_diagnostics();
    printf("\nPress + to exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}

#include "probe.h"
#include <stdio.h>
#include <switch.h>
#include <sys/stat.h>
static FILE *open_report(void) {
    mkdir("sdmc:/switch", 0700);
    mkdir("sdmc:/switch/switch-browser", 0700);
    return fopen("sdmc:/switch/switch-browser/engine-report.txt", "w");
}
static int reserve_probe(FILE *report) {
    virtmemLock();
    void *address = virtmemFindAslr((size_t)1 << 32, 4096);
    VirtmemReservation *reservation =
        address ? virtmemAddReservation(address, (size_t)1 << 32) : NULL;
    int ok = reservation != NULL;
    if (reservation)
        virtmemRemoveReservation(reservation);
    virtmemUnlock();
    printf("%s reserve_4GiB_virtual_only (no physical memory committed)\n", ok ? "PASS" : "FAIL");
    if (report)
        fprintf(report, "%s reserve_4GiB_virtual_only; no commit/growth test\n",
                ok ? "PASS" : "FAIL");
    return ok;
}
static void run(void) {
    consoleClear();
    FILE *report = open_report();
    printf("Switch Engine Probe / interpreter compatibility candidate\n\n");
    engine_probe_run(report);
    reserve_probe(report);
    if (report)
        fclose(report);
    printf("\nNo WebGPU, game import bridge or game execution in this probe.\n");
    printf("A: repeat | X: load-only SD client.wasm | +: exit\n");
    printf("Report: /switch/switch-browser/engine-report.txt\n");
}
int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    run();
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus)
            break;
        if (down & HidNpadButton_A)
            run();
        if (down & HidNpadButton_X) {
            FILE *report = fopen("sdmc:/switch/switch-browser/engine-report.txt", "a");
            engine_probe_load_only("sdmc:/switch/switch-browser/engine/client.wasm", report);
            if (report)
                fclose(report);
        }
        consoleUpdate(NULL);
        svcSleepThread(16000000);
    }
    consoleExit(NULL);
    return 0;
}

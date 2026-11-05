#include "memory_port.h"
#include "probe.h"
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *open_report(void) {
    mkdir("sdmc:/switch", 0700);
    mkdir("sdmc:/switch/switch-browser", 0700);
    // Preserve the last pre-crash checkpoint across application launches.
    return fopen("sdmc:/switch/switch-browser/engine-report.txt", "a");
}
static void menu(unsigned index) {
    printf("\nSwitch Engine Probe 0.2.0 - staged diagnostics\n");
    printf("No tests run automatically.\n");
    printf("Selected: %u/%u %s\n", index + 1, engine_probe_case_count(),
           engine_probe_case_name(index));
    printf("Up/Down: select | A: run selected | Y: all tests\n");
    printf("X: load-only client.wasm | -: 4 GiB VA-only probe | +: exit\n");
    printf("Report: /switch/switch-browser/engine-report.txt\n");
    consoleUpdate(NULL);
}
static void reserve_probe(FILE *report) {
    engine_probe_checkpoint(report, "va.find", "4 GiB address space only");
    EngineMemory m;
    engine_memory_init(&m, engine_memory_backend(), ENGINE_MEMORY_COMMIT_LIMIT, 0);
    void *address = engine_memory_reserve(&m, ENGINE_MEMORY_MAX_RESERVE);
    int ok = address && engine_memory_release(&m, address);
    printf("%s reserve_4GiB_virtual_only (no physical allocation)\n", ok ? "PASS" : "FAIL");
    engine_probe_checkpoint(report, "va.finished", ok ? "PASS" : "FAIL");
}
int main(void) {
    consoleInit(NULL);
    printf("BOOT console ready\n");
    consoleUpdate(NULL);
    FILE *report = open_report();
    engine_probe_checkpoint(report, "boot.console_ready", "0.2.0");
    Thread *self = threadGetSelf();
    if (report) {
        fprintf(report, "APPLET type=%u main_stack_bytes=%zu\n", (unsigned)appletGetAppletType(),
                self ? self->stack_sz : 0);
        fflush(report);
        fsync(fileno(report));
    }
    if (report)
        fclose(report);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    unsigned selected = 0;
    for (unsigned i = 0; i < engine_probe_case_count(); ++i)
        if (!strcmp(engine_probe_case_name(i), "client_memory_growth"))
            selected = i;
    menu(selected);
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus)
            break;
        if (down & HidNpadButton_Up) {
            selected = (selected + engine_probe_case_count() - 1) % engine_probe_case_count();
            menu(selected);
        }
        if (down & HidNpadButton_Down) {
            selected = (selected + 1) % engine_probe_case_count();
            menu(selected);
        }
        if (down & (HidNpadButton_A | HidNpadButton_Y | HidNpadButton_X | HidNpadButton_Minus)) {
            report = open_report();
            if (down & HidNpadButton_A) {
                engine_probe_run_case(selected, report);
                if (selected + 1 < engine_probe_case_count())
                    ++selected;
            } else if (down & HidNpadButton_Y)
                engine_probe_run(report);
            else if (down & HidNpadButton_X)
                engine_probe_load_only("sdmc:/switch/switch-browser/engine/client.wasm", report);
            else
                reserve_probe(report);
            if (report)
                fclose(report);
            menu(selected);
        }
        consoleUpdate(NULL);
        svcSleepThread(16000000);
    }
    consoleExit(NULL);
    return 0;
}

#pragma once
#include <cstdio>
#include <string>
#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace visual {
inline void diagnostic(const char *stage, const std::string &detail) {
    fprintf(stderr, "VISUAL %s %s\n", stage, detail.c_str());
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0700);
    mkdir("sdmc:/switch/switch-visual-browser", 0700);
    FILE *f = fopen("sdmc:/switch/switch-visual-browser/visual-report.txt", "a");
    if (f) {
        fprintf(f, "VISUAL %s %s\n", stage, detail.c_str());
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
#endif
}
inline bool write_core_report(const std::string &report) {
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0700);
    mkdir("sdmc:/switch/switch-visual-browser", 0700);
    FILE *f = fopen("sdmc:/switch/switch-visual-browser/core-report.txt", "w");
    if (!f)
        return false;
    bool ok = fwrite(report.data(), 1, report.size(), f) == report.size();
    if (fflush(f) != 0 || fsync(fileno(f)) != 0)
        ok = false;
    if (fclose(f) != 0)
        ok = false;
    return ok;
#else
    (void)report;
    return false;
#endif
}
inline void fatal_screen(const std::string &detail) {
    diagnostic("fatal", detail);
#ifdef __SWITCH__
    consoleInit(nullptr);
    printf("Switch Visual Browser\n\n%s\n\nReport: "
           "/switch/switch-visual-browser/visual-report.txt\nPress + to exit.\n",
           detail.c_str());
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(nullptr);
        svcSleepThread(16000000);
    }
    consoleExit(nullptr);
#endif
}
} // namespace visual

// winrun.cpp — запуск Windows-программы на хосте через наш рантайм.
//
//   winrun [-v] [-L каталог] [-f кадров] [-c кадр:x:y]... [-d снимок.ppm]
//          программа.exe [аргументы...]
//
// Для оконных программ: -f — сколько кадров цикла сообщений прокрутить до
// выхода, -c — клик левой кнопкой в окно (виртуальный экран 640x360) на
// заданном кадре, -d — сохранить экранный DC в PPM при выходе.
//
// Загружает EXE и DLL (rt::Process), инициализирует модули и исполняет
// главный поток интерпретатором; консольный вывод программы идёт в
// stdout. Код возврата — код выхода программы (или 0xC0000005 и т.п.
// при необработанном исключении). Для хостовых тестов и отладки.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "source/cpu/interpreter.h"
#include "source/runtime/process.h"
#include "source/win32/gdi.h"
#include "source/win32/host_functions.h"
#include "source/win32/message_loop.h"
#include "source/win32/window_manager.h"

namespace {

struct Click { int frame, x, y; };
int g_frameLimit = -1, g_frame = 0;
std::vector<Click> g_clicks;

win32::HostFrameResult PumpFrame() {
    win32::HostFrameResult r;
    ++g_frame;
    uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
    for (const Click& c : g_clicks) {
        if (c.frame != g_frame) continue;
        uint32_t lp = (static_cast<uint32_t>(c.y) << 16) | static_cast<uint32_t>(c.x);
        win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONDOWN, 1, lp);
        win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONUP, 0, lp);
    }
    if (g_frame > g_frameLimit) r.shouldQuit = true;
    return r;
}

void DumpScreen(const char* path) {
    uint32_t dc = gdi::g_gdi.GetScreenDCHandle();
    const gdi::Bitmap* bmp = dc ? gdi::g_gdi.GetBitmapForDC(dc) : nullptr;
    if (!bmp || bmp->width <= 0) { std::fprintf(stderr, "winrun: экранного DC нет\n"); return; }
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", bmp->width, bmp->height);
    for (uint32_t px : bmp->pixels) { std::fputc((px >> 16) & 0xFF, f); std::fputc((px >> 8) & 0xFF, f); std::fputc(px & 0xFF, f); }
    std::fclose(f);
}

} // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::string> libDirs;
    const char* dumpPath = nullptr;
    int i = 1;
    for (; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0) verbose = true;
        else if (std::strcmp(argv[i], "-L") == 0 && i + 1 < argc) libDirs.push_back(argv[++i]);
        else if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) g_frameLimit = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) dumpPath = argv[++i];
        else if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            Click c{};
            if (std::sscanf(argv[++i], "%d:%d:%d", &c.frame, &c.x, &c.y) == 3) g_clicks.push_back(c);
        } else break;
    }
    if (g_frameLimit >= 0) win32::g_pumpHostFrame = &PumpFrame;
    if (i >= argc) {
        std::fprintf(stderr, "usage: %s [-v] [-L dir] program.exe [args...]\n", argv[0]);
        return 2;
    }
    std::string exe = argv[i];
    std::string cmd = exe;
    for (int k = i + 1; k < argc; ++k) { cmd += " "; cmd += argv[k]; }

    cpu::g_consoleAvailable = verbose;
    cpu::PagedGuestMemory mem;
    rt::Process proc(mem);
    rt::g_process = &proc;
    for (const auto& d : libDirs) proc.AddSearchPath(d);
    proc.SetCommandLine(cmd);

    std::string err;
    if (!proc.LoadMainExe(exe, err)) { std::fprintf(stderr, "winrun: %s\n", err.c_str()); return 3; }
    cpu::CpuState c;
    if (!proc.PrepareMainThread(c, err)) { std::fprintf(stderr, "winrun: %s\n", err.c_str()); return 3; }
    // Консольный вывод гостя нужен всегда, даже без -v
    cpu::g_consoleAvailable = true;
    bool quiet = !verbose;
    if (quiet) {
        // диагностика рантайма только с -v; вывод программы — всегда
        cpu::g_consoleAvailable = false;
        win32::g_consoleWriteImpl = [](const char* d, size_t n, bool e) {
            std::fwrite(d, 1, n, e ? stderr : stdout);
            std::fflush(e ? stderr : stdout);
        };
    }
    if (!proc.InitializeModules(c, err)) { std::fprintf(stderr, "winrun: %s\n", err.c_str()); return 3; }

    cpu::Interpreter interp(mem, proc.TebAddress(), proc.TebAddress());
    interp.lenientDivide = false;
    for (uint64_t steps = 0;; ++steps) {
        win32::HostCallOutcome out;
        if (win32::TryHandleHostCall(c, mem, out)) {
            if (out.requestExit) {
                if (dumpPath) DumpScreen(dumpPath);
                return out.exitCode;
            }
            continue;
        }
        cpu::StepResult r = interp.Step(c);
        if (r == cpu::StepResult::Continue) continue;
        if (r == cpu::StepResult::Halted) continue;
        if (dumpPath) DumpScreen(dumpPath);
        const cpu::CpuException& e = interp.LastException();
        std::fprintf(stderr, "winrun: необработанное исключение 0x%08x на 0x%08x (адрес 0x%08x)\n", e.code, e.eip, e.faultAddress);
        rt::Module* m = proc.FindByAddress(e.eip);
        if (m) std::fprintf(stderr, "        в модуле %s +0x%x\n", m->name.c_str(), e.eip - m->base);
        return static_cast<int>(e.code);
    }
}

// winrun.cpp — запуск Windows-программы на хосте через наш рантайм.
//
//   winrun [-v] [-L каталог] программа.exe [аргументы...]
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
#include "source/win32/host_functions.h"

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::string> libDirs;
    int i = 1;
    for (; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0) verbose = true;
        else if (std::strcmp(argv[i], "-L") == 0 && i + 1 < argc) libDirs.push_back(argv[++i]);
        else break;
    }
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
            if (out.requestExit) return out.exitCode;
            continue;
        }
        cpu::StepResult r = interp.Step(c);
        if (r == cpu::StepResult::Continue) continue;
        if (r == cpu::StepResult::Halted) continue;
        const cpu::CpuException& e = interp.LastException();
        std::fprintf(stderr, "winrun: необработанное исключение 0x%08x на 0x%08x (адрес 0x%08x)\n", e.code, e.eip, e.faultAddress);
        rt::Module* m = proc.FindByAddress(e.eip);
        if (m) std::fprintf(stderr, "        в модуле %s +0x%x\n", m->name.c_str(), e.eip - m->base);
        return static_cast<int>(e.code);
    }
}

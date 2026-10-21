// winrun.cpp — запуск Windows-программы на хосте через наш рантайм.
//
//   winrun [-v] [-L каталог] [-f кадров] [-c кадр:x:y]... [-d снимок.ppm] [-s кадр снимок.ppm]
//          программа.exe [аргументы...]
//
// Для оконных программ: -f — сколько кадров цикла сообщений прокрутить до
// выхода, -c — клик левой кнопкой в окно (виртуальный экран 640x360) на
// заданном кадре, -d — сохранить экран в PPM при выходе, -s — на кадре.
// --new-ui/--legacy-ui — выбрать оконную подсистему явно.
//
// Загружает EXE и DLL (rt::Process), инициализирует модули и исполняет
// главный поток интерпретатором; консольный вывод программы идёт в
// stdout. Код возврата — код выхода программы (или 0xC0000005 и т.п.
// при необработанном исключении). Для хостовых тестов и отладки.

#include "source/win32/seh_dispatch.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>

#include "source/cpu/interpreter.h"
#include "source/runtime/process.h"
#include "source/win32/gdi.h"
#include "source/win32/host_functions.h"
#include "source/win32/message_loop.h"
#include "source/win32/window_manager.h"
#include "source/win32/user32.h"
#include "source/runtime/clock.h"
#include "source/ui/font.h"

namespace {

struct Click { int frame, x, y; };
int g_frameLimit = -1, g_frame = 0, g_snapFrame = -1;
const char* g_snapPath = nullptr;
void DumpScreen(const char* path);
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

// Новая оконная подсистема: сценарий ввода по «кадрам» ожидания
bool g_newUi = false;
bool UiWait(int timeoutMs, std::vector<win32::user::InputEvent>& out) {
    using win32::user::InputEvent;
    ++g_frame;
    if (g_frame == g_snapFrame && g_snapPath) DumpScreen(g_snapPath);
    for (const Click& c : g_clicks) {
        if (c.frame != g_frame) continue;
        int x = c.x, y = c.y;
        // CI-only semantic click: -c frame:-control_id:row_index
        // Compute the actual client origin instead of guessing titlebar/border sizes.
        if (x <= -1000) {
            uint32_t wanted = static_cast<uint32_t>(-x);
            bool found = false;
            for (const auto& pair : win32::user::S().windows) {
                win32::user::Window* win = pair.second.get();
                if (!(win->style & win32::user::WS_CHILD) || win->id != wanted ||
                    !win32::user::IsVisibleChain(win)) continue;
                x = win32::user::ClientOriginX(win) + 8;
                y = win32::user::ClientOriginY(win) + 8 + c.y * 18;
                found = true;
                break;
            }
            if (!found) { std::fprintf(stderr, "winrun: scripted control %u not found\\n", wanted); continue; }
        }
        InputEvent mv; mv.type = InputEvent::MouseMove; mv.x = x; mv.y = y;
        InputEvent dn = mv; dn.type = InputEvent::MouseDown;
        InputEvent up = mv; up.type = InputEvent::MouseUp;
        out.push_back(mv); out.push_back(dn); out.push_back(up);
    }
    if (g_frameLimit >= 0 && g_frame > g_frameLimit) {
        InputEvent q; q.type = InputEvent::Quit;
        out.push_back(q);
        return true;
    }
    rt::Clock::Sleep(timeoutMs < 0 || timeoutMs > 16 ? 16 : static_cast<uint32_t>(timeoutMs));
    // без сценария и без таймеров ждать нечего
    return g_frameLimit >= 0 || timeoutMs >= 0;
}

void DumpSurface(const char* path, const ui::Surface& s) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", s.width, s.height);
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            uint32_t px = s.Get(x, y);
            std::fputc((px >> 16) & 0xFF, f); std::fputc((px >> 8) & 0xFF, f); std::fputc(px & 0xFF, f);
        }
    std::fclose(f);
}

void DumpScreen(const char* path) {
    if (g_newUi) { DumpSurface(path, win32::user::S().screen.s); return; }
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
    bool forceLegacy = false, forceNew = false, listMissing = false, probeThreads = false, probeX86Threads = false;
    int i = 1;
    for (; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0) verbose = true;
        else if (std::strcmp(argv[i], "-L") == 0 && i + 1 < argc) libDirs.push_back(argv[++i]);
        else if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) g_frameLimit = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) dumpPath = argv[++i];
        else if (std::strcmp(argv[i], "-s") == 0 && i + 2 < argc) { g_snapFrame = std::atoi(argv[++i]); g_snapPath = argv[++i]; }
        else if (std::strcmp(argv[i], "-t") == 0) win32::g_traceHostCalls = true;
        else if (std::strcmp(argv[i], "-m") == 0) listMissing = true;
        else if (std::strcmp(argv[i], "--probe-threads") == 0) probeThreads = true;
        else if (std::strcmp(argv[i], "--probe-x86-threads") == 0) probeX86Threads = true;
        else if (std::strcmp(argv[i], "--legacy-ui") == 0) forceLegacy = true;
        else if (std::strcmp(argv[i], "--new-ui") == 0) forceNew = true;
        else if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            Click c{};
            if (std::sscanf(argv[++i], "%d:%d:%d", &c.frame, &c.x, &c.y) == 3) g_clicks.push_back(c);
        } else break;
    }

    if (i >= argc) {
        std::fprintf(stderr, "usage: %s [-v] [-L dir] program.exe [args...]\n", argv[0]);
        return 2;
    }
    std::string exe = argv[i];
    std::string cmd = exe;
    for (int k = i + 1; k < argc; ++k) { cmd += " "; cmd += argv[k]; }

    // Оконная подсистема: Сапёр и Paint пока работают на прежнем HLE окон
    // (он подогнан под них), остальные программы — на новом USER32/GDI32.
    {
        std::string base = exe.substr(exe.find_last_of("/\\") == std::string::npos ? 0 : exe.find_last_of("/\\") + 1);
        for (char& ch : base) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        bool legacy = base == "winmine.exe" || base == "paint.exe";
        g_newUi = forceNew || (!legacy && !forceLegacy);
    }
    rt::Clock::SetFastForward(true);
    if (g_newUi) {
        std::string self = argv[0];
        size_t slash = self.find_last_of('/');
        std::string dir = slash == std::string::npos ? "." : self.substr(0, slash);
        ui::FontCache::SetDirectory(dir + "/../romfs/fonts");
        win32::user::g_platform.wait = &UiWait;
        win32::user::EnableUi(800, 600);
    } else if (g_frameLimit >= 0) {
        win32::g_pumpHostFrame = &PumpFrame;
    }

    cpu::g_consoleAvailable = verbose;
    cpu::PagedGuestMemory mem;
    rt::Process proc(mem);
    rt::g_process = &proc;
    for (const auto& d : libDirs) proc.AddSearchPath(d);
    proc.SetCommandLine(cmd);

    std::string err;
    if (!proc.LoadMainExe(exe, err)) { std::fprintf(stderr, "winrun: %s\n", err.c_str()); return 3; }
    if (listMissing) {
        // импорты из эмулируемых DLL, для которых нет реализации
        for (const auto& mp : proc.Modules()) {
            if (mp->hle || mp->path.empty()) continue;
            pe::PEParser p;
            if (p.LoadFromFile(mp->path) != pe::LoadResult::Ok) continue;
            for (const auto& imp : p.GetImports()) {
                rt::Module* dep = proc.FindByName(rt::Process::NormalizeName(imp.dllName));
                if (dep && !dep->hle) continue;
                for (const auto& f : imp.functions) {
                    int idx = f.name.empty() ? win32::FindHostFunctionByOrdinal(imp.dllName.c_str(), f.ordinal)
                                             : win32::FindHostFunction(imp.dllName.c_str(), f.name.c_str());
                    if (idx < 0) std::printf("%s: %s!%s\n", mp->name.c_str(), imp.dllName.c_str(), f.name.empty() ? std::to_string(f.ordinal).c_str() : f.name.c_str());
                }
            }
        }
        return 0;
    }
    cpu::CpuState& c = proc.MainThread().cpu;
    if (!proc.PrepareMainThread(c, err)) { std::fprintf(stderr, "winrun: %s\n", err.c_str()); return 3; }
    if (probeThreads) {
        const uint32_t start=proc.MainThread().cpu.eip;
        rt::GuestThread* a=proc.CreateGuestThread(start,0x12345678u,0x40000u,false,err);
        if (!a) { std::fprintf(stderr,"thread probe: %s\n",err.c_str()); return 4; }
        rt::GuestThread* b=proc.CreateGuestThread(start,0xABCDEF90u,0x40000u,true,err);
        if (!b) { std::fprintf(stderr,"thread probe: %s\n",err.c_str()); return 4; }
        const bool distinct =
            proc.Scheduler().Size()==3 && a!=b &&
            a->teb!=b->teb && a->teb!=proc.MainThread().teb &&
            b->teb!=proc.MainThread().teb &&
            a->tlsArray!=b->tlsArray &&
            a->tlsArray!=proc.MainThread().tlsArray &&
            b->tlsArray!=proc.MainThread().tlsArray &&
            (a->stackBase<=b->stackLimit || b->stackBase<=a->stackLimit) &&
            a->id!=b->id && a->id!=proc.MainThread().id &&
            mem.Read32(a->teb+0x00)==0xffffffffu &&
            mem.Read32(b->teb+0x00)==0xffffffffu &&
            mem.Read32(a->teb+0x18)==a->teb &&
            mem.Read32(b->teb+0x18)==b->teb &&
            mem.Read32(a->teb+0x24)==a->id &&
            mem.Read32(b->teb+0x24)==b->id &&
            mem.Read32(a->teb+0x2c)==a->tlsArray &&
            mem.Read32(b->teb+0x2c)==b->tlsArray &&
            win32::HostAddressToIndex(mem.Read32(a->cpu.ESP()))==
                win32::FindHostFunction("KERNEL32.dll","__SwitchRecompilerWorkerReturn") &&
            mem.Read32(a->cpu.ESP()+4)==0x12345678u &&
            mem.Read32(b->cpu.ESP()+4)==0xABCDEF90u &&
            a->cpu.eip==start && b->cpu.eip==start &&
            a->IsRunnable() && !b->IsRunnable() &&
            b->suspendCount==1;
        if (!distinct) {
            std::fprintf(stderr,"thread probe: wrong TEB/TLS/CPU stack isolation\n");
            return 4;
        }
        std::puts("PASSED: independent worker TEB TLS stack and CPU context");
        return 0;
    }
    if (probeX86Threads) {
        // Real guest IA-32 instructions: MOV EAX, FS:[24h]; INC EAX;
        // INC EAX; HLT. Each independently scheduled worker must read its
        // own TEB, not the main thread's interpreter FS base.
        constexpr uint32_t code=0x32000000u;
        if (!mem.Map(code,0x1000u,cpu::kProtRWX)) {
            std::fprintf(stderr,"guest x86 thread test code mapping failed\n");
            return 4;
        }
        const uint8_t machine[] = {
            0x64,0xA1,0x24,0,0,0, // mov eax, fs:[0x24]
            0x40,                 // inc eax
            0x40,                 // inc eax
            0xF4                  // hlt
        };
        for (unsigned n=0;n<sizeof(machine);++n)
            mem.Write8(code+n,machine[n]);
        rt::GuestThread* first=proc.CreateGuestThread(code,0,0x10000u,false,err);
        if (!first) { std::fprintf(stderr,"first x86 worker: %s\n",err.c_str()); return 4; }
        rt::GuestThread* second=proc.CreateGuestThread(code,0,0x10000u,false,err);
        if (!second) { std::fprintf(stderr,"second x86 worker: %s\n",err.c_str()); return 4; }
        // Main EXE has not started. Put main thread to sleep so only our
        // two x86 workers participate in the scheduler run.
        proc.MainThread().status=rt::GuestThreadStatus::Waiting;
        proc.MainThread().waitObject=0x5555u;
        uint32_t stepsFirst=0,stepsSecond=0;
        bool guestFailure=false;
        for (unsigned quantum=0;quantum<20 && (first->IsRunnable() || second->IsRunnable());++quantum) {
            auto result=proc.Scheduler().RunQuantum([&](rt::GuestThread& t) -> bool {
                if (proc.CurrentThreadId()!=t.id || proc.CurrentTebAddress()!=t.teb) {
                    guestFailure=true;
                    return false;
                }
                cpu::Interpreter interpreter(mem,t.teb,t.teb);
                cpu::StepResult step=interpreter.Step(t.cpu);
                if (&t==first) ++stepsFirst;
                if (&t==second) ++stepsSecond;
                if (step==cpu::StepResult::Halted) {
                    proc.Scheduler().Exit(t,t.cpu.Get(cpu::Reg32::EAX));
                    return false;
                }
                if (step!=cpu::StepResult::Continue) {
                    std::fprintf(stderr,"guest x86 thread %u failed at 0x%x\n",t.id,t.cpu.eip);
                    guestFailure=true;
                    proc.Scheduler().Exit(t,1);
                    return false;
                }
                return true;
            },2u);
            if (!result.threadId || guestFailure) break;
        }
        bool ok=!guestFailure &&
            first->status==rt::GuestThreadStatus::Exited &&
            second->status==rt::GuestThreadStatus::Exited &&
            first->exitCode==first->id+2 &&
            second->exitCode==second->id+2 &&
            stepsFirst==4 && stepsSecond==4 &&
            mem.Read32(first->teb+0x24)==first->id &&
            mem.Read32(second->teb+0x24)==second->id;
        if(!ok) {
            std::fprintf(stderr,
                "x86 workers FAILED steps=%u,%u status=%u,%u eax=%u,%u\n",
                stepsFirst,stepsSecond,unsigned(first->status),unsigned(second->status),
                first->exitCode,second->exitCode);
            return 4;
        }
        // A normal stdcall thread function can RET instead of calling
        // ExitThread explicitly. The HLE worker trampoline must consume
        // EAX as its exit code without triggering whole-process shutdown.
        const uint32_t returnCode=code+64u;
        const uint8_t returns[] = {0xB8,0x2A,0,0,0,0xC2,0x04,0}; // mov eax,42; ret 4
        for(unsigned n=0;n<sizeof(returns);++n)
            mem.Write8(returnCode+n,returns[n]);
        rt::GuestThread* returning=proc.CreateGuestThread(returnCode,0,0x10000u,false,err);
        if(!returning) { std::fprintf(stderr,"worker RET test: %s\n",err.c_str()); return 4; }
        bool trampolineCalled=false;
        for(unsigned n=0;n<12 && returning->IsRunnable();++n) {
            cpu::Interpreter workerInterpreter(mem,returning->teb,returning->teb);
            if (win32::HostAddressToIndex(returning->cpu.eip)>=0) {
                proc.Scheduler().SetCurrent(returning);
                win32::HostCallOutcome outcome{};
                if(!win32::TryHandleHostCall(returning->cpu,mem,outcome)) break;
                trampolineCalled=true;
                break;
            }
            if(workerInterpreter.Step(returning->cpu)!=cpu::StepResult::Continue)
                break;
        }
        if(!trampolineCalled || returning->status!=rt::GuestThreadStatus::Exited ||
           returning->exitCode!=42 || proc.MainThread().status==rt::GuestThreadStatus::Exited) {
            std::fprintf(stderr,"worker RET incorrectly exited process or lost code\n");
            return 4;
        }
        std::puts("PASSED: two real x86 guest threads, FS TEB and instruction quanta");
        return 0;
    }
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

    // Every running x86 CPU context is owned by its GuestThread. Keep the
    // original one-thread behavior, but rotate after a bounded instruction
    // quantum as soon as a guest CreateThread registers a worker.
    rt::GuestThread* running=nullptr;
    std::unique_ptr<cpu::Interpreter> runningInterpreter;
    unsigned quantumLeft=0;
    for (uint64_t steps=0;;++steps) {
        if (!running || !running->IsRunnable() || quantumLeft==0) {
            running=proc.Scheduler().Next();
            if(!running) {
                std::fprintf(stderr,"winrun: no runnable guest threads (all waiting/exited)\n");
                return 3;
            }
            quantumLeft=256;
            runningInterpreter.reset(new cpu::Interpreter(mem,running->teb,running->teb));
            runningInterpreter->lenientDivide=false;
        }
        --quantumLeft;
        cpu::CpuState& state=running->cpu;
        win32::HostCallOutcome out;
        if (win32::TryHandleHostCall(state,mem,out)) {
            if(out.requestExit) {
                if(dumpPath) DumpScreen(dumpPath);
                return out.exitCode;
            }
            continue;
        }
        cpu::StepResult r=runningInterpreter->Step(state);
        if(r==cpu::StepResult::Continue) continue;
        if(r==cpu::StepResult::Halted) {
            // HLT remains a no-op for compatibility with existing tests;
            // workers must use ExitThread or return through their trampoline.
            continue;
        }
        if(win32::seh::DispatchCpuFault(state,mem,running->teb,
                                        runningInterpreter->LastException()) ==
           win32::seh::DispatchResult::Handled) continue;
        if(dumpPath) DumpScreen(dumpPath);
        const cpu::CpuException& e=threadInterpreter.LastException();
        std::fprintf(stderr,"winrun: thread 0x%x unhandled exception 0x%08x at 0x%08x (address 0x%08x)\n",
                     running->id,e.code,e.eip,e.faultAddress);
        rt::Module* module=proc.FindByAddress(e.eip);
        if(module) std::fprintf(stderr,"        in %s +0x%x\n",module->name.c_str(),e.eip-module->base);
        return static_cast<int>(e.code);
    }
}

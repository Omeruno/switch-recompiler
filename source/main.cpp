// Include the most common headers from the C standard library
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <cstdarg>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <set>
#include <queue>
#include <cctype>
#include <chrono>
#include <ctime>

// Include the main libnx system header, for Switch development
#include <switch.h>
#include <sys/stat.h>
#ifndef SR_BUILD_REVISION
#define SR_BUILD_REVISION "unknown"
#endif

// Наш PE-парсер и x86-decoder
#include "loader/pe_parser.h"
#include "decoder/x86_decoder.h"
#include "cpu/cpu_state.h"
#include "cpu/guest_memory.h"
#include "cpu/interpreter.h"
#include "runtime/clock.h"
#include "runtime/process.h"
#include "runtime/guest_execution.h"
#include "runtime/guest_session.h"
#include "runtime/files.h"
#include "runtime/guest_execution_log.h"
#include "ui/presentation.h"
#include "ui/pointer_input.h"
#include "ui/launcher_loop.h"
#include "ui/output_lifetime.h"
#include "ui/executable_catalog.h"
#include "ui/desktop_shell.h"
#include "ui/opengl_presentation.h"
#include "ui/switch_gpu_output.h"
#include "win32/user32.h"
#include "win32/shell_session.h"
#include <new>
#include "win32/presentation_session.h"
#include "win32/host_functions.h"
#include "win32/seh_dispatch.h"
#include "win32/bitmap_font.h"
#include "win32/iat_patcher.h"
#include "win32/crt_globals.h"
#include "loader/resource_parser.h"
#include "win32/gdi.h"
#include "win32/toolbar_bitmap_data.h"
#include "win32/palette_data.h"

// Сигнал "выбрать инструмент" (эта сессия) — реально определён в
// host_functions.cpp В НАЧАЛЕ файла (до анонимного namespace, чтобы
// линковка была корректной — на этих граблях мы уже спотыкались раньше
// в этой же сессии с диагностическими счётчиками).
namespace win32 {
    extern uint32_t g_requestedToolId;
    extern uint32_t g_requestedForegroundColor;
    extern uint32_t g_requestedBackgroundColor;
}

// Флаг переключения в графический режим — объявлен здесь (а не рядом с
// остальным вводом ниже), чтобы RunInterpreter мог его проверить перед
// своими финальными диагностическими printf: если мы уже в графике
// (консоль закрыта), печатать туда больше нельзя.
static PadState g_pad;
static bool g_graphicsInitialized = false;
static bool g_framebufferReady = false;
static bool g_appletStopped = false;
static const char* g_launchStatus = "Program stopped. Choose a program to launch again.";
static bool ContinueApplet() {
    if(g_appletStopped)return false;
    if(appletMainLoop())return true;
    g_appletStopped=true;return false;
}
static ui::PointerInputGate g_legacyInputGate;

// Путь к выбранному исполняемому файлу — заполняется PickDesktopExecutable()
// в постоянном frontend (native shell или fallback picker) ДО гостевого сеанса; объявлена так рано
// специально, чтобы RunSelectedGuest (которая идёт по файлу раньше, чем сам
// пикер) могла её использовать.
// New USER/GDI is the default for every EXE; hold L for legacy compatibility.
static bool g_useNewUi = true;
static ui::DesktopShell* g_activeShell = nullptr; // worker-only until its join
namespace newui {
extern win32::user::ShellSession* g_shellSession;
bool Wait(int timeoutMs, std::vector<win32::user::InputEvent>& out);
void Present(const ui::Surface& s);
void BeginPresentation(const char* guestPath);
extern bool g_gpuActive;
void EndPresentation();
void InvalidateOutput();
bool InitializeOutput(bool gpuRequested);
bool DesktopOutput();
bool OutputHealthy();
bool DrawDesktop(const ui::Surface& s,int x,int y,bool cursor);
void Blit();
void CloseOutput();
bool TextInput(const std::u16string& initial, bool multiline, bool password, std::u16string& out);
}
static std::string g_selectedCommandLine;
static std::string g_selectedExePath; // set only after an actual picker selection

// НАЙДЕНО ОБРАТНОЙ СВЯЗЬЮ С РЕАЛЬНОГО ЖЕЛЕЗА: тулбар/палитра рисовались
// поверх Сапёра — оказалось, DrawToolbar/DrawPalette ошибочно
// использовали ОБЩИЙ признак "g_menuLoaded" (истинен для ЛЮБОГО
// MFC-приложения с настоящим меню, включая Сапёра — у него тоже
// честное меню Game/Help). Тулбар/палитра — специфичны именно для
// Paint, им нужен СВОЙ, более узкий признак.
static bool IsRunningPaint() {
    std::string lower = g_selectedExePath;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("paint") != std::string::npos;
}

// Безопасные обёртки: печатать/обновлять текстовую консоль имеет смысл
// ТОЛЬКО пока мы реально в текстовом режиме (g_graphicsInitialized==false).
// Вызов printf/consoleUpdate ПОСЛЕ переключения в графику — именно та
// причина краха, которую мы поймали через addr2line (падало внутри
// ConsoleSwRenderer_drawChar из libnx) — эти обёртки защищают от повторения.
static void SafePrint(const char* fmt, ...) {
    if (g_graphicsInitialized) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
static void SafeConsoleUpdate() {
    if (!g_graphicsInitialized) consoleUpdate(NULL);
}

// Настоящий источник времени для GetTickCount — не выдумка, реальные тики
// системы Switch, переведённые в миллисекунды (как это делает Windows).
static uint32_t RealGetTickCount()
{
    u64 ns = armTicksToNs(armGetSystemTick());
    return static_cast<uint32_t>(ns / 1000000ull);
}

static void PrintPEAnalysis(const char* path)
{
    pe::PEParser parser;
    pe::LoadResult result = parser.LoadFromFile(path);

    if (result != pe::LoadResult::Ok)
    {
        printf("[!] Failed to load %s: %s\n", path, pe::ToString(result));
        return;
    }

    printf("[+] PE loaded: %s\n", path);
    printf("    Entry Point RVA: 0x%x\n", parser.GetEntryPointRva());
    printf("    Image Base:      0x%x\n", parser.GetImageBase());
    printf("    Size Of Image:   0x%x\n\n", parser.GetSizeOfImage());

    printf("[+] Sections (%zu):\n", parser.GetSections().size());
    for (const auto& s : parser.GetSections())
    {
        printf("    %-8s VA=0x%-6x Size=0x%-6x\n",
               s.name.c_str(), s.virtualAddress, s.virtualSize);
    }

    printf("\n[+] Imports (%zu modules):\n", parser.GetImports().size());
    size_t totalFunctions = 0;
    for (const auto& mod : parser.GetImports())
    {
        printf("    %-14s -> %zu functions\n", mod.dllName.c_str(), mod.functions.size());
        totalFunctions += mod.functions.size();
    }
    printf("\n    Total imported functions: %zu\n", totalFunctions);
}

// Реально ДЕКОДИРУЕТ (не просто читает заголовки) байты .text секции нашим
// собственным x86-decoder'ом, идя по потоку исполнения от entry point —
// та же методика, что мы обкатали на ПК, но теперь прямо на консоли.
static void PrintDisassembly(const char* path, int maxLinesToShow)
{
    pe::PEParser parser;
    pe::LoadResult result = parser.LoadFromFile(path);
    if (result != pe::LoadResult::Ok)
    {
        printf("[!] Failed to load %s for disassembly\n", path);
        return;
    }

    uint32_t imageSize = parser.GetSizeOfImage();
    std::vector<uint8_t> image(imageSize, 0);
    parser.MapImageToMemory(image.data(), image.size());

    uint32_t imageBase = parser.GetImageBase();
    uint32_t entryVa = imageBase + parser.GetEntryPointRva();

    // Границы допустимой адресации внутри нашего образа (для проверки выхода
    // за пределы .text/вообще известной памяти — напр. переход в другую DLL)
    uint32_t imageLo = imageBase;
    uint32_t imageHi = imageBase + imageSize;

    std::set<uint32_t> visited;
    std::queue<uint32_t> worklist;
    worklist.push(entryVa);

    int totalDecoded = 0;
    int unknownCount = 0;
    int shown = 0;

    printf("\n[+] Disassembly from entry point 0x%x:\n", entryVa);

    while (!worklist.empty())
    {
        uint32_t addr = worklist.front();
        worklist.pop();
        if (visited.count(addr)) continue;
        if (addr < imageLo || addr >= imageHi) continue;

        uint32_t cursor = addr;
        while (cursor >= imageLo && cursor < imageHi && !visited.count(cursor))
        {
            visited.insert(cursor);
            uint32_t offsetInImage = cursor - imageBase;

            uint8_t safeBuf[16] = {0};
            uint32_t remaining = imageSize - offsetInImage;
            uint32_t toCopy = remaining < 16 ? remaining : 16;
            std::memcpy(safeBuf, image.data() + offsetInImage, toCopy);

            cpu::Instruction instr = cpu::Decode(safeBuf, cursor);
            totalDecoded++;

            if (shown < maxLinesToShow)
            {
                if (instr.mnemonic == cpu::Mnemonic::Unknown)
                    printf(" %06x: ??? (0x%02x)\n", cursor, instr.rawOpcodeForDebug);
                else
                    printf(" %06x: %s\n", cursor, cpu::ToString(instr.mnemonic));
                shown++;
            }

            if (instr.mnemonic == cpu::Mnemonic::Unknown)
            {
                unknownCount++;
                break;
            }
            if (instr.mnemonic == cpu::Mnemonic::Ret || instr.mnemonic == cpu::Mnemonic::Halt)
                break;
            if (instr.mnemonic == cpu::Mnemonic::Jmp)
            {
                if (!instr.indirect) worklist.push(instr.branchTarget);
                break;
            }
            if (instr.mnemonic == cpu::Mnemonic::Jcc)
            {
                worklist.push(instr.branchTarget);
                cursor += instr.length;
                continue;
            }
            if (instr.mnemonic == cpu::Mnemonic::Call)
            {
                if (!instr.indirect) worklist.push(instr.branchTarget);
                cursor += instr.length;
                continue;
            }
            cursor += instr.length;
        }
    }

    printf("\n[+] Decoded %d instructions total (%d shown above)\n", totalDecoded, shown);
    printf("    Unknown opcodes hit: %d\n", unknownCount);
    if (totalDecoded > 0)
    {
        double pct = 100.0 * (totalDecoded - unknownCount) / totalDecoded;
        printf("    Coverage on reached paths: %.1f%%\n", pct);
    }
}

// НАСТОЯЩЕЕ исполнение — не просто разбор байт, а реальный запуск через наш
// Interpreter, с перехватом вызовов Win32/CRT функций через IAT.
// Сохраняем адрес падения гостя: консоль недоступна после включения графики.
// Не пишем трассу каждого кадра на SD (это быстро убьёт карту и FPS).
static void SaveGuestStopReport(const char* reason, const char* exeName,
                                const cpu::CpuState& state, rt::Process& proc,
                                const cpu::CpuException* exception, bool initializationFailure=false, int steps=0)
{
    // Главный файл — корень SD: отдельный каталог создавать не требуется.
    FILE* log = fopen("sdmc:/switch-recompiler-crash.txt", "w");
    if (!log) return;
    fprintf(log, "switch-recompiler guest stop report\n");
    fprintf(log, "EXE: %s\n", exeName ? exeName : "?");
    fprintf(log,"Build: %s\n",SR_BUILD_REVISION);
    fprintf(log, "Reason: %s\n", reason ? reason : "?");
    fprintf(log,"Window layer: %s\nGuest exit code: 0x%08x\nSteps: %d\n",g_useNewUi?"new":"legacy",uint32_t(win32::g_guestExitCode),steps);
    fprintf(log, "EIP: 0x%08x\n", state.eip);
    fprintf(log, "ESP: 0x%08x\n", state.Get(cpu::Reg32::ESP));
    if (exception) {
        fprintf(log, "Exception: 0x%08x\n", exception->code);
        fprintf(log, "Fault address: 0x%08x\n", exception->faultAddress);
    }
    uint32_t where = exception ? exception->eip : state.eip;
    if(initializationFailure) {
        fprintf(log,"Phase: DLL/TLS initialization\n");
        for(const auto& module:proc.Modules()) {
            if(module->unloaded || !module->initializationExceptionCode)continue;
            fprintf(log,"Initializer: %s\n",module->name.c_str());
            fprintf(log,"Initialization exception: 0x%08x\n",module->initializationExceptionCode);
            where=module->initializationExceptionAddress;
            break;
        }
    }
    fprintf(log, "Fault EIP: 0x%08x\n", where);
    if (rt::Module* mod = proc.FindByAddress(where)) {
        fprintf(log, "Module: %s\n", mod->name.c_str());
        fprintf(log, "Module offset: 0x%08x\n", where - mod->base);
    }
    fclose(log);
}

static void ResetLegacyFrontend();
static win32::HostFrameResult PumpHostFrame();
static void RunInterpreter(const char* path, int maxLinesToShow, int maxSteps)
{
    rt::GuestExecutionLog executionLog("sdmc:/switch-recompiler-execution.txt",path,g_useNewUi?"new":"legacy",SR_BUILD_REVISION);
    rt::GuestSession guestSession;
    auto& mem=guestSession.Memory();auto& proc=guestSession.ProcessObject();
    win32::EnableHostLayer(win32::kUiLayer,g_useNewUi);
    struct FrontendCleanup {bool legacy;~FrontendCleanup(){if(legacy)ResetLegacyFrontend();}} frontendCleanup{!g_useNewUi};
    if(!g_useNewUi) {
        ResetLegacyFrontend();padUpdate(&g_pad);
        HidTouchScreenState touch{};
        const bool touching=hidGetTouchScreenStates(&touch,1) && touch.count>0;
        g_legacyInputGate.Reset(padGetButtons(&g_pad),touching);
    }
    int stackMarker=0;win32::g_threadStackBase=&stackMarker;
    win32::g_pumpHostFrame=&PumpHostFrame;
    win32::user::PresentationSession presentationScope(g_useNewUi,&newui::EndPresentation);
    // Процесс гостя: EXE и его DLL по своим адресам, TEB/PEB, стек, куча
    // (rt::Process — тот же загрузчик, что в хостовых тестах). MFC пока
    // эмулируется (HLE), даже если mfc42u.dll лежит рядом с EXE.
    if (g_useNewUi) {
        win32::user::g_platform.wait = &newui::Wait;
        win32::user::g_platform.present = &newui::Present;
        win32::user::g_platform.textInput = &newui::TextInput;
        win32::user::EnableUi(ui::kHandheldWidth,ui::kHandheldHeight);
        newui::BeginPresentation(path);
        win32::user::EnableWindowLayers(newui::g_gpuActive);
    }

    std::unique_ptr<win32::user::ShellSession> shellSession;
    struct ShellBinding {~ShellBinding(){newui::g_shellSession=nullptr;}} shellBinding;
    if(g_useNewUi && g_activeShell) {
        shellSession.reset(new(std::nothrow) win32::user::ShellSession(*g_activeShell));
        const std::string label=std::string(path).substr(std::string(path).find_last_of("/\\")+1);
        if(!shellSession || !shellSession->Attach(label)) {
            g_launchStatus="Could not allocate shared desktop. Choose a program to retry.";
            executionLog.Complete("Native shell initialization failed",8,0);return;
        }
        newui::g_shellSession=shellSession.get();
        // F4a composes native chrome in a bounded CPU image before GPU/software
        // output. Existing retained window layers remain available without it.
        win32::user::EnableWindowLayers(false);
    }

    // прежнему слою нужны заглушки MFC; новый грузит настоящую mfc42u.dll
    // (из каталога программы), если она есть
    if (!g_useNewUi) proc.ForceHle("mfc42u.dll");
    proc.SetCommandLine(g_selectedCommandLine.empty()?g_selectedExePath:g_selectedCommandLine);

    std::string err;
    if(!rt::FileSystem().ConfigurePersistent("sdmc:/switch-recompiler/drive_c","sdmc:/","romfs:/",err)) {
        g_launchStatus="Could not initialize persistent filesystem. Check SD access.";
        executionLog.Complete(err.c_str(),5,0);return;
    }
    cpu::CpuState& cpuState = proc.MainThread().cpu;
    rt::Module* exe = proc.LoadMainExe(path, err);
    if (!exe || !proc.PrepareMainThread(cpuState, err))
    {
        g_launchStatus="Could not load program. Choose another EXE or retry.";
        executionLog.Complete(err.c_str(),193,0);
        SaveGuestStopReport(err.c_str(),path,cpuState,proc,nullptr);
        SafePrint("[!] Failed to load %s: %s\n", path, err.c_str());
        return;
    }
    SafePrint("[+] %s loaded at 0x%x\n", path, exe->base);

    win32::g_getTickCountImpl = &RealGetTickCount;
    rt::GuestThread* runningThread=nullptr;
    SafePrint("\n[+] Executing from entry point 0x%x:\n",cpuState.eip);
    SafeConsoleUpdate();
    int shown=0, steps=0;
    const char* stopReason="max steps reached";
    cpu::CpuException savedException{};
    const cpu::CpuException* stopException=nullptr;
    rt::GuestExecution execution(proc,[&](rt::GuestThread& thread) {
        runningThread=&thread;
        auto& activeCpu=thread.cpu;
        if (++steps>maxSteps) return false;
        if (!activeCpu.eip) { stopReason="called unimplemented import (address 0)"; return false; }
        win32::HostCallOutcome outcome;
        if (win32::TryHandleHostCall(activeCpu,mem,outcome)) {
            if (shown++<maxLinesToShow) {
                SafePrint(" [host] eax=0x%x%s\n",outcome.returnValue,
                          outcome.requestExit ? " (exit requested)" : "");
                SafeConsoleUpdate();
            }
            if (outcome.requestExit) { stopReason="guest called exit()"; return false; }
            return true;
        }
        cpu::Interpreter interpreter(mem,thread.teb,thread.teb);
        cpu::g_lastKnownEip=activeCpu.eip;
        cpu::Instruction instruction;
        uint32_t previous=activeCpu.eip;
        auto result=interpreter.Step(activeCpu,&instruction);
        if (shown++<maxLinesToShow) {
            SafePrint(" %06x: op %02x (len %u)\n",previous,instruction.rawOpcodeForDebug,instruction.length);
            SafeConsoleUpdate();
        }
        if (result==cpu::StepResult::Halted) {
            if (&thread!=&proc.MainThread()) {
                proc.Scheduler().Exit(thread,activeCpu.Get(cpu::Reg32::EAX));
                return true;
            }
            stopReason="HLT"; return false;
        }
        if (result==cpu::StepResult::Continue) return true;
        if (win32::seh::DispatchCpuFault(activeCpu,mem,thread.teb,interpreter.LastException())==
                win32::seh::DispatchResult::Handled) return true;
        savedException=interpreter.LastException();
        stopException=&savedException;
        stopReason=result==cpu::StepResult::UnknownOpcode ? "unknown opcode" : "guest exception";
        return false;
    },rt::GuestExecution::Startup::InitializeModules);
    ui::PointerInputGate stopInput;
    padUpdate(&g_pad);stopInput.Reset(padGetButtons(&g_pad),false);
    unsigned appletPoll=0;
    uint64_t nativePollNs=0;
    while (steps<maxSteps && !execution.Stopped()) {
        if(shellSession) {
            if(!newui::OutputHealthy()){stopReason="display output failed";break;}
            const uint64_t now=ui::PerformanceNowNs();
            if(now-nativePollNs>=8'000'000ull) {
                nativePollNs=now;
                const auto status=shellSession->ServiceHost();
                if(status!=win32::user::ShellSession::HostStatus::Running) {
                    stopReason=status==win32::user::ShellSession::HostStatus::InputOverflow
                        ?"native input queue overflow":status==win32::user::ShellSession::HostStatus::Quit
                        ?"host cancelled guest":"native input source closed";
                    break;
                }
                win32::user::PresentIfNeeded(false);
            }
        }
        if((++appletPoll%4096)==0) {
            if(!ContinueApplet()) {stopReason="applet shutdown";break;}
            padUpdate(&g_pad);
            if(stopInput.Update(padGetButtons(&g_pad),false).down&HidNpadButton_Plus) {
                stopReason="host cancelled guest";break;
            }
        }
        auto result=execution.RunOne();
        runningThread=proc.Scheduler().Current();
        if (result==rt::GuestExecution::Result::AllocationFailed) {
            stopReason="cannot allocate guest native continuation stack"; break;
        }
        if (result==rt::GuestExecution::Result::Idle) {
            if (!proc.Scheduler().HasLiveThreads()) { stopReason="guest called exit()"; break; }
            if (proc.Scheduler().HasPendingTimers()) { rt::Clock::Sleep(1); continue; }
            stopReason="no runnable guest threads"; break;
        }
    }

    if (execution.InitializationFailed()) {
        g_launchStatus="Program initialization failed. Choose another EXE or retry.";
        executionLog.Complete(execution.InitializationError().c_str(),uint32_t(win32::g_guestExitCode),steps);
        SaveGuestStopReport(execution.InitializationError().c_str(),path,cpuState,proc,nullptr,true,steps);
        SafePrint("[!] DLL initialization failed: %s\n",execution.InitializationError().c_str());
        SafeConsoleUpdate();
        return;
    }
    if(steps<maxSteps && win32::g_guestExitRequested && strcmp(stopReason,"max steps reached")==0)
        stopReason="guest called exit()";
    executionLog.Complete(stopReason,uint32_t(win32::g_guestExitCode),steps);
    if (strcmp(stopReason, "guest called exit()") != 0 || win32::g_guestExitCode!=0)
        SaveGuestStopReport(stopReason, path, runningThread ? runningThread->cpu : cpuState, proc, stopException,false,steps);

    if (!g_graphicsInitialized) {
        SafePrint("\n[+] Execution stopped: %s\n", stopReason);
        SafePrint("    Steps executed: %d (EIP=0x%x)\n", steps, cpuState.eip);
        SafeConsoleUpdate();
    }
}

// Наш внутренний формат пикселя — 0xAARRGGBB (см. gdi.h). Framebuffer Switch
// в PIXEL_FORMAT_RGBA_8888 хранит байты в памяти как R,G,B,A по возрастанию
// адреса — как 32-битное little-endian число это R | G<<8 | B<<16 | A<<24.
// Перепутать порядок каналов тут — классическая ошибка при портировании,
// поэтому конвертация вынесена отдельно и явно прокомментирована.
static inline uint32_t ArgbToSwitchRgba(uint32_t argb)
{
    uint8_t a = (argb >> 24) & 0xFF;
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    return r
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

// Копирует наш GDI "экранный" битмап (тот самый, что заполняли BitBlt/
// SetPixel/SetDIBitsToDevice изнутри интерпретируемой игры) в один кадр
// настоящего framebuffer'а Switch с масштабированием (2x), и поверх рисует
// курсор (управляется стиком — см. PumpHostFrame ниже). cursorX/cursorY —
// в ВИРТУАЛЬНЫХ координатах (640x360), как и события мыши, которые мы шлём.
// Текущая трансформация "виртуальный экран -> реальный экран" — обновляется
// каждый раз в DrawGdiScreenIntoFramebuffer, используется и для курсора, и
// (обратным преобразованием) для тача в PumpHostFrame — чтобы палец на
// экране совпадал именно с тем местом, где реально нарисовано содержимое.
static float g_contentScale = 2.0f;
static int g_contentOffsetX = 0, g_contentOffsetY = 0;
static int g_contentBaseX = 0, g_contentBaseY = 0;

// ВРЕМЕННО (для диагностики хитбокса смайлика): простейший битмап-шрифт
// цифр 0-9, 3x5 точек каждая — у нас пока нет настоящего рендеринга текста
// (TextOutW), поэтому рисуем вручную, прямо в реальный framebuffer.
static const uint8_t kDigitFont[10][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b001,0b001,0b001}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
};

static void DrawDigit(u32* framebuf, u32 strideInPixels, int screenX, int screenY, int digit, int scale) {
    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (kDigitFont[digit][row] & (1 << (2 - col))) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px = screenX + col*scale + sx, py = screenY + row*scale + sy;
                        if (px >= 0 && px < 1280 && py >= 0 && py < 720)
                            framebuf[static_cast<size_t>(py) * strideInPixels + px] = 0xFF00FF00u; // ярко-зелёный
                    }
            }
        }
    }
}

static void DrawNumber(u32* framebuf, u32 strideInPixels, int screenX, int screenY, int number, int scale) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", number);
    int x = screenX;
    for (char* p = buf; *p; p++) {
        if (*p == '-') { x += (3*scale + scale); continue; } // минус пропускаем визуально
        DrawDigit(framebuf, strideInPixels, x, screenY, *p - '0', scale);
        x += 3*scale + scale; // ширина цифры + промежуток
    }
}

// Классический курсор-стрелка (как в Windows) — белая заливка с чёрной
// обводкой для видимости на любом фоне. '#'=чёрная обводка, 'O'=белая
// заливка, '.'=прозрачно (не трогаем фон).
static const char* kCursorArrow[17] = {
    "#...........",
    "#O..........",
    "#OO.........",
    "#OOO........",
    "#OOOO.......",
    "#OOOOO......",
    "#OOOOOO.....",
    "#OOOOOOO....",
    "#OOOOOOOO...",
    "#OOOOOOOOO..",
    "#OOOOOOOOOO.",
    "#OOOOOO#####",
    "#OOO#OO#....",
    "#O#..#OO#...",
    "##....#OO#..",
    "#......#OO#.",
    "........##..",
};

static void DrawCursor(u32* framebuf, u32 strideInPixels, int cx, int cy) {
    for (int row = 0; row < 17; row++) {
        const char* line = kCursorArrow[row];
        for (int col = 0; line[col] != '\0'; col++) {
            char c = line[col];
            if (c == '.') continue;
            int px = cx + col, py = cy + row;
            if (px < 0 || px >= 1280 || py < 0 || py >= 720) continue;
            uint32_t color = (c == '#') ? 0xFF000000u : 0xFFFFFFFFu;
            framebuf[static_cast<size_t>(py) * strideInPixels + px] = color;
        }
    }
}

// --- Меню ("Игра"/"Справка") — рисуем и обрабатываем клики сами, раз у нас
// нет системного USER32, который в реальной Windows отрисовывал бы меню за
// приложение. Прямоугольники — в РЕАЛЬНЫХ координатах экрана (не виртуальных),
// раз сама полоса меню всегда занимает фиксированную область сверху экрана,
// поверх отмасштабированного содержимого игры.
struct MenuRect { int x1, y1, x2, y2; };
static std::vector<MenuRect> g_menuBarRects;   // индекс == индекс в topLevel
static std::vector<MenuRect> g_dropdownRects;  // индекс == индекс в subItems открытого меню
static int g_openMenuIndex = -1;               // -1 = меню закрыто

constexpr int MENU_BAR_H = 30;       // реальные пиксели
constexpr int MENU_FONT_SCALE = 2;
constexpr uint32_t MENU_BG_COLOR = 0xFFC0C0C0u;    // классический серый Windows
constexpr uint32_t MENU_TEXT_COLOR = 0xFF000000u;
constexpr uint32_t MENU_HOVER_COLOR = 0xFF000080u; // тёмно-синий — выделение открытого пункта
constexpr uint32_t MENU_SEPARATOR_COLOR = 0xFF808080u;

// --- Тулбар инструментов (эта сессия) ---
// Реальный битмап (ресурс 859, RLE4, декодирован offline — см.
// toolbar_bitmap_data.h/.cpp) и настоящий порядок 16 инструментов,
// прочитанный ИЗ КОДА paint.exe (массив 0x103e388, индексируемый в
// цикле "cmp edi,0x10" — подтверждено дизассемблированием, не догадка).
// 2 колонки подтверждены явным "push 2" перед настройкой тулбара.
constexpr int TOOLBAR_COLS = 2;
constexpr int TOOLBAR_ROWS = 8;
// Размер кнопки — картинка честная (28x16, подтверждено: 448/16=28
// точно), но реальный размер КНОПКИ (с отступами вокруг картинки)
// однозначно найти в дизассемблировании не удалось (SetSizes нигде не
// вызывается явно) — используем задокументированное стандартное
// MFC-соглашение (image + 7px по ширине, + 6px по высоте) как разумное
// приближение, скорректируем по факту визуальной проверки на реальном
// прогоне при необходимости.
//
// ИСПРАВЛЕНО (эта сессия): иконки инструментов имеют РАЗНУЮ ширину (см.
// g_toolIconRects в toolbar_bitmap_data.h — точная таблица, найдена
// методичной ручной проверкой по битмапу, а не единой формулой). Ширина
// кнопки теперь берёт максимальную ширину иконки (Zoom=26px) + отступ,
// чтобы вместить любую из 16 без обрезки. TOOLBAR_ICON_X_BIAS полностью
// убран — раньше он лечил симптом (кривой X_BIAS) вместо настоящей
// причины (неверная нарезка исходника).
constexpr int TOOLBAR_BTN_W = 34; // максимальная иконка (26px) + отступы
constexpr int TOOLBAR_BTN_H = win32::TOOL_IMAGE_H + 6; // 22
constexpr int TOOLBAR_W = TOOLBAR_BTN_W * TOOLBAR_COLS; // реальные пиксели, резервируем слева
constexpr uint32_t TOOLBAR_BG_COLOR = 0xFFC0C0C0u;
constexpr uint32_t TOOLBAR_SELECTED_COLOR = 0xFF000080u; // рамка выбранного инструмента

// Порядок ID — ИЗ КОДА paint.exe (0x103e388), не догадка:
constexpr uint16_t g_toolIds[16] = {
    0x26d, 0x26c, // Своб.выделение, Прямоуг.выделение
    0x27d, 0x26f, // Ластик, Заливка
    0x27f, 0x27e, // Пипетка, Лупа
    0x27c, 0x280, // Карандаш, Кисть
    0x273, 0x26e, // Аэрограф, Текст
    0x270, 0x274, // Линия, Кривая
    0x281, 0x278, // Прямоугольник, Многоугольник
    0x283, 0x27a, // Эллипс, Скруглённый прямоугольник
};
// Индекс текущего выбранного инструмента в g_toolIds (для рамки
// выделения) — по умолчанию Карандаш (индекс 6), честный дефолт
// paint.exe (подтверждено в прошлых сессиях: [0x103d74c] изначально
// указывает именно на Карандаш).
static int g_selectedToolIndex = 6;

// --- Палитра цветов (эта сессия) ---
// Настоящие 28 цветов MS Paint — прочитаны из кода paint.exe
// (0x103d260), не воссозданы визуально (см. win32::g_paintPalette в
// palette_data.h). Классическая раскладка — 2 ряда по 14 свотчей,
// снизу экрана. ГЕОМЕТРИЯ (размер свотча, положение) — первое
// приближение, НЕ проверено на реальном железе (тот же урок, что и с
// тулбаром — легко подстроить один блок констант, если будет нужно).
constexpr int PALETTE_COLS = 14;
constexpr int PALETTE_ROWS = 2;
constexpr int PALETTE_SWATCH_W = 24;
constexpr int PALETTE_SWATCH_H = 20;
// Слева от свотчей — индикатор текущих цветов (два перекрывающихся
// квадрата, как в оригинальном Paint): передний план сверху-слева, фон
// снизу-справа.
constexpr int PALETTE_INDICATOR_W = 40;
constexpr int PALETTE_H = PALETTE_SWATCH_H * PALETTE_ROWS; // резервируем снизу экрана
constexpr int PALETTE_W = PALETTE_INDICATOR_W + PALETTE_SWATCH_W * PALETTE_COLS;
constexpr uint32_t PALETTE_BG_COLOR = 0xFFC0C0C0u;
constexpr uint32_t PALETTE_BORDER_COLOR = 0xFF000000u;

// Текущие выбранные цвета (для отрисовки индикатора) — по умолчанию
// чёрный передний план/белый фон, честный дефолт paint.exe.
static uint32_t g_currentForegroundColor = 0xFF000000u;
static uint32_t g_currentBackgroundColor = 0xFFFFFFFFu;

// Эти три пункта относятся к старой системе WinHelp (.hlp-файлы) — она не
// поддерживается нативно современным Windows и падает даже в ОРИГИНАЛЬНОЙ
// игре на актуальной системе (подтверждено тестером на реальном ПК). Это не
// наш баг — блокируем клик по ним у себя (и рисуем серым), раз и в оригинале
// это не работает.
static bool IsDisabledHelpMenuId(uint16_t id) {
    return id == 590 || id == 591 || id == 592; // "Вызов справки"/"Предметный указатель"/"Пользование справкой"
}

static void DrawMenuBar(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_menuBarRects.clear();
    if (!win32::g_menuLoaded) return;

    for (int y = 0; y < MENU_BAR_H && y < screenH; y++)
        for (int x = 0; x < screenW; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = MENU_BG_COLOR;

    int x = 12;
    for (size_t i = 0; i < win32::g_loadedMenu.topLevel.size(); i++) {
        const auto& item = win32::g_loadedMenu.topLevel[i];
        int textW = win32::MeasureTextUtf16(item.text.data(), item.text.size(), MENU_FONT_SCALE);
        constexpr int padding = 14;
        int itemX1 = x, itemX2 = x + textW + padding * 2;

        bool isOpen = (g_openMenuIndex == static_cast<int>(i));
        if (isOpen) {
            for (int yy = 0; yy < MENU_BAR_H; yy++)
                for (int xx = itemX1; xx < itemX2 && xx < screenW; xx++)
                    framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = MENU_HOVER_COLOR;
        }
        uint32_t textColor = isOpen ? 0xFFFFFFFFu : MENU_TEXT_COLOR;
        int textY = (MENU_BAR_H - 7 * MENU_FONT_SCALE) / 2;
        win32::DrawTextUtf16(framebuf, screenW, screenH, itemX1 + padding, textY,
                              item.text.data(), item.text.size(), textColor, MENU_FONT_SCALE);

        g_menuBarRects.push_back({itemX1, 0, itemX2, MENU_BAR_H});
        x = itemX2;
    }

    // Выпадающий список текущего открытого пункта (если есть)
    g_dropdownRects.clear();
    if (g_openMenuIndex < 0 || g_openMenuIndex >= static_cast<int>(win32::g_loadedMenu.topLevel.size())) return;

    const auto& menu = win32::g_loadedMenu.topLevel[g_openMenuIndex];
    int dropX = g_menuBarRects[g_openMenuIndex].x1;
    int dropY = MENU_BAR_H;

    int maxTextW = 0;
    for (const auto& sub : menu.subItems) {
        int w = win32::MeasureTextUtf16(sub.text.data(), sub.text.size(), MENU_FONT_SCALE);
        if (!sub.shortcutText.empty())
            w += 30 + win32::MeasureTextUtf16(sub.shortcutText.data(), sub.shortcutText.size(), MENU_FONT_SCALE);
        if (w > maxTextW) maxTextW = w;
    }
    int dropW = maxTextW + 40;
    constexpr int itemH = 7 * MENU_FONT_SCALE + 12;
    constexpr int sepH = 7;

    int totalH = 0;
    for (const auto& sub : menu.subItems) totalH += sub.isSeparator ? sepH : itemH;

    for (int yy = 0; yy < totalH && dropY + yy < screenH; yy++)
        for (int xx = 0; xx < dropW && dropX + xx < screenW; xx++)
            framebuf[static_cast<size_t>(dropY + yy) * strideInPixels + (dropX + xx)] = MENU_BG_COLOR;

    int curY = dropY;
    for (const auto& sub : menu.subItems) {
        int h = sub.isSeparator ? sepH : itemH;
        if (!sub.isSeparator) {
            uint32_t itemColor = IsDisabledHelpMenuId(sub.id) ? 0xFF808080u : MENU_TEXT_COLOR;
            int ty = curY + (h - 7 * MENU_FONT_SCALE) / 2;
            win32::DrawTextUtf16(framebuf, screenW, screenH, dropX + 12, ty,
                                  sub.text.data(), sub.text.size(), itemColor, MENU_FONT_SCALE);
            if (!sub.shortcutText.empty()) {
                int shortcutW = win32::MeasureTextUtf16(sub.shortcutText.data(), sub.shortcutText.size(), MENU_FONT_SCALE);
                win32::DrawTextUtf16(framebuf, screenW, screenH, dropX + dropW - shortcutW - 12, ty,
                                      sub.shortcutText.data(), sub.shortcutText.size(), itemColor, MENU_FONT_SCALE);
            }
            g_dropdownRects.push_back({dropX, curY, dropX + dropW, curY + h});
        } else {
            int midY = curY + h / 2;
            for (int xx = 4; xx < dropW - 4; xx++)
                if (dropX + xx < screenW && midY < screenH)
                    framebuf[static_cast<size_t>(midY) * strideInPixels + (dropX + xx)] = MENU_SEPARATOR_COLOR;
            g_dropdownRects.push_back({0, 0, 0, 0}); // разделитель — клик по нему никуда не попадёт
        }
        curY += h;
    }
}

// Тулбар инструментов (эта сессия) — рисуем слева, ниже полосы меню,
// вертикальной полосой TOOLBAR_W пикселей на всю оставшуюся высоту
// экрана. Реальные иконки из настоящего ресурса 859 (см.
// toolbar_bitmap_data.h), масштабируем каждую 28x16 картинку под кнопку
// целочисленным ближайшим соседом (чтобы не мылить пиксель-арт).
static std::vector<MenuRect> g_toolbarButtonRects; // индекс == индекс в g_toolIds

static void DrawToolbar(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_toolbarButtonRects.clear();
    if (!win32::g_menuLoaded || !IsRunningPaint()) return; // тулбар — специфика именно Paint, не любого MFC-приложения с меню

    int menuReserve = MENU_BAR_H;
    for (int y = menuReserve; y < screenH; y++)
        for (int x = 0; x < TOOLBAR_W && x < screenW; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = TOOLBAR_BG_COLOR;

    for (int i = 0; i < 16; i++) {
        int col = i % TOOLBAR_COLS;
        int row = i / TOOLBAR_COLS;
        int btnX = col * TOOLBAR_BTN_W;
        int btnY = menuReserve + row * TOOLBAR_BTN_H;
        g_toolbarButtonRects.push_back({btnX, btnY, btnX + TOOLBAR_BTN_W, btnY + TOOLBAR_BTN_H});

        if (i == g_selectedToolIndex) {
            uint32_t c = TOOLBAR_SELECTED_COLOR;
            for (int yy = btnY; yy < btnY + TOOLBAR_BTN_H && yy < screenH; yy++)
                for (int xx = btnX; xx < btnX + TOOLBAR_BTN_W && xx < screenW; xx++)
                    framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = c;
        }

        // Картинка инструмента — честные пиксели из встроенного массива,
        // альфа=0 означает прозрачность (цвет-маска исходного ресурса).
        // Каждая иконка теперь имеет СВОЮ ширину (см. g_toolIconRects) —
        // никакого X_BIAS, честное центрирование по РЕАЛЬНОЙ ширине
        // конкретной иконки.
        const auto& rect = win32::g_toolIconRects[i];
        int imgX = btnX + (TOOLBAR_BTN_W - rect.srcW) / 2;
        int imgY = btnY + (TOOLBAR_BTN_H - win32::TOOL_IMAGE_H) / 2;
        for (int dy = 0; dy < win32::TOOL_IMAGE_H; dy++) {
            for (int dx = 0; dx < rect.srcW; dx++) {
                uint32_t argb = win32::g_toolbarBitmap[dy * win32::TOOLBAR_BITMAP_W + (rect.srcX + dx)];
                uint8_t a = (argb >> 24) & 0xFF;
                if (a == 0) continue; // прозрачно — не трогаем фон
                int px = imgX + dx, py = imgY + dy;
                if (px >= 0 && px < screenW && py >= 0 && py < screenH)
                    framebuf[static_cast<size_t>(py) * strideInPixels + px] = 0xFF000000u | (argb & 0xFFFFFFu);
            }
        }
    }
}

// Палитра цветов (эта сессия) — снизу экрана, полоса шириной PALETTE_W:
// слева индикатор текущих fg/bg (два перекрывающихся квадрата, как в
// оригинале), справа 28 честных свотчей из настоящей палитры paint.exe.
static std::vector<MenuRect> g_paletteSwatchRects; // индекс == индекс в win32::g_paintPalette

static void DrawPalette(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_paletteSwatchRects.clear();
    if (!win32::g_menuLoaded || !IsRunningPaint()) return; // палитра — специфика именно Paint

    int paletteY = screenH - PALETTE_H;
    if (paletteY < 0) return;

    for (int y = paletteY; y < screenH; y++)
        for (int x = 0; x < PALETTE_W && x < screenW; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = PALETTE_BG_COLOR;

    // Индикатор текущих цветов — фон снизу-справа (больше), передний
    // план сверху-слева (меньше, поверх) — классическая раскладка Paint.
    // РАЗМЕР РАЗНИЦЫ УВЕЛИЧЕН (обратная связь с реального железа): при
    // разнице всего в 6px два квадрата визуально сливались в один блок,
    // создавая впечатление "перепутанных" fg/bg — само взаимное
    // расположение (что меньше и сверху — это fg) было логически
    // верным всегда, проблема была чисто в неразличимости на маленьком
    // экране.
    {
        int bgSize = PALETTE_H - 6;
        int fgSize = bgSize - 14; // было -6, теперь заметно больше разница
        int bgX = 6, bgY = paletteY + 6;
        for (int yy = bgY; yy < bgY + bgSize && yy < screenH; yy++)
            for (int xx = bgX; xx < bgX + bgSize && xx < screenW; xx++) {
                bool border = (xx == bgX || xx == bgX + bgSize - 1 || yy == bgY || yy == bgY + bgSize - 1);
                framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = border ? PALETTE_BORDER_COLOR : g_currentBackgroundColor;
            }
        int fgX = 2, fgY = paletteY + 2; // 4px отступ от bg (было 2px) — заметнее на маленьком экране
        for (int yy = fgY; yy < fgY + fgSize && yy < screenH; yy++)
            for (int xx = fgX; xx < fgX + fgSize && xx < screenW; xx++) {
                bool border = (xx == fgX || xx == fgX + fgSize - 1 || yy == fgY || yy == fgY + fgSize - 1);
                framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = border ? PALETTE_BORDER_COLOR : g_currentForegroundColor;
            }
    }

    for (int i = 0; i < win32::PALETTE_COLOR_COUNT; i++) {
        int col = i % PALETTE_COLS;
        int row = i / PALETTE_COLS;
        int swX = PALETTE_INDICATOR_W + col * PALETTE_SWATCH_W;
        int swY = paletteY + row * PALETTE_SWATCH_H;
        g_paletteSwatchRects.push_back({swX, swY, swX + PALETTE_SWATCH_W, swY + PALETTE_SWATCH_H});
        uint32_t color = win32::g_paintPalette[i];
        for (int yy = swY; yy < swY + PALETTE_SWATCH_H && yy < screenH; yy++) {
            for (int xx = swX; xx < swX + PALETTE_SWATCH_W && xx < screenW; xx++) {
                bool border = (xx == swX || yy == swY); // тонкая рамка между свотчами
                framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = border ? PALETTE_BORDER_COLOR : color;
            }
        }
    }
}

// Преобразование виртуальных координат (640x360) в реальные координаты
// экрана — та же трансформация (масштаб+сдвиг), что использует отрисовка,
// нужна, чтобы проверять клик джойконом по меню (оно в реальных координатах)
static void VirtualToReal(int vx, int vy, int& rx, int& ry) {
    rx = g_contentOffsetX + static_cast<int>((vx - g_contentBaseX) * g_contentScale);
    ry = g_contentOffsetY + static_cast<int>((vy - g_contentBaseY) * g_contentScale);
}

// Проверяет клик по меню (полоса или выпадающий список) в РЕАЛЬНЫХ
// координатах экрана. Возвращает true, если клик "проглочен" меню (не нужно
// пересылать его как обычный клик по игре).
static bool HandleMenuClick(int realX, int realY) {
    if (!win32::g_menuLoaded) return false;

    if (g_openMenuIndex >= 0) {
        for (size_t i = 0; i < g_dropdownRects.size(); i++) {
            const auto& r = g_dropdownRects[i];
            if (r.x2 <= r.x1) continue; // разделитель
            if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
                const auto& sub = win32::g_loadedMenu.topLevel[g_openMenuIndex].subItems[i];
                if (!sub.isSeparator && !IsDisabledHelpMenuId(sub.id)) {
                    uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
                    win32::g_messageQueue.Post(hwnd, win32::WM_COMMAND, sub.id, 0);
                }
                g_openMenuIndex = -1;
                return true;
            }
        }
        g_openMenuIndex = -1;
        return true;
    }

    for (size_t i = 0; i < g_menuBarRects.size(); i++) {
        const auto& r = g_menuBarRects[i];
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            g_openMenuIndex = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

// Клик по кнопке тулбара (эта сессия) — та же схема, что и HandleMenuClick:
// проверяем реальные координаты против сохранённых прямоугольников кнопок,
// и если попали — сигнализируем host_functions.cpp через
// g_requestedToolId (сам вызов SelectTool происходит ТАМ — у нас нет
// прямого доступа к гостевому коду отсюда).
static bool HandleToolbarClick(int realX, int realY) {
    if (!win32::g_menuLoaded || !IsRunningPaint()) return false;
    for (size_t i = 0; i < g_toolbarButtonRects.size(); i++) {
        const auto& r = g_toolbarButtonRects[i];
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            g_selectedToolIndex = static_cast<int>(i);
            win32::g_requestedToolId = g_toolIds[i];
            return true;
        }
    }
    return false;
}

// Клик по свотчу палитры (эта сессия) — isRightClick=false (левый/A) =
// передний план, true (правый/ZR) = фон, как в оригинальном Paint.
// Сигнализируем host_functions.cpp через g_requestedForeground/
// BackgroundColor (сам вызов SetForegroundColor/SetBackgroundColor
// происходит ТАМ — у нас нет прямого доступа к гостевому коду отсюда),
// и сразу обновляем локальный индикатор для отрисовки.
static bool HandlePaletteClick(int realX, int realY, bool isRightClick) {
    if (!win32::g_menuLoaded || !IsRunningPaint()) return false;
    for (size_t i = 0; i < g_paletteSwatchRects.size(); i++) {
        const auto& r = g_paletteSwatchRects[i];
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            uint32_t argb = win32::g_paintPalette[i]; // ARGB8888 — для ОТРИСОВКИ индикатора
            // ИСПРАВЛЕНО (обратная связь с реального железа): сигнал
            // g_requestedForegroundColor/BackgroundColor должен нести
            // настоящий COLORREF (0x00BBGGRR — именно это ожидает
            // честный SetForegroundColor), а не ARGB8888 напрямую. Раньше
            // белый (0xFFFFFFFF в ARGB) совпадал с sentinel'ом "нет
            // запроса" и потому НИКОГДА не применялся, а остальные
            // несимметричные цвета (R≠B) получали перепутанные местами
            // красный/синий каналы при разборе как "чужого" формата.
            uint32_t colorref = ((argb & 0xFFu) << 16) | (argb & 0xFF00u) | ((argb >> 16) & 0xFFu);
            if (isRightClick) {
                g_currentBackgroundColor = argb; // индикатор рисуем в ARGB, тут это ок
                win32::g_requestedBackgroundColor = colorref;
            } else {
                g_currentForegroundColor = argb;
                win32::g_requestedForegroundColor = colorref;
            }
            return true;
        }
    }
    return false;
}

// --- Модальные диалоги (Чемпионы, Особые и т.п.) — рисуем и обрабатываем
// клики сами, как и меню. Диалог рисуется В РЕАЛЬНЫХ координатах экрана,
// поверх всего остального, по центру.

// Перевод диалоговых единиц в пиксели — стандартная формула Win32 для
// системного шрифта диалогов ("MS Shell Dlg", ~8pt): baseUnitX=6, baseUnitY=13
constexpr int DLG_BASE_X = 6, DLG_BASE_Y = 13;
constexpr int DlgToPixelX(int du) { return du * DLG_BASE_X / 4; }
constexpr int DlgToPixelY(int du) { return du * DLG_BASE_Y / 8; }

constexpr int DLG_FONT_SCALE = 1; // было 2 — не влезал в компактную раскладку диалога, наезжал на соседние поля
constexpr int DLG_UI_SCALE = 2; // масштаб самого окна/контролов — ОТДЕЛЬНО от масштаба шрифта (раньше были ошибочно связаны одной переменной)
constexpr uint32_t DLG_BG_COLOR = 0xFFC0C0C0u;
constexpr uint32_t DLG_TEXT_COLOR = 0xFF000000u;
constexpr uint32_t DLG_BORDER_LIGHT = 0xFFFFFFFFu;
constexpr uint32_t DLG_BORDER_DARK = 0xFF808080u;
constexpr uint32_t DLG_EDIT_BG = 0xFFFFFFFFu;
constexpr uint32_t DLG_TITLEBAR_COLOR = 0xFF000080u; // классический тёмно-синий заголовок активного окна Windows
constexpr uint32_t DLG_TITLEBAR_TEXT = 0xFFFFFFFFu;
constexpr int DLG_TITLEBAR_H = 16;

static std::vector<MenuRect> g_dialogButtonRects; // индекс == индекс в g_currentDialog.controls
static int g_dialogOffsetX = 0, g_dialogOffsetY = 0; // где диалог реально нарисован на экране

static void FillRect(u32* fb, u32 stride, int screenW, int screenH, int x1, int y1, int x2, int y2, uint32_t color) {
    for (int y = y1; y < y2 && y < screenH; y++) {
        if (y < 0) continue;
        for (int x = x1; x < x2 && x < screenW; x++) {
            if (x < 0) continue;
            fb[static_cast<size_t>(y) * stride + x] = color;
        }
    }
}

// Простая рамка "приподнято" (светлая сверху/слева, тёмная снизу/справа) —
// классический вид кнопок и панелей в Windows 9x/XP classic
static void Draw3DBorder(u32* fb, u32 stride, int screenW, int screenH, int x1, int y1, int x2, int y2, bool raised) {
    uint32_t top = raised ? DLG_BORDER_LIGHT : DLG_BORDER_DARK;
    uint32_t bottom = raised ? DLG_BORDER_DARK : DLG_BORDER_LIGHT;
    FillRect(fb, stride, screenW, screenH, x1, y1, x2, y1 + 2, top);
    FillRect(fb, stride, screenW, screenH, x1, y1, x1 + 2, y2, top);
    FillRect(fb, stride, screenW, screenH, x1, y2 - 2, x2, y2, bottom);
    FillRect(fb, stride, screenW, screenH, x2 - 2, y1, x2, y2, bottom);
}

static void DrawDialog(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_dialogButtonRects.clear();
    if (!win32::g_dialogActive) return;

    const auto& dlg = win32::g_currentDialog;
    int dlgW = DlgToPixelX(dlg.cx) * DLG_UI_SCALE + 20;
    int dlgH = DlgToPixelY(dlg.cy) * DLG_UI_SCALE + 20;
    int baseX = (screenW - dlgW) / 2;
    int baseY = (screenH - dlgH) / 2;
    g_dialogOffsetX = baseX;
    g_dialogOffsetY = baseY;

    // Фон + рамка диалога
    FillRect(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + dlgW, baseY + dlgH, DLG_BG_COLOR);
    Draw3DBorder(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + dlgW, baseY + dlgH, true);

    // Заголовок диалога — теперь настоящая синяя полоса, как в реальной
    // Windows (активное окно) — раньше был просто текст поверх того же
    // серого фона, что и у самой игры, и терялся на общем фоне
    FillRect(framebuf, strideInPixels, screenW, screenH, baseX + 2, baseY + 2, baseX + dlgW - 2, baseY + 2 + DLG_TITLEBAR_H, DLG_TITLEBAR_COLOR);
    win32::DrawTextUtf16(framebuf, screenW, screenH, baseX + 6, baseY + 2 + (DLG_TITLEBAR_H - 7 * DLG_FONT_SCALE) / 2,
                          dlg.title.data(), dlg.title.size(), DLG_TITLEBAR_TEXT, DLG_FONT_SCALE);

    int contentTop = baseY + DLG_TITLEBAR_H + 12;

    for (size_t i = 0; i < dlg.controls.size(); i++) {
        const auto& c = dlg.controls[i];
        int cx1 = baseX + DlgToPixelX(c.x) * DLG_UI_SCALE;
        int cy1 = contentTop + DlgToPixelY(c.y) * DLG_UI_SCALE;
        int cx2 = cx1 + DlgToPixelX(c.cx) * DLG_UI_SCALE;
        int cy2 = cy1 + DlgToPixelY(c.cy) * DLG_UI_SCALE;

        // Реальный текст — либо то, что игра установила через SetDlgItemTextW,
        // либо исходный текст из ресурса (для статичных подписей типа "Новичок:")
        const std::vector<uint16_t>* text = &c.text;
        auto it = win32::g_dialogItemText.find(c.id);
        if (it != win32::g_dialogItemText.end()) text = &it->second;

        if (c.windowClass == "button") {
            FillRect(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, DLG_BG_COLOR);
            Draw3DBorder(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, true);
            int tw = win32::MeasureTextUtf16(text->data(), text->size(), DLG_FONT_SCALE);
            win32::DrawTextUtf16(framebuf, screenW, screenH, cx1 + (cx2 - cx1 - tw) / 2, cy1 + (cy2 - cy1 - 7 * DLG_FONT_SCALE) / 2,
                                  text->data(), text->size(), DLG_TEXT_COLOR, DLG_FONT_SCALE);
            g_dialogButtonRects.push_back({cx1, cy1, cx2, cy2});
        } else if (c.windowClass == "edit") {
            FillRect(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, DLG_EDIT_BG);
            Draw3DBorder(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, false);
            win32::DrawTextUtf16(framebuf, screenW, screenH, cx1 + 3, cy1 + (cy2 - cy1 - 7 * DLG_FONT_SCALE) / 2,
                                  text->data(), text->size(), DLG_TEXT_COLOR, DLG_FONT_SCALE);
            g_dialogButtonRects.push_back({cx1, cy1, cx2, cy2}); // теперь кликабельно — вызывает swkbd
        } else { // static и всё прочее — просто текст
            win32::DrawTextUtf16(framebuf, screenW, screenH, cx1, cy1,
                                  text->data(), text->size(), DLG_TEXT_COLOR, DLG_FONT_SCALE);
            g_dialogButtonRects.push_back({0, 0, 0, 0});
        }
    }
}

// Вызывает системную клавиатуру Switch (swkbd) для ввода текста в поле
// EDIT. Блокирующий вызов — как и обычные модальные диалоги, но экран во
// время ввода полностью занимает сама системная клавиатура, наше собственное
// рисование в этот момент не требуется (нативная реализация всё берёт на себя).
static bool ShowKeyboardForText(const std::vector<uint16_t>& initialText, std::vector<uint16_t>& outText) {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return false;

    swkbdConfigMakePresetDefault(&kbd);

    // Начальный текст — то, что уже было в поле (например, текущее значение
    // высоты/ширины/числа мин), чтобы не печатать заново с нуля
    if (!initialText.empty()) {
        std::string initialUtf8;
        for (uint16_t ch : initialText) initialUtf8 += static_cast<char>(ch < 128 ? ch : '?');
        swkbdConfigSetInitialText(&kbd, initialUtf8.c_str());
    }

    char outStr[128] = {0};
    rc = swkbdShow(&kbd, outStr, sizeof(outStr));
    swkbdClose(&kbd);

    if (R_FAILED(rc)) return false; // пользователь отменил ввод

    outText.clear();
    for (char* p = outStr; *p; p++) outText.push_back(static_cast<uint16_t>(static_cast<unsigned char>(*p)));
    return true;
}

// Клик по диалогу — возвращает true, если клик обработан (диалог модален —
// ЛЮБОЙ клик, пока он открыт, ему и принадлежит, даже мимо контролов)
static bool HandleDialogClick(int realX, int realY) {
    if (!win32::g_dialogActive) return true; // на всякий случай съедаем клик, хотя сюда не должны попасть

    for (size_t i = 0; i < g_dialogButtonRects.size(); i++) {
        const auto& r = g_dialogButtonRects[i];
        if (r.x2 <= r.x1) continue;
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            const auto& ctrl = win32::g_currentDialog.controls[i];

            if (ctrl.windowClass == "edit") {
                // Поле ввода — вызываем клавиатуру, результат сохраняем в
                // g_dialogItemText (та же структура, что заполняет игра
                // через SetDlgItemTextW — GetDlgItemTextW потом прочитает
                // ровно то, что мы сюда положим, когда нажмут OK)
                std::vector<uint16_t> current;
                auto it = win32::g_dialogItemText.find(ctrl.id);
                current = (it != win32::g_dialogItemText.end()) ? it->second : ctrl.text;
                std::vector<uint16_t> result;
                if (ShowKeyboardForText(current, result)) {
                    win32::g_dialogItemText[ctrl.id] = std::move(result);
                }
            } else {
                // Кнопка — отправляем WM_COMMAND как раньше
                win32::g_messageQueue.Post(win32::g_dialogHwnd, win32::WM_COMMAND, ctrl.id, 0);
            }
            break;
        }
    }
    return true; // модально — клик мимо контролов тоже просто "проглатывается"
}

// --- Простой about-бокс (ShellAboutW) — заголовок + несколько строк текста
// + кнопка OK. В отличие от DialogBoxParamW тут нет шаблона ресурса, весь
// текст уже готовыми строками пришёл от самой функции (см. host_functions.cpp)
static MenuRect g_aboutOkButtonRect{0, 0, 0, 0};

static void DrawAboutBox(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    if (!win32::g_aboutBoxActive) return;

    constexpr int SCALE = 1;
    constexpr int LINE_H = 7 * SCALE + 6;
    constexpr int PAD = 16;

    int titleW = win32::MeasureTextUtf16(win32::g_aboutBoxTitle.data(), win32::g_aboutBoxTitle.size(), SCALE);
    int maxLineW = titleW;
    for (auto& line : win32::g_aboutBoxLines) {
        int w = win32::MeasureTextUtf16(line.data(), line.size(), SCALE);
        if (w > maxLineW) maxLineW = w;
    }

    int boxW = maxLineW + PAD * 2;
    if (boxW < 140) boxW = 140;
    int boxH = DLG_TITLEBAR_H + 12 + static_cast<int>(win32::g_aboutBoxLines.size()) * LINE_H + PAD + 20 /*кнопка*/;

    int baseX = (screenW - boxW) / 2;
    int baseY = (screenH - boxH) / 2;

    FillRect(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + boxW, baseY + boxH, DLG_BG_COLOR);
    Draw3DBorder(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + boxW, baseY + boxH, true);

    // Синяя полоса заголовка — как у настоящего активного окна Windows
    FillRect(framebuf, strideInPixels, screenW, screenH, baseX + 2, baseY + 2, baseX + boxW - 2, baseY + 2 + DLG_TITLEBAR_H, DLG_TITLEBAR_COLOR);
    win32::DrawTextUtf16(framebuf, screenW, screenH, baseX + 6, baseY + 2 + (DLG_TITLEBAR_H - 7 * SCALE) / 2,
                          win32::g_aboutBoxTitle.data(), win32::g_aboutBoxTitle.size(), DLG_TITLEBAR_TEXT, SCALE);

    int y = baseY + DLG_TITLEBAR_H + 12;

    for (auto& line : win32::g_aboutBoxLines) {
        win32::DrawTextUtf16(framebuf, screenW, screenH, baseX + PAD, y, line.data(), line.size(), DLG_TEXT_COLOR, SCALE);
        y += LINE_H;
    }

    // Кнопка OK
    int btnW = 50, btnH = 18;
    int btnX = baseX + (boxW - btnW) / 2;
    int btnY = baseY + boxH - PAD - btnH + 4;
    FillRect(framebuf, strideInPixels, screenW, screenH, btnX, btnY, btnX + btnW, btnY + btnH, DLG_BG_COLOR);
    Draw3DBorder(framebuf, strideInPixels, screenW, screenH, btnX, btnY, btnX + btnW, btnY + btnH, true);
    uint16_t okText[] = {0x041E, 0x041A}; // "ОК"
    int okW = win32::MeasureTextUtf16(okText, 2, SCALE);
    win32::DrawTextUtf16(framebuf, screenW, screenH, btnX + (btnW - okW) / 2, btnY + (btnH - 7 * SCALE) / 2,
                          okText, 2, DLG_TEXT_COLOR, SCALE);
    g_aboutOkButtonRect = {btnX, btnY, btnX + btnW, btnY + btnH};
}

static bool HandleAboutBoxClick(int realX, int realY) {
    if (!win32::g_aboutBoxActive) return true;
    const auto& r = g_aboutOkButtonRect;
    if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
        win32::g_aboutBoxCloseRequested = true;
    }
    return true; // модально — любой клик "проглатывается"
}

static void DrawGdiScreenIntoFramebuffer(Framebuffer& fb, int cursorX, int cursorY)
{
    if(!g_framebufferReady)return;
    // Берём уменьшенное "виртуальное" разрешение (640x360)
    uint32_t screenDc = gdi::g_gdi.GetOrCreateScreenDC(640, 360);
    const gdi::Bitmap* screen = gdi::g_gdi.GetBitmapForDC(screenDc);
    if (!screen) return;

    // Область контента — сканируем пиксели в поисках РЕАЛЬНО нарисованного
    // (не просто чистой фоновой заливки). Раньше пробовали доверять сырым
    // цифрам из MoveWindow напрямую — оказалось, что это, похоже, ВНЕШНИЙ
    // размер окна (с "накладными расходами" вроде невидимого нам заголовка),
    // а не точная область, где реально рисует GDI — отсюда и лишняя пустая
    // область снизу, и курсор, зажатый в диапазон больше настоящего.
    //
    // Ищем не "не чёрные" (это работало, пока фон был чёрным), а "не
    // совпадающие с чистой заливкой" — теперь, когда фон закрашивается
    // классическим серым (см. FillScreenDC), именно так находим границы
    // того, что игра реально нарисовала поверх фона.
    constexpr uint32_t kBackgroundFillColor = 0xFFC0C0C0u;
    int minX = screen->width, minY = screen->height, maxX = -1, maxY = -1;
    for (int y = 0; y < screen->height; y++) {
        for (int x = 0; x < screen->width; x++) {
            uint32_t px = screen->pixels[static_cast<size_t>(y) * screen->width + x];
            if (px != kBackgroundFillColor && px != 0xFF000000u) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }

    int contentX, contentY, contentW, contentH;
    if (maxX < 0) {
        // Ничего ещё не нарисовано поверх фона — используем размеры окна,
        // если уже известны, иначе весь виртуальный холст как есть
        if (win32::g_lastWindowW > 0 && win32::g_lastWindowH > 0) {
            contentX = 0; contentY = 0;
            contentW = win32::g_lastWindowW; contentH = win32::g_lastWindowH;
        } else {
            contentX = 0; contentY = 0; contentW = screen->width; contentH = screen->height;
        }
    } else {
        contentX = minX; contentY = minY;
        contentW = maxX - minX + 1; contentH = maxY - minY + 1;
    }

    // Масштаб — заполняем высоту реального экрана (с небольшим отступом
    // сверху/снизу), сохраняя пропорции; по ширине НЕ растягиваем на весь
    // экран — центрируем в ОСТАВШЕЙСЯ (за вычетом тулбара) области.
    // Резервируем сверху место под полосу меню, слева — под тулбар
    // инструментов, снизу — под палитру цветов (если меню загружено —
    // тот же признак "честный UI готов", см. DrawMenuBar/DrawToolbar/
    // DrawPalette).
    constexpr int SCREEN_W = 1280, SCREEN_H = 720;
    constexpr int MARGIN = 24;
    int menuReserve = win32::g_menuLoaded ? MENU_BAR_H : 0; // меню — общая концепция, верно для ЛЮБОГО MFC-приложения с честным меню (включая Сапёра)
    int toolbarReserve = (win32::g_menuLoaded && IsRunningPaint()) ? TOOLBAR_W : 0;
    int paletteReserve = (win32::g_menuLoaded && IsRunningPaint()) ? PALETTE_H : 0;
    int availableV = SCREEN_H - MARGIN * 2 - menuReserve - paletteReserve;
    float scale = static_cast<float>(availableV) / static_cast<float>(contentH);
    if (scale > 6.0f) scale = 6.0f; // не растягиваем чрезмерно, если контент совсем маленький

    int outW = static_cast<int>(contentW * scale);
    int outH = static_cast<int>(contentH * scale);
    int offsetX = toolbarReserve + (SCREEN_W - toolbarReserve - outW) / 2;
    int offsetY = menuReserve + (SCREEN_H - menuReserve - paletteReserve - outH) / 2;

    // Сохраняем для обратного преобразования тача (см. PumpHostFrame)
    g_contentScale = scale;
    g_contentOffsetX = offsetX;
    g_contentOffsetY = offsetY;
    g_contentBaseX = contentX;
    g_contentBaseY = contentY;

    u32 stride;
    if(g_useNewUi)newui::InvalidateOutput();
    u32* framebuf = static_cast<u32*>(framebufferBegin(&fb, &stride));
    u32 strideInPixels = stride / sizeof(u32);

    // Чистим экран в чёрный — контент теперь не покрывает всю площадь
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = 0xFF000000u;

    // Рисуем содержимое с нужным масштабом и сдвигом (центрирование)
    for (int oy = 0; oy < outH; oy++) {
        int screenY = offsetY + oy;
        if (screenY < 0 || screenY >= SCREEN_H) continue;
        int srcY = contentY + static_cast<int>(oy / scale);
        if (srcY >= screen->height) continue;
        for (int ox = 0; ox < outW; ox++) {
            int screenX = offsetX + ox;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            int srcX = contentX + static_cast<int>(ox / scale);
            if (srcX >= screen->width) continue;
            uint32_t argb = screen->pixels[static_cast<size_t>(srcY) * screen->width + srcX];
            framebuf[static_cast<size_t>(screenY) * strideInPixels + screenX] = ArgbToSwitchRgba(argb);
        }
    }

    // Меню — поверх содержимого игры
    DrawMenuBar(framebuf, strideInPixels, SCREEN_W, SCREEN_H);
    DrawToolbar(framebuf, strideInPixels, SCREEN_W, SCREEN_H);
    DrawPalette(framebuf, strideInPixels, SCREEN_W, SCREEN_H);

    // Диалог — поверх меню (модальный)
    DrawDialog(framebuf, strideInPixels, SCREEN_W, SCREEN_H);

    // About-бокс — поверх абсолютно всего, включая обычные диалоги
    DrawAboutBox(framebuf, strideInPixels, SCREEN_W, SCREEN_H);

    // Курсор — ПОСЛЕДНИМ из всех, чтобы был виден поверх абсолютно всего
    // (раньше рисовался до меню/диалога и прятался под ними)
    int cx = offsetX + static_cast<int>((cursorX - contentX) * scale);
    int cy = offsetY + static_cast<int>((cursorY - contentY) * scale);
    DrawCursor(framebuf, strideInPixels, cx, cy);

    framebufferEnd(&fb);
}

// --- Реальный ввод: стик + кнопка A (замена тача — см. историю разработки:
// hidGetTouchScreenStates уронил всю консоль целиком, а не только наше
// приложение — значит там реальная ошибка на уровне памяти, а не наш
// аккуратный abort(). Стик/кнопки — это API, что мы УЖЕ проверили на
// реальном железе много раз без единого сбоя, поэтому пока переключаемся
// на него, курсор + клик вместо прямого тача.)

static Framebuffer g_fb;
static int g_cursorX = 320, g_cursorY = 180; // старт — центр виртуального экрана 640x360
static float g_cursorXf = 320.0f, g_cursorYf = 180.0f; // дробная позиция — для плавного аналогового движения
static bool g_wasTouching = false;
static uint32_t g_lastTouchX = 0, g_lastTouchY = 0;

// Переключение с текстовой консоли на графику теперь происходит ОДИН РАЗ,
// НАПЕРЁД, из main() — простого, неглубокого контекста (см. подробный
// комментарий у PumpHostFrame про то, почему лениво изнутри было опасно).

// Это и есть win32::g_pumpHostFrame — GetMessage реально зовёт эту функцию
// в цикле, пока ждёт событие (см. host_functions.cpp). За один "тик":
// двигаем курсор стиком (плавно, аналогово) ИЛИ тачем, проверяем клик и
// кнопку выхода, перерисовываем текущий кадр на экран.
//
// ПРО ТАЧ: настоящая причина прошлых крашей — 10 недостающих опкодов x86
// декодера (нашли и починили отдельно), а НЕ тач-API — любой клик тогда
// падал одинаково, независимо от способа ввода. Так что тач возвращаем.
struct LegacyPumpState {
    int s_frameThrottle = 0;
    bool s_aGameClickActive = false;
    bool s_zrGameClickActive = false;
    uint32_t s_aHwnd = 0, s_aLParam = 0;
    uint32_t s_zrHwnd = 0, s_zrLParam = 0;
    int s_moveThrottle = 0;
    int s_moveThrottleZr = 0;
    bool s_touchStartedOnMenu = false;
    int s_touchMoveThrottle = 0;
};
static LegacyPumpState g_legacyPump;
static win32::HostFrameResult PumpHostFrame()
{
    win32::HostFrameResult result;

    if(!ContinueApplet()) {result.shouldQuit=true;return result;}
    padUpdate(&g_pad);
    HidTouchScreenState touchState{};
    const bool rawTouch=hidGetTouchScreenStates(&touchState,1) && touchState.count>0;
    const auto input=g_legacyInputGate.Update(padGetButtons(&g_pad),rawTouch);
    const u64 kDown=input.down;

    if (kDown & HidNpadButton_Plus)
    {
        result.shouldQuit = true;
        return result;
    }

    // --- Стик: ПЛАВНОЕ (аналоговое) движение — чем сильнее отклонён стик,
    // тем быстрее курсор, а не резкий фиксированный шаг как раньше ---
    HidAnalogStickState stick = padGetStickPos(&g_pad, 0);
    constexpr int32_t DEADZONE = 4000;
    constexpr float MAX_SPEED = 0.7f; // пикселей виртуального экрана за тик при полном отклонении (было 1.1 — ещё ~37% медленнее по просьбе тестера)

    auto axisSpeed = [](int32_t v) -> float {
        int32_t av = v >= 0 ? v : -v;
        if (av <= DEADZONE) return 0.0f;
        float t = static_cast<float>(av - DEADZONE) / static_cast<float>(32767 - DEADZONE);
        float speed = t * t * MAX_SPEED; // квадратичная кривая — у центра ещё мягче, у края быстрее
        return v > 0 ? speed : -speed;
    };

    g_cursorXf += axisSpeed(stick.x);
    g_cursorYf -= axisSpeed(stick.y); // ось Y стика обычно инвертирована относительно экрана

    // Зажимаем курсор в границы РЕАЛЬНОГО игрового поля (точные размеры из
    // MoveWindow), а не жёстко в 640x360 — раньше справа оставался огромный
    // "запас хода" стика в пустоту (за пределами реального поля, но ещё в
    // пределах виртуального холста), а слева такого запаса не было вообще —
    // отсюда и ощущение "стены" именно слева, хотя формально это было просто
    // отсутствие лишнего запаса, а не баг границы как таковой.
    int maxCursorX = (win32::g_lastWindowW > 0) ? win32::g_lastWindowW - 1 : 639;
    int maxCursorY = (win32::g_lastWindowH > 0) ? win32::g_lastWindowH - 1 : 359;

    // РАСШИРЕНИЕ (эта сессия): курсор должен физически доставать до
    // полосы меню (сверху), тулбара инструментов (слева) и палитры
    // цветов (снизу) — раньше был жёстко заперт в [0, max], что делало
    // эти области недостижимыми даже при том, что они уже честно
    // рисовались. Используем те же сохранённые g_contentScale/offsetX/Y,
    // что и обратное преобразование тача — переводим "реальный (0,0)" и
    // "реальный (SCREEN_W-1,SCREEN_H-1)" обратно в виртуальные
    // координаты, это и есть минимально/максимально достижимые точки.
    float minCursorXf = 0.0f, minCursorYf = 0.0f;
    float maxCursorYf = static_cast<float>(maxCursorY);
    if (g_contentScale > 0.0f) {
        minCursorXf = static_cast<float>(g_contentBaseX) - static_cast<float>(g_contentOffsetX) / g_contentScale;
        minCursorYf = static_cast<float>(g_contentBaseY) - static_cast<float>(g_contentOffsetY) / g_contentScale;
        maxCursorYf = static_cast<float>(g_contentBaseY) + (719.0f - static_cast<float>(g_contentOffsetY)) / g_contentScale;
    }

    if (g_cursorXf < minCursorXf) g_cursorXf = minCursorXf;
    if (g_cursorXf > maxCursorX) g_cursorXf = static_cast<float>(maxCursorX);
    if (g_cursorYf < minCursorYf) g_cursorYf = minCursorYf;
    if (g_cursorYf > maxCursorYf) g_cursorYf = maxCursorYf;

    g_cursorX = static_cast<int>(g_cursorXf);
    g_cursorY = static_cast<int>(g_cursorYf);

    // Держим экран живым (не каждый тик ожидания, а раз в 10 — разумная
    // защита от лишних вызовов графики без реальной необходимости)

    if (++g_legacyPump.s_frameThrottle >= 10) {
        g_legacyPump.s_frameThrottle = 0;
        DrawGdiScreenIntoFramebuffer(g_fb, g_cursorX, g_cursorY);
    }

    // Раньше отпускание планировалось через ФИКСИРОВАННОЕ число тиков,
    // независимо от того, отпустил ли пользователь кнопку по-настоящему —
    // из-за этого удержание A вело себя не как удержание тача (тач держит
    // ЛКМ ровно пока палец на экране, а кнопка "клевала" сразу). Теперь
    // отслеживаем РЕАЛЬНОЕ состояние кнопки (padGetButtons, не только
    // "только что нажата") — полностью так же, как уже сделано для тача.
    const u64 kHeld = input.held;
    bool aHeldNow = (kHeld & HidNpadButton_A) != 0;
    bool zrHeldNow = (kHeld & HidNpadButton_ZR) != 0;

    // Клик по нажатию A (джойкон) — сначала диалог (если открыт — модальный,
    // забирает ВСЕ клики), потом меню, и только если ни то ни другое не
    // сработало — обычный игровой клик.
    if (kDown & HidNpadButton_A)
    {
        int realCX, realCY;
        VirtualToReal(g_cursorX, g_cursorY, realCX, realCY);
        bool handled = win32::g_aboutBoxActive ? HandleAboutBoxClick(realCX, realCY)
                     : win32::g_dialogActive ? HandleDialogClick(realCX, realCY)
                     : HandleMenuClick(realCX, realCY)
                     || HandleToolbarClick(realCX, realCY)
                     || HandlePaletteClick(realCX, realCY, false);

        if (!handled)
        {
            uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONDOWN, 1, lParam);
            g_legacyPump.s_aHwnd = hwnd;
            g_legacyPump.s_aLParam = lParam;
            g_legacyPump.s_aGameClickActive = true;
        }
    }
    else if (!aHeldNow && g_legacyPump.s_aGameClickActive)
    {
        // Кнопку реально отпустили — только теперь шлём UP
        win32::g_messageQueue.Post(g_legacyPump.s_aHwnd, win32::WM_LBUTTONUP, 0, g_legacyPump.s_aLParam);
        g_legacyPump.s_aGameClickActive = false;
    }
    else if (aHeldNow && g_legacyPump.s_aGameClickActive)
    {
        // Кнопка ВСЁ ЕЩЁ удерживается — реальная мышь в этот момент шлёт
        // WM_MOUSEMOVE (с зажатой левой кнопкой в wParam), и, похоже, именно
        // на это реагирует визуальная обратная связь игры при удержании
        // (например, предпросмотр соседних клеток) — просто DOWN...UP без
        // этих промежуточных сообщений визуально неотличим от быстрого тапа,
        // даже если сам UP теперь реально приходит с задержкой.

        if (++g_legacyPump.s_moveThrottle >= 3) {
            g_legacyPump.s_moveThrottle = 0;
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(g_legacyPump.s_aHwnd, win32::WM_MOUSEMOVE, 1 /*MK_LBUTTON*/, lParam);
        }
    }

    // Правый клик (флажок) по ZR — та же логика удержания. Сначала
    // проверяем те же UI-зоны, что и для A (палитра — правый клик =
    // цвет фона; меню/тулбар не имеют отдельного поведения на правый
    // клик, но всё равно проверяем, чтобы клик по ним не улетал в игру
    // с координатами вне холста).
    if (kDown & HidNpadButton_ZR)
    {
        int realCX, realCY;
        VirtualToReal(g_cursorX, g_cursorY, realCX, realCY);
        bool zrHandled = win32::g_aboutBoxActive ? HandleAboutBoxClick(realCX, realCY)
                        : win32::g_dialogActive ? HandleDialogClick(realCX, realCY)
                        : HandleMenuClick(realCX, realCY)
                          || HandleToolbarClick(realCX, realCY)
                          || HandlePaletteClick(realCX, realCY, true);

        if (!zrHandled)
        {
            uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(hwnd, win32::WM_RBUTTONDOWN, 2, lParam); // MK_RBUTTON
            g_legacyPump.s_zrHwnd = hwnd;
            g_legacyPump.s_zrLParam = lParam;
            g_legacyPump.s_zrGameClickActive = true;
        }
    }
    else if (!zrHeldNow && g_legacyPump.s_zrGameClickActive)
    {
        win32::g_messageQueue.Post(g_legacyPump.s_zrHwnd, win32::WM_RBUTTONUP, 0, g_legacyPump.s_zrLParam);
        g_legacyPump.s_zrGameClickActive = false;
    }
    else if (zrHeldNow && g_legacyPump.s_zrGameClickActive)
    {

        if (++g_legacyPump.s_moveThrottleZr >= 3) {
            g_legacyPump.s_moveThrottleZr = 0;
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(g_legacyPump.s_zrHwnd, win32::WM_MOUSEMOVE, 2 /*MK_RBUTTON*/, lParam);
        }
    }

    // --- Тач-экран: касание напрямую двигает курсор В ТУ ТОЧКУ и кликает ---
    bool isTouchingNow = false;
    uint32_t touchX = 0, touchY = 0;

    if (input.touching)
    {
        isTouchingNow = true;
        // Реальные координаты тача — в физических пикселях (1280x720 в
        // портативном режиме).
        float realX = static_cast<float>(touchState.touches[0].x);
        float realY = static_cast<float>(touchState.touches[0].y);

        if (!g_wasTouching)
        {
            // Самый первый тик касания — сначала about-бокс, потом диалог,
            // потом меню, до пересчёта в виртуальные координаты
            g_legacyPump.s_touchStartedOnMenu = win32::g_aboutBoxActive
                ? HandleAboutBoxClick(static_cast<int>(realX), static_cast<int>(realY))
                : win32::g_dialogActive
                ? HandleDialogClick(static_cast<int>(realX), static_cast<int>(realY))
                : HandleMenuClick(static_cast<int>(realX), static_cast<int>(realY))
                  || HandleToolbarClick(static_cast<int>(realX), static_cast<int>(realY))
                  || HandlePaletteClick(static_cast<int>(realX), static_cast<int>(realY), false);
        }

        if (!g_legacyPump.s_touchStartedOnMenu)
        {
            // Обратное преобразование той же трансформации (масштаб + сдвиг
            // под центрирование), что использует отрисовка — иначе палец
            // попадал бы не туда, где реально нарисовано содержимое.
            int vx = g_contentBaseX + static_cast<int>((realX - g_contentOffsetX) / g_contentScale);
            int vy = g_contentBaseY + static_cast<int>((realY - g_contentOffsetY) / g_contentScale);
            int maxVx = (win32::g_lastWindowW > 0) ? win32::g_lastWindowW - 1 : 639;
            int maxVy = (win32::g_lastWindowH > 0) ? win32::g_lastWindowH - 1 : 359;
            if (vx < 0) vx = 0;
            if (vx > maxVx) vx = maxVx;
            if (vy < 0) vy = 0;
            if (vy > maxVy) vy = maxVy;
            touchX = static_cast<uint32_t>(vx);
            touchY = static_cast<uint32_t>(vy);

            // Курсор сразу переезжает под палец — и визуально, и для следующего клика
            g_cursorXf = static_cast<float>(touchX);
            g_cursorYf = static_cast<float>(touchY);
            g_cursorX = static_cast<int>(touchX);
            g_cursorY = static_cast<int>(touchY);
        }
    }

    uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
    if (!g_legacyPump.s_touchStartedOnMenu)
    {
        if (isTouchingNow && !g_wasTouching)
        {
            // Только что коснулись — WM_LBUTTONDOWN
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONDOWN, 1,
                                        (touchY << 16) | touchX);
        }
        else if (!isTouchingNow && g_wasTouching)
        {
            // Только что отпустили — WM_LBUTTONUP по последним известным координатам
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONUP, 0,
                                        (g_lastTouchY << 16) | g_lastTouchX);
        }
        else if (isTouchingNow && g_wasTouching)
        {
            // НАЙДЕННЫЙ БАГ (эта сессия): палец УЖЕ на экране и просто
            // двигается — раньше здесь не было ВООБЩЕ НИКАКОГО сообщения,
            // только DOWN (первое касание) и UP (отпускание). Реальная
            // мышь в этот момент шлёт WM_MOUSEMOVE с зажатой левой
            // кнопкой в wParam — без этого Paint (и вообще любой код,
            // ожидающий WM_MOUSEMOVE во время "перетаскивания") никогда
            // не узнаёт о движении пальца. Троттлинг — как у кнопки A.

            if (++g_legacyPump.s_touchMoveThrottle >= 3) {
                g_legacyPump.s_touchMoveThrottle = 0;
                win32::g_messageQueue.Post(hwnd, win32::WM_MOUSEMOVE, 1 /*MK_LBUTTON*/,
                                            (touchY << 16) | touchX);
            }
        }
    }

    g_wasTouching = isTouchingNow;
    if (isTouchingNow) { g_lastTouchX = touchX; g_lastTouchY = touchY; }

    return result;
}

static void ResetLegacyFrontend() {
    g_contentScale=2.0f;g_contentOffsetX=g_contentOffsetY=g_contentBaseX=g_contentBaseY=0;
    g_menuBarRects.clear();g_dropdownRects.clear();g_openMenuIndex=-1;
    g_selectedToolIndex=6;g_currentForegroundColor=0xFF000000u;g_currentBackgroundColor=0xFFFFFFFFu;
    g_toolbarButtonRects.clear();g_paletteSwatchRects.clear();g_dialogButtonRects.clear();
    g_dialogOffsetX=g_dialogOffsetY=0;g_aboutOkButtonRect={0,0,0,0};
    g_cursorX=320;g_cursorY=180;g_cursorXf=320.0f;g_cursorYf=180.0f;
    g_wasTouching=false;g_lastTouchX=g_lastTouchY=0;g_legacyPump={};
}

// Вся "тяжёлая" логика — на ОТДЕЛЬНОМ потоке с явно большим стеком (см.
// main() ниже). Найдено через addr2line+objdump: реальный краш происходил
// на самой обычной инструкции записи в локальный стек (stp xzr,xzr,[sp,#32])
// внутри DecodeCore — это переполнение стека НАСТОЯЩЕГО ARM64-выполнения,
// не имеющее отношения к нашей эмулируемой памяти. Вложенный вызов WndProc
// (CallGuestFunctionStdcall создаёт ещё один Interpreter поверх уже
// глубокой цепочки) исчерпывал маленький стек главного потока по умолчанию.
// --- Новая оконная подсистема (USER32/GDI32, слой "ui2"): платформа Switch ---
//
// Экран Windows (800x600) масштабируется на 1280x720 с полями по бокам,
// поверх рисуется курсор. Ввод: тач — мышь (касание = левая кнопка), стик —
// курсор, A — левая кнопка, ZR — правая, крестовина — стрелки, B — Esc,
// X — Enter, Y — Tab, L — Alt (меню), R — Пуск при активной оболочке (иначе F2), «+» — закрыть окно. Поля
// ввода открывают экранную клавиатуру.
namespace newui {

win32::user::ShellSession* g_shellSession=nullptr;
constexpr int kOutW=ui::kHandheldWidth,kOutH=ui::kHandheldHeight;
ui::SoftwarePresentation g_software;
ui::OpenGlPresentation g_gpu;
ui::SwitchGpuOutput g_gpuOutput;
bool g_gpuActive=false;
uint64_t g_gpuFallbacks=0;
std::string g_gpuError="none";
ui::Presentation g_presentation;
float g_cx=kOutW/2.0f,g_cy=kOutH/2.0f;
bool g_touching=false;
int g_lastTouchX=0,g_lastTouchY=0;
ui::PointerInputGate g_pointerInputGate;
ui::StickPointerMotion g_pointerMotion;
u64 g_startupHeldButtons=0;
bool g_startupTouch=false;
std::string g_presentationGuest;
struct FramebufferStats {
    uint64_t frames=0,beginNs=0,endNs=0,maxBeginNs=0,maxEndNs=0,deferredDragFrames=0;
    uint64_t skippedUnchanged=0,gpuFrames=0,gpuSwapNs=0,maxGpuSwapNs=0;
} g_framebufferStats;
bool g_havePresented=false,g_cursorVisible=true;
uint64_t g_presentedSerial=0,g_presentedGeneration=0;
ui::PresentationCursor g_presentedCursor;
using PresentationClock=std::chrono::steady_clock;
uint64_t PresentationElapsed(PresentationClock::time_point start) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(PresentationClock::now()-start).count());
}
void InvalidateOutput() {g_havePresented=false;g_software.Reset();}

bool CreateSoftwareOutput() {
    if(g_framebufferReady)return true;
    Result rc=framebufferCreate(&g_fb,nwindowGetDefault(),kOutW,kOutH,PIXEL_FORMAT_RGBA_8888,2);
    if(R_FAILED(rc))return false;
    rc=framebufferMakeLinear(&g_fb);
    if(R_FAILED(rc)) {framebufferClose(&g_fb);return false;}
    g_framebufferReady=true;return true;
}
bool g_outputFailed=false,g_guestPresentation=false;
uint64_t g_scopeFallbackStart=0;
struct OutputDevice {
    bool Open(ui::OutputPreference mode) {
        g_havePresented=false;g_outputFailed=false;g_gpuError="none";
        if(mode==ui::OutputPreference::GpuPreferred) {
            if(g_gpuOutput.Initialize() && g_gpuOutput.MakeCurrent() && g_gpu.Initialize()) {
                g_gpuActive=true;g_presentation.Attach(&g_gpu);return true;
            }
            ++g_gpuFallbacks;g_gpuError=g_gpuOutput.Error();
            if(g_gpuOutput.Error()==std::string("none"))g_gpuError=g_gpu.Error();
            g_gpu.Shutdown();g_gpuOutput.Shutdown();g_gpuActive=false;
        }
        if(!CreateSoftwareOutput())return false;
        g_presentation.Attach(&g_software);return true;
    }
    void Close() {
        g_presentation.Detach();g_gpu.Shutdown();g_gpuOutput.Shutdown();g_gpuActive=false;
        if(g_framebufferReady){framebufferClose(&g_fb);g_framebufferReady=false;}
        g_havePresented=false;g_outputFailed=false;g_graphicsInitialized=false;
    }
};
OutputDevice g_outputDevice;
ui::OutputLifetime<OutputDevice> g_outputLifetime(g_outputDevice);
bool InitializeOutput(bool gpuRequested) {
    if(g_outputFailed)g_outputLifetime.Close();
    g_graphicsInitialized=g_outputLifetime.Ensure(gpuRequested?ui::OutputPreference::GpuPreferred:ui::OutputPreference::Software);
    return g_graphicsInitialized;
}
bool OutputHealthy() {return g_outputLifetime.Ready() && !g_outputFailed;}
bool DesktopOutput() {
    if(OutputHealthy())return true;
    return InitializeOutput(true);
}
void CloseOutput() {g_outputLifetime.Close();}
bool FallBackToSoftware(const char* error) {
    g_gpuError=error;++g_gpuFallbacks;
    g_presentation.Attach(&g_software); // retain the owned CPU snapshot
    g_presentation.ClearLayers();win32::user::EnableWindowLayers(false);
    g_gpu.Shutdown();g_gpuOutput.Shutdown();g_gpuActive=false;g_havePresented=false;
    return CreateSoftwareOutput();
}
void BeginPresentation(const char* guestPath) {
    // The device/context and the last owned frame remain alive across guests.
    g_cursorVisible=true;g_guestPresentation=true;g_presentation.ResetStats();g_gpu.ResetStats();
    g_scopeFallbackStart=g_gpuFallbacks;
    g_presentationGuest=guestPath;g_framebufferStats={};
    ui::g_rasterMetrics.Reset(true);win32::user::S().inputFrames.Reset(true);
    g_havePresented=false;
    g_touching=false;
    win32::user::S().cursorX=static_cast<int>(g_cx);win32::user::S().cursorY=static_cast<int>(g_cy);
    padUpdate(&g_pad);
    HidTouchScreenState touch={0};
    const bool touching=hidGetTouchScreenStates(&touch,1) && touch.count>0;
    g_startupHeldButtons=padGetButtons(&g_pad);g_startupTouch=touching;
    g_pointerInputGate.Reset(g_startupHeldButtons,touching);
    g_pointerMotion.ResetTime(rt::Clock::NowUs());
}
void EndPresentation() {
    // One summary per session; never log each frame to SD or closed console.
    if(g_gpuActive)g_gpu.PollTimings();
    const auto& s=g_presentation.Stats();
    FILE* log=fopen("sdmc:/switch-recompiler-renderer.txt","w");
    if(log) {
        fprintf(log,"Presentation v7\nBuild: %s\nExecutable: %s\nGuest: %dx%d\nOutput: %dx%d\n",
            SR_BUILD_REVISION,g_presentationGuest.c_str(),g_presentation.SourceWidth(),g_presentation.SourceHeight(),kOutW,kOutH);
        fprintf(log,"Submitted: %llu\nRendered: %llu\nCursor/redraw frames: %llu\nRejected: %llu\nFrame storage: %zu bytes\n",
            (unsigned long long)s.submitted,(unsigned long long)s.rendered,(unsigned long long)s.redraws,
            (unsigned long long)s.rejected,g_presentation.FrameBytes());
        fprintf(log,"Snapshot bytes: %llu\nOutput bytes: %llu\nSnapshot ns: %llu\nRender ns: %llu\n",
            (unsigned long long)s.snapshotBytes,(unsigned long long)s.outputBytes,
            (unsigned long long)s.snapshotNs,(unsigned long long)s.renderNs);
        fprintf(log,"Framebuffer frames: %llu\nFramebuffer begin ns: %llu\nFramebuffer end ns: %llu\nMax framebuffer begin ns: %llu\nMax framebuffer end ns: %llu\nDeferred drag redraws: %llu\n",
            (unsigned long long)g_framebufferStats.frames,(unsigned long long)g_framebufferStats.beginNs,
            (unsigned long long)g_framebufferStats.endNs,(unsigned long long)g_framebufferStats.maxBeginNs,
            (unsigned long long)g_framebufferStats.maxEndNs,(unsigned long long)g_framebufferStats.deferredDragFrames);
        fprintf(log,"Compared normalized bytes: %llu\nUnchanged submissions: %llu\nNo-write renders: %llu\nSkipped unchanged scanouts: %llu\nTracking bytes: %zu\nBackend cache bytes: %zu\n",
            (unsigned long long)s.comparedBytes,(unsigned long long)s.noOpSubmissions,
            (unsigned long long)s.noOpRenders,(unsigned long long)g_framebufferStats.skippedUnchanged,
            g_presentation.TrackingBytes(),g_software.CacheBytes());
        fprintf(log,"Startup held buttons: 0x%llx\nStartup touch: %u\nStick source pixels/s limit: %.0f\n",
            (unsigned long long)g_startupHeldButtons,unsigned(g_startupTouch),ui::StickPointerMotion::MaxPixelsPerSecond);
        fprintf(log,"Backend: %s\nGPU uploaded bytes: %llu\nGPU upload calls: %llu\nGPU fallbacks: %llu\nGPU error: %s\n",
            g_gpuActive?"OpenGL":"software",(unsigned long long)g_gpu.UploadedBytes(),
            (unsigned long long)g_gpu.UploadCalls(),(unsigned long long)(g_gpuFallbacks-g_scopeFallbackStart),g_gpuError.c_str());
        fprintf(log,"GPU frames: %llu\nGPU swap ns: %llu\nMax GPU swap ns: %llu\n",
            (unsigned long long)g_framebufferStats.gpuFrames,(unsigned long long)g_framebufferStats.gpuSwapNs,
            (unsigned long long)g_framebufferStats.maxGpuSwapNs);
        fprintf(log,"Window capture enabled: %u\nWindow raster bytes: %zu\nWindow ownership bytes: %zu\nLayer snapshot bytes: %zu\nRetained layers: %zu\nLayered submissions: %llu\nLayer fallbacks: %llu\nReconciled pixels: %llu\nGPU layer draws: %llu\n",
            unsigned(win32::user::S().windowLayers.Enabled()),win32::user::S().windowLayers.PixelBytes(),
            win32::user::S().windowLayers.OwnershipBytes(),g_presentation.LayerBytes(),g_presentation.LayerCount(),
            (unsigned long long)s.layeredSubmissions,(unsigned long long)s.layerFallbacks,
            (unsigned long long)g_presentation.ReconciledPixels(),(unsigned long long)g_gpu.LayerDraws());
        fprintf(log,"Layer snapshot ns: %llu\n",(unsigned long long)s.layerSnapshotNs);
        fprintf(log,"Layer compared bytes: %llu\nOwnership checked pixels: %llu\n",
            (unsigned long long)g_presentation.LayerComparedBytes(),(unsigned long long)g_presentation.OwnershipCheckedPixels());
        const auto& t=g_gpu.Timings();
        fprintf(log,"GPU CPU stage frames: %llu\nGPU upload CPU ns: %llu\nGPU draw submission CPU ns: %llu\nMax GPU upload CPU ns: %llu\nMax GPU draw submission CPU ns: %llu\n",
            (unsigned long long)t.frames,(unsigned long long)t.uploadCpuNs,(unsigned long long)t.drawCpuNs,
            (unsigned long long)t.maxUploadCpuNs,(unsigned long long)t.maxDrawCpuNs);
        fprintf(log,"GPU timer available: %u\nGPU command interval samples: %llu\nGPU command interval ns: %llu\nMax GPU command interval ns: %llu\nGPU timer ring skips: %llu\nGPU logical texture bytes: %zu\nGPU revision cache bytes: %zu\n",
            unsigned(g_gpu.TimerAvailable()),(unsigned long long)t.gpuSamples,(unsigned long long)t.gpuNs,
            (unsigned long long)t.maxGpuNs,(unsigned long long)t.gpuSkipped,g_gpu.TextureBytes(),g_gpu.CacheBytes());
        fprintf(log,"Timing scope: CPU submission, asynchronous GPU upload/draw interval (includes idle gaps), swap CPU wait; excludes scanout and physical input latency. Pending GPU samples are not waited for. Texture bytes exclude driver/EGL buffers.\n");
        fprintf(log,"GPU screen upload attempted bytes: %llu\nGPU ownership upload attempted bytes: %llu\nGPU layer upload attempted bytes: %llu\nGPU peak logical texture bytes: %zu\n",
            (unsigned long long)t.screenUploadBytes,(unsigned long long)t.ownershipUploadBytes,
            (unsigned long long)t.layerUploadBytes,t.peakTextureBytes);
        const auto& input=win32::user::S().inputFrames;
        const auto& latency=input.Stats();
        auto duration=[&](const char* name,const ui::DurationCounter& d) {
            fprintf(log,"%s samples: %llu\n%s total ns: %llu\n%s max ns: %llu\n",name,(unsigned long long)d.count,
                name,(unsigned long long)d.totalNs,name,(unsigned long long)d.maxNs);
        };
        for(size_t i=0;i<size_t(ui::RasterStage::Count);++i) {
            const auto stage=static_cast<ui::RasterStage>(i);char name[64];
            snprintf(name,sizeof(name),"CPU raster %s",ui::RasterStageName(stage));duration(name,ui::g_rasterMetrics.Stage(stage));
        }
        duration("Input queue wait",latency.queueWait);
        duration("Input observed to queued frame",latency.observedToFrame);
        duration("Input removed to queued frame",latency.removedToFrame);
        fprintf(log,"Input measured frames: %llu\nInput coalesced moves: %llu\nInput measurement drops: %llu\nInput invalid timestamps: %llu\nInput pending samples: %zu\n",
            (unsigned long long)latency.outputFrames,(unsigned long long)latency.coalesced,(unsigned long long)latency.dropped,
            (unsigned long long)latency.invalid,input.Pending());
        fprintf(log,"Input timing scope: platform observation / queue removal to later changed CPU frame queued by output; correlation only, not causal dispatch or input-to-photon latency. Raster stages include synchronous host GDI preparation, exclude raw guest writes and interpreter time.\n");
        fclose(log);
    }
    ui::g_rasterMetrics.Reset(false);win32::user::S().inputFrames.Reset(false);
    g_presentation.ClearLayers();
    win32::user::EnableWindowLayers(false);g_guestPresentation=false;
}
void Blit() {
    if(g_outputFailed || !g_presentation.SourceWidth())return;
    const ui::PresentationCursor cursor{static_cast<int>(g_cx),static_cast<int>(g_cy),g_cursorVisible};
    // libnx linear mode preserves one CPU staging buffer; framebufferEnd
    // copies that staging image into the dequeued display slot. Skip unchanged
    // staging images before acquisition, swizzle and queueing a duplicate.
    if(g_havePresented && g_presentedSerial==g_presentation.ContentSerial() &&
       g_presentedGeneration==g_presentation.ContentGeneration() &&
       g_presentedCursor.x==cursor.x && g_presentedCursor.y==cursor.y) {
        ++g_framebufferStats.skippedUnchanged;return;
    }
    if(g_gpuActive) {
        const bool rendered=g_presentation.Render({nullptr,kOutW,kOutH,0,0},cursor);
        const auto swapStart=PresentationClock::now();
        const bool swapped=rendered && g_gpuOutput.Swap();
        const uint64_t swapNs=PresentationElapsed(swapStart);
        if(swapped) {
            ++g_framebufferStats.gpuFrames;g_framebufferStats.gpuSwapNs+=swapNs;
            g_framebufferStats.maxGpuSwapNs=std::max(g_framebufferStats.maxGpuSwapNs,swapNs);
            if(g_guestPresentation)win32::user::S().inputFrames.OutputQueued(ui::PerformanceNowNs(),ui::g_gdi.ScreenChangeCounter(),g_presentation.ContentSerial());
            g_havePresented=true;g_presentedSerial=g_presentation.ContentSerial();
            g_presentedGeneration=g_presentation.ContentGeneration();g_presentedCursor=cursor;
            return;
        }
        const char* error=rendered?g_gpuOutput.Error():g_gpu.Error();
        if(!FallBackToSoftware(error)){g_outputFailed=true;return;}
    }
    if(!g_framebufferReady){g_outputFailed=true;return;}
    u32 stride=0;
    auto start=PresentationClock::now();
    u32* fb=static_cast<u32*>(framebufferBegin(&g_fb,&stride));
    const auto beginNs=PresentationElapsed(start);
    ui::OutputBuffer target{fb,kOutW,kOutH,stride/sizeof(u32),size_t(stride/sizeof(u32))*kOutH};
    target.preserved=true;
    const bool rendered=g_presentation.Render(target,cursor);
    start=PresentationClock::now();
    framebufferEnd(&g_fb);
    const auto endNs=PresentationElapsed(start);
    ++g_framebufferStats.frames;g_framebufferStats.beginNs+=beginNs;g_framebufferStats.endNs+=endNs;
    g_framebufferStats.maxBeginNs=std::max(g_framebufferStats.maxBeginNs,beginNs);
    g_framebufferStats.maxEndNs=std::max(g_framebufferStats.maxEndNs,endNs);
    if(!rendered){g_havePresented=false;g_outputFailed=true;}
    if(rendered) {
        if(g_guestPresentation)win32::user::S().inputFrames.OutputQueued(ui::PerformanceNowNs(),ui::g_gdi.ScreenChangeCounter(),g_presentation.ContentSerial());
        g_havePresented=true;g_presentedSerial=g_presentation.ContentSerial();
        g_presentedGeneration=g_presentation.ContentGeneration();g_presentedCursor=cursor;
    }
}
std::u16string DeviceClock() {
    const std::time_t now=std::time(nullptr);std::tm deviceTime{};char clock[16]="--:--";
    if(localtime_r(&now,&deviceTime))std::strftime(clock,sizeof(clock),"%H:%M",&deviceTime);
    return std::u16string(clock,clock+std::strlen(clock));
}
bool DrawDesktop(const ui::Surface& s,int x,int y,bool cursor) {
    g_cx=float(x);g_cy=float(y);g_cursorVisible=cursor;
    if(!OutputHealthy() || !g_presentation.Submit(s))return false;
    // Cursor visibility can change between keyboard and pointer navigation.
    if(g_presentedCursor.visible!=cursor)g_havePresented=false;
    Blit();return OutputHealthy();
}
void Present(const ui::Surface& s) {
    if(g_shellSession) {
        g_shellSession->SetClock(DeviceClock());
        if(g_presentation.Submit(g_shellSession->Compose(s)))Blit();else g_outputFailed=true;
        return;
    }
    const bool submitted=g_gpuActive && win32::user::S().windowLayers.Enabled()
        ?g_presentation.Submit(s,win32::user::S().windowLayers.Scene()):g_presentation.Submit(s);
    if(submitted)Blit();else g_outputFailed=true;
}

void Key(std::vector<win32::user::InputEvent>& out, bool down, uint32_t vk,uint64_t observedNs) {
    win32::user::InputEvent e;
    e.type = down ? win32::user::InputEvent::KeyDown : win32::user::InputEvent::KeyUp;
    e.vk = vk;e.observedNs=observedNs;
    out.push_back(e);
}

void Mouse(std::vector<win32::user::InputEvent>& out, win32::user::InputEvent::Type t, int button,uint64_t observedNs) {
    win32::user::InputEvent e;
    e.type = t;
    e.x = static_cast<int>(g_cx);
    e.y = static_cast<int>(g_cy);
    e.button = button;e.observedNs=observedNs;
    out.push_back(e);
}

bool Wait(int timeoutMs, std::vector<win32::user::InputEvent>& out) {
    using IE = win32::user::InputEvent;
    uint32_t start = rt::Clock::NowMs();
    uint32_t clockPoll=start;std::u16string lastClock;
    for (;;) {
        // Native taskbar time advances even while the guest blocks in GetMessage.
        if(g_shellSession && uint32_t(rt::Clock::NowMs()-clockPoll)>=1000) {
            clockPoll=rt::Clock::NowMs();const auto clock=DeviceClock();
            if(clock!=lastClock){lastClock=clock;g_shellSession->SetClock(clock);Present(win32::user::S().screen.s);}
        }
        if (!ContinueApplet()) { IE q; q.type = IE::Quit; out.push_back(q); return true; }
        const uint64_t observedNs=ui::PerformanceNowNs();
        padUpdate(&g_pad);
        HidTouchScreenState ts = {0};
        const bool rawTouching=hidGetTouchScreenStates(&ts,1) && ts.count>0;
        const auto edges=g_pointerInputGate.Update(padGetButtons(&g_pad),rawTouching);
        const u64 down=edges.down,up=edges.up;
        const int sw=g_presentation.SourceWidth()?g_presentation.SourceWidth():win32::user::S().screenW;
        const int sh=g_presentation.SourceHeight()?g_presentation.SourceHeight():win32::user::S().screenH;
        // Stick speed is in pixels/second; rendering frequency is not a speed multiplier.
        HidAnalogStickState stick = padGetStickPos(&g_pad, 0);
        bool moved=g_pointerMotion.Advance(g_cx,g_cy,stick.x,stick.y,rt::Clock::NowUs(),sw,sh);
        // тач
        bool touching=edges.touching;
        const auto viewport=ui::FitViewport(sw,sh,kOutW,kOutH);
        if(touching) {
            const int tx=static_cast<int>(ts.touches[0].x),ty=static_cast<int>(ts.touches[0].y);
            // A new press in letterbox is not a click on the image edge.
            if(!g_touching && !viewport.Contains(tx,ty))touching=false;
        }
        if (touching) {
            int tx=static_cast<int>(ts.touches[0].x),ty=static_cast<int>(ts.touches[0].y);
            viewport.ToSource(tx,ty,sw,sh,g_cx,g_cy);
            if (!g_touching || tx != g_lastTouchX || ty != g_lastTouchY) moved = true;
            g_lastTouchX = tx;
            g_lastTouchY = ty;
        }
        if (moved) Mouse(out, IE::MouseMove, 0,observedNs);
        if (touching && !g_touching) Mouse(out, IE::MouseDown, 0,observedNs);
        if (!touching && g_touching) Mouse(out, IE::MouseUp, 0,observedNs);
        g_touching = touching;
        // кнопки
        if (down & HidNpadButton_A) Mouse(out, IE::MouseDown, 0,observedNs);
        if (up & HidNpadButton_A) Mouse(out, IE::MouseUp, 0,observedNs);
        if (down & HidNpadButton_ZR) Mouse(out, IE::MouseDown, 1,observedNs);
        if (up & HidNpadButton_ZR) Mouse(out, IE::MouseUp, 1,observedNs);
        struct { u64 button; uint32_t vk; } keys[] = {
            {HidNpadButton_Left, 0x25}, {HidNpadButton_Up, 0x26}, {HidNpadButton_Right, 0x27}, {HidNpadButton_Down, 0x28},
            {HidNpadButton_B, 0x1B}, {HidNpadButton_X, 0x0D}, {HidNpadButton_Y, 0x09}, {HidNpadButton_L, 0x12},
            {HidNpadButton_R, g_shellSession?0x5Bu:0x71u}};
        for (auto& k : keys) {
            if (down & k.button) Key(out, true, k.vk,observedNs);
            if (up & k.button) Key(out, false, k.vk,observedNs);
        }
        if (down & HidNpadButton_Plus) { IE q; q.type = IE::Quit; out.push_back(q); }
        if (moved && out.size() == 1) {
            // Deliver drag input to the guest before presenting its old pixels.
            // Otherwise a cursor-only frame can block on scanout while Paint
            // still has not received this movement. Hover keeps immediate redraw.
            if(touching || (edges.held&(HidNpadButton_A|HidNpadButton_ZR)))
                ++g_framebufferStats.deferredDragFrames;
            else Blit();
        }
        if (!out.empty()) return true;
        if (timeoutMs >= 0 && static_cast<int>(rt::Clock::NowMs() - start) >= timeoutMs) return true;
        svcSleepThread(8'000'000);
    }
}

std::string ToUtf8(const std::u16string& w) { return win32::hle::WideToUtf8(w); }

std::u16string FromUtf8(const char* s) {
    std::u16string out;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p >> 5) == 6 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((*p >> 4) == 14 && p[1] && p[2]) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else { ++p; continue; }
        out.push_back(static_cast<char16_t>(cp));
    }
    return out;
}

bool TextInput(const std::u16string& initial, bool multiline, bool password, std::u16string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    if (password) swkbdConfigMakePresetPassword(&kbd);
    std::string init = ToUtf8(initial);
    if (!init.empty()) swkbdConfigSetInitialText(&kbd, init.c_str());
    if (multiline) swkbdConfigSetType(&kbd, SwkbdType_All);
    char buf[2048] = {0};
    Result rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);
    padUpdate(&g_pad);HidTouchScreenState touch{};
    const bool touching=hidGetTouchScreenStates(&touch,1) && touch.count>0;
    g_pointerInputGate.Reset(padGetButtons(&g_pad),touching);g_touching=false;
    g_pointerMotion.ResetTime(rt::Clock::NowUs());
    if (R_FAILED(rc)) return false;
    out = FromUtf8(buf);
    InvalidateOutput();
    Blit();
    return true;
}

} // namespace newui

static void RunSelectedGuest()
{

    // Лимит шагов теперь по сути "неограниченный" — реальный выход
    // происходит по кнопке + (через PumpHostFrame -> GetMessage -> WM_QUIT),
    // а не по счётчику инструкций.
    g_launchStatus="Program stopped. Choose a program to launch again.";
    RunInterpreter(g_selectedExePath.c_str(), 40, 100000000);
}

// --- Звук через audout ---
// Простой пул из нескольких слотов-буферов (короткие игровые звуки, их
// редко нужно больше 2-3 одновременно). Каждый слот — отдельный выровненный
// буфер (audout требует выравнивание по 0x1000), которым мы владеем, пока
// audout не сообщит, что буфер отыгран (audoutGetReleasedAudioOutBuffer).
constexpr int kAudioSlots = 4;
struct AudioSlot {
    AudioOutBuffer outBuf;
    void* rawMem = nullptr;
    bool inUse = false;
};
static AudioSlot g_audioSlots[kAudioSlots];

static void ReclaimReleasedAudioBuffers() {
    AudioOutBuffer* released;
    u32 count;
    while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&released, &count)) && count > 0) {
        for (auto& slot : g_audioSlots) {
            if (slot.inUse && &slot.outBuf == released) slot.inUse = false;
        }
    }
}

static void PlayAudioImpl(const std::vector<int16_t>& samples) {
    if (samples.empty()) return;
    ReclaimReleasedAudioBuffers();

    int slotIdx = -1;
    for (int i = 0; i < kAudioSlots; i++) if (!g_audioSlots[i].inUse) { slotIdx = i; break; }
    if (slotIdx < 0) slotIdx = 0; // все заняты — жертвуем самым старым, короткие звуки, не страшно

    AudioSlot& slot = g_audioSlots[slotIdx];
    size_t dataSize = samples.size() * sizeof(int16_t);
    size_t alignedSize = (dataSize + 0xFFFu) & ~0xFFFu; // audout требует выравнивание по 0x1000

    if (slot.rawMem) { free(slot.rawMem); slot.rawMem = nullptr; }
    slot.rawMem = memalign(0x1000, alignedSize);
    if (!slot.rawMem) return; // не хватило памяти — просто без звука в этот раз, не крашимся

    memcpy(slot.rawMem, samples.data(), dataSize);
    if (alignedSize > dataSize) memset(static_cast<uint8_t*>(slot.rawMem) + dataSize, 0, alignedSize - dataSize);

    slot.outBuf.next = nullptr;
    slot.outBuf.buffer = slot.rawMem;
    slot.outBuf.buffer_size = alignedSize;
    slot.outBuf.data_size = static_cast<u32>(dataSize);
    slot.outBuf.data_offset = 0;
    slot.inUse = true;

    audoutAppendAudioOutBuffer(&slot.outBuf);
}

// --- Выбор исполняемого файла при старте ---
// Программы ищутся в romfs (вшиты в .nro) и на карте памяти в
// sdmc:/switch-recompiler/ — там же, где реестр. На карте программу
// можно положить и в свою подпапку вместе с её DLL и данными
// (sdmc:/switch-recompiler/Game/game.exe): так готовый .nro из CI
// запускает свои программы без пересборки.
// Bound inventory work before the guest starts; preserve ROMFS-before-SD order.
static ui::ExecutableCatalog ListExecutables() {
    ui::ExecutableCatalog catalog;
    catalog.ScanRoot("romfs:/",0);
    catalog.ScanRoot(std::string(win32::kRegistryFolderPath)+"/",1);
    const auto programs=std::string(win32::kRegistryFolderPath)+"/drive_c/Programs/";
    struct stat st{};
    if(stat(programs.c_str(),&st)==0 && S_ISDIR(st.st_mode))catalog.ScanRoot(programs,1);
    return catalog;
}

// Single-file autolaunch is preserved. The picker can always return on applet
// shutdown or B/PLUS, including while waiting for a new selection edge.
static bool PickConsoleExecutableFile(bool allowAutoLaunch) {
    g_selectedCommandLine.clear();
    padUpdate(&g_pad);
    ui::PointerInputGate gate;gate.Reset(padGetButtons(&g_pad),false);
    const auto catalog=ListExecutables();const auto& files=catalog.Files();
    if(files.empty()) {
        printf("[!] No .exe found in romfs or %s/\n",win32::kRegistryFolderPath);
        printf("B/PLUS: exit\n");consoleUpdate(NULL);
        while(ContinueApplet()) {
            padUpdate(&g_pad);
            if(gate.Update(padGetButtons(&g_pad),false).down&(HidNpadButton_B|HidNpadButton_Plus))break;
            svcSleepThread(16'000'000);
        }
        return false;
    }
    if(allowAutoLaunch && files.size()==1 && !catalog.Truncated()) {g_selectedExePath=files[0];return true;}
    ui::LauncherSelection selection(files.size());
    while(ContinueApplet()) {
        consoleClear();
        printf("=== Switch Recompiler - choose a program ===\n\n");
        if(!allowAutoLaunch)printf("%s\n\n",g_launchStatus);
        for(size_t i=selection.FirstVisible();i<selection.EndVisible();++i) {
            std::string label=files[i];
            for(char& c:label)if(static_cast<unsigned char>(c)<32 || c==127)c='?';
            if(label.size()>68) {
                size_t start=label.size()-65;
                while(start<label.size() && (static_cast<unsigned char>(label[start])&0xC0)==0x80)++start;
                label="..."+label.substr(start);
            }
            printf(" %s %s\n",i==selection.Selected()?">":" ",label.c_str());
        }
        printf("\n%zu/%zu  Up/Down: select  Left/Right: page\nA: launch  B/PLUS: exit\n",selection.Selected()+1,files.size());
        if(catalog.Truncated())printf("[!] Scan limit reached; list is incomplete.\n");
        if(catalog.Errors())printf("[!] Some paths could not be read.\n");
        consoleUpdate(NULL);
        for(;;) {
            if(!ContinueApplet())return false;
            padUpdate(&g_pad);const u64 down=gate.Update(padGetButtons(&g_pad),false).down;
            if(down&(HidNpadButton_B|HidNpadButton_Plus))return false;
            if(down&HidNpadButton_A){g_selectedExePath=files[selection.Selected()];return true;}
            if(down&HidNpadButton_Up){selection.Move(-1);break;}
            if(down&HidNpadButton_Down){selection.Move(1);break;}
            if(down&HidNpadButton_Left){selection.Move(-int(ui::LauncherSelection::Rows));break;}
            if(down&HidNpadButton_Right){selection.Move(int(ui::LauncherSelection::Rows));break;}
            svcSleepThread(16'000'000);
        }
    }
    return false;
}


// One frontend worker owns idle and guest output. This picker keeps the device
// and presentation alive; it never borrows guest USER/window pointers.
static bool PickDesktopExecutable(ui::DesktopShell& shell,ui::PresentationBuffer<uint32_t>& pixels,bool initial) {
    g_selectedCommandLine.clear();
    if(!shell.FilesystemReady()) {
        std::string error;
        shell.ConfigureFilesystem("sdmc:/switch-recompiler/drive_c","sdmc:/","romfs:/",error);
    }
    const auto catalog=ListExecutables();shell.SetCatalog(catalog);shell.Enter();
    const bool autoLaunch=initial && catalog.Files().size()==1 && !catalog.Truncated();
    shell.SetStatus(!shell.FilesystemReady()?"Persistent filesystem unavailable; file actions are disabled.":initial?"":g_launchStatus);
    if(cpu::g_consoleAvailable){consoleExit(NULL);cpu::g_consoleAvailable=false;}
    const bool ready=pixels.Resize(size_t(ui::DesktopShell::Width)*ui::DesktopShell::Height) && newui::DesktopOutput();
    if(!ready) {
        newui::CloseOutput();consoleInit(NULL);cpu::g_consoleAvailable=true;
        g_launchStatus="Desktop display unavailable; using text picker.";
        return PickConsoleExecutableFile(false);
    }
    g_graphicsInitialized=true;
    ui::Surface surface;surface.width=ui::DesktopShell::Width;surface.height=ui::DesktopShell::Height;
    surface.pitch=surface.width*4;surface.bits=reinterpret_cast<uint8_t*>(pixels.data());
    ui::PointerInputGate gate;ui::StickPointerMotion motion;
    padUpdate(&g_pad);HidTouchScreenState touch{};
    const bool touching=hidGetTouchScreenStates(&touch,1) && touch.count>0;
    gate.Reset(padGetButtons(&g_pad),touching);motion.ResetTime(rt::Clock::NowUs());
    float x=newui::g_cx,y=newui::g_cy;bool wasTouching=false,pointerHeld=false,keyboard=true,dirty=true,outputFailed=false;
    std::u16string lastClock;
    if(autoLaunch) {
        shell.Render(surface,newui::DeviceClock());
        if(newui::DrawDesktop(surface,int(x),int(y),false)){g_selectedExePath=catalog.Files()[0];return true;}
        outputFailed=true;
    }
    while(!outputFailed && ContinueApplet()) {
        padUpdate(&g_pad);touch={};
        const bool rawTouch=hidGetTouchScreenStates(&touch,1) && touch.count>0;
        const auto edges=gate.Update(padGetButtons(&g_pad),rawTouch);
        const auto stick=padGetStickPos(&g_pad,0);
        bool moved=motion.Advance(x,y,stick.x,stick.y,rt::Clock::NowUs(),surface.width,surface.height);
        const auto viewport=ui::FitViewport(surface.width,surface.height,1280,720);
        bool acceptedTouch=edges.touching;
        if(acceptedTouch && !wasTouching && !viewport.Contains(touch.touches[0].x,touch.touches[0].y))acceptedTouch=false;
        if(acceptedTouch) {
            const int oldX=int(x),oldY=int(y);
            viewport.ToSource(touch.touches[0].x,touch.touches[0].y,surface.width,surface.height,x,y);
            moved=moved || oldX!=int(x) || oldY!=int(y);
        }
        if(moved || acceptedTouch)keyboard=false;
        const int oldHover=shell.Hover();shell.PointerMove(int(x),int(y));
        dirty=dirty || shell.Hover()!=oldHover;
        const u64 down=edges.down;
        if(down&HidNpadButton_Plus)break;
        const u64 navigation=down&(HidNpadButton_R|HidNpadButton_B|HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right|HidNpadButton_X|HidNpadButton_Y);
        if(navigation){shell.CancelPointer();pointerHeld=false;gate.Reset(padGetButtons(&g_pad),rawTouch);}
        if(down&HidNpadButton_R){shell.ToggleStart();keyboard=true;}
        if(down&HidNpadButton_B){shell.Back();keyboard=true;}
        if(down&HidNpadButton_Up){shell.Move(-1);keyboard=true;}
        if(down&HidNpadButton_Down){shell.Move(1);keyboard=true;}
        if(down&HidNpadButton_Left){shell.Page(-1);keyboard=true;}
        if(down&HidNpadButton_Right){shell.Page(1);keyboard=true;}
        if(down&HidNpadButton_Y){shell.NextFocus();keyboard=true;}
        if(down&HidNpadButton_X || (keyboard && (down&HidNpadButton_A)))shell.Accept();
        const bool contact=!navigation && (acceptedTouch || (!keyboard && (edges.held&HidNpadButton_A)));
        if(contact && !pointerHeld)shell.PointerDown(int(x),int(y));
        if(!contact && pointerHeld)shell.PointerUp(int(x),int(y));
        pointerHeld=contact;
        dirty=dirty || down || edges.up || acceptedTouch!=wasTouching;wasTouching=acceptedTouch;
        if(shell.PendingTextInput()!=ui::DesktopShell::TextInput::None) {
            // Shell keyboard uses no guest USER state or newui::Blit binding.
            const bool address=shell.PendingTextInput()==ui::DesktopShell::TextInput::Address;
            const bool openWith=shell.PendingTextInput()==ui::DesktopShell::TextInput::OpenWith;
            SwkbdConfig config;char text[4097]={};bool accepted=false;
            if(R_SUCCEEDED(swkbdCreate(&config,0))) {
                swkbdConfigMakePresetDefault(&config);
                swkbdConfigSetHeaderText(&config,address?"Путь к папке":openWith?"Выберите приложение: путь к EXE":"Выполнить: путь и аргументы");
                swkbdConfigSetGuideText(&config,openWith?"Для расширения выбор запомнится до выхода из оболочки.":"Путь с пробелами укажите в кавычках. C:\\, S:\\ или R:\\.");
                swkbdConfigSetStringLenMax(&config,500);
                swkbdConfigSetInitialText(&config,shell.InitialText().c_str());
                accepted=R_SUCCEEDED(swkbdShow(&config,text,sizeof(text)));
                swkbdClose(&config);
            } else shell.SetStatus("Text input is unavailable.");
            if(!ContinueApplet()){shell.CompleteTextInput(false,"");break;}
            shell.CompleteTextInput(accepted,text);
            // Confirm/cancel belongs to the keyboard until released. Re-present
            // after the applet even if clock/content/cursor did not change.
            padUpdate(&g_pad);touch={};
            const bool heldTouch=hidGetTouchScreenStates(&touch,1) && touch.count>0;
            gate.Reset(padGetButtons(&g_pad),heldTouch);shell.CancelPointer();
            pointerHeld=false;wasTouching=false;keyboard=true;dirty=true;
            motion.ResetTime(rt::Clock::NowUs());
        }
        if(shell.ExitRequested() || !shell.LaunchPath().empty())break;
        const std::time_t now=std::time(nullptr);std::tm deviceTime{};char clock[16]="--:--";
        if(localtime_r(&now,&deviceTime))std::strftime(clock,sizeof(clock),"%H:%M",&deviceTime);
        const std::u16string clockText(clock,clock+std::strlen(clock));
        dirty=dirty || clockText!=lastClock;
        if(dirty) {
            shell.Render(surface,clockText);lastClock=clockText;
        }
        if((dirty || moved) && !newui::DrawDesktop(surface,int(x),int(y),!keyboard)){outputFailed=true;break;}
        dirty=false;svcSleepThread(16'000'000);
    }
    newui::g_cx=x;newui::g_cy=y;
    if(!shell.LaunchPath().empty()){g_selectedExePath=shell.LaunchPath();g_selectedCommandLine=shell.LaunchRequest().commandLine;}
    if(outputFailed && !g_appletStopped) {
        newui::CloseOutput();consoleInit(NULL);cpu::g_consoleAvailable=true;
        g_launchStatus="Desktop render unavailable; using text picker.";
        return PickConsoleExecutableFile(false);
    }
    return !shell.LaunchPath().empty() && !g_appletStopped;
}

// One native owner for selection, loading, guest fibers, output and final cleanup.
static void FrontendThreadFunc(void*) {
    struct SwitchWorkspace {
        ui::DesktopShell shell;
        ui::PresentationBuffer<uint32_t> desktopPixels;
        bool Alive(){return ContinueApplet();}
        bool Pick(bool initial){return PickDesktopExecutable(shell,desktopPixels,initial);}
        bool BeginGuest(){
            g_activeShell=&shell;padUpdate(&g_pad);
            g_useNewUi=ui::UseNewGuestUi((padGetButtons(&g_pad)&HidNpadButton_L)!=0);
            const bool requestGpu=g_useNewUi && !(padGetButtons(&g_pad)&HidNpadButton_ZL);
            if(cpu::g_consoleAvailable){consoleExit(NULL);cpu::g_consoleAvailable=false;}
            g_launchStatus="Could not initialize display. Choose a program to retry.";
            if(!newui::InitializeOutput(requestGpu))return false;
            shell.SetStatus("Loading application...");
            if(desktopPixels.size()) {
                ui::Surface s;s.width=ui::DesktopShell::Width;s.height=ui::DesktopShell::Height;s.pitch=s.width*4;
                s.bits=reinterpret_cast<uint8_t*>(desktopPixels.data());shell.Render(s,newui::DeviceClock());
                if(!newui::DrawDesktop(s,int(newui::g_cx),int(newui::g_cy),false))return false;
            }
            return true;
        }
        void RunGuest(){RunSelectedGuest();}
        void EndGuest(){g_activeShell=nullptr;if(!g_useNewUi)newui::InvalidateOutput();}
        void ReturnToDesktop(){
            if(!newui::DesktopOutput() || !desktopPixels.size())return;
            shell.Enter();shell.SetStatus(g_launchStatus);
            ui::Surface s;s.width=ui::DesktopShell::Width;s.height=ui::DesktopShell::Height;s.pitch=s.width*4;
            s.bits=reinterpret_cast<uint8_t*>(desktopPixels.data());shell.Render(s,newui::DeviceClock());
            if(!newui::DrawDesktop(s,int(newui::g_cx),int(newui::g_cy),false))
                g_launchStatus="Could not redraw desktop. Display recovery will be retried.";
        }
        void Close(){
            g_activeShell=nullptr;newui::CloseOutput();
            if(cpu::g_consoleAvailable){consoleExit(NULL);cpu::g_consoleAvailable=false;}
        }
    } workspace;
    ui::RunWorkspace(workspace);
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    hidInitializeTouchScreen(); // без этого тач может отдавать некорректное состояние

    // Реестр на SD: создаём папку (если её ещё нет — mkdir просто вернёт
    // ошибку, это не страшно, обрабатываем как "уже была") и загружаем
    // сохранённые ранее значения (рекорды, настройки). Если файла ещё нет
    // (самый первый запуск) — LoadFromFile честно вернёт false, ничего
    // страшного, реестр просто останется пустым, как раньше.
    mkdir(win32::kRegistryFolderPath, 0777);
    win32::g_registry.LoadFromFile(win32::kRegistryFilePath);

    // Звук: инициализируем audout один раз, заранее (главный поток, до
    // запуска интерпретатора) — та же логика, что и с графикой: простая,
    // неглубокая инициализация из безопасного места.
    Result audRc = audoutInitialize();
    if (R_SUCCEEDED(audRc)) {
        audoutStartAudioOut();
        win32::g_playAudioImpl = &PlayAudioImpl;
    }
    // Если audoutInitialize не удался — просто играем без звука (не критично,
    // win32::g_playAudioImpl останется nullptr, PlaySoundW тихо ничего не сыграет)

    Result rc = romfsInit();
    if (R_FAILED(rc))
    {
        // Ошибка ДО того, как мы вообще начали — остаёмся в текстовом
        // режиме консоли целиком, графику даже не трогаем.
        printf("[!] romfsInit failed: 0x%x\n", rc);
        printf("\nPress PLUS to exit.\n");
        consoleUpdate(NULL);
        while (ContinueApplet())
        {
            padUpdate(&g_pad);
            if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        if(win32::g_playAudioImpl){audoutExit();win32::g_playAudioImpl=nullptr;}
        consoleExit(NULL);
        return 0;
    }

    printf("=== Switch Recompiler ===\n");
    consoleUpdate(NULL);

    Thread frontend{};
    Result frontendResult=threadCreate(&frontend,FrontendThreadFunc,nullptr,nullptr,2*1024*1024,0x2C,-2);
    if(R_SUCCEEDED(frontendResult)) {
        frontendResult=threadStart(&frontend);
        if(R_SUCCEEDED(frontendResult))threadWaitForExit(&frontend);
        threadClose(&frontend);
    }
    if(R_FAILED(frontendResult)) {
        printf("[!] Could not start frontend: 0x%x\nPress PLUS to exit.\n",frontendResult);
        while(ContinueApplet()) {padUpdate(&g_pad);if(padGetButtonsDown(&g_pad)&HidNpadButton_Plus)break;consoleUpdate(NULL);svcSleepThread(16'000'000);}
    }
    romfsExit();
    if(win32::g_playAudioImpl){audoutExit();win32::g_playAudioImpl=nullptr;}
    if(cpu::g_consoleAvailable)consoleExit(NULL);
    return 0;
}

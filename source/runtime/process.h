#pragma once
// process.h
//
// Процесс гостя: адресное пространство, загруженные модули, главный поток.
//
// Загрузка как в Windows:
//  - EXE по своему ImageBase; DLL по предпочтительному адресу или, если он
//    занят, в свободное место с применением базовых релокаций;
//  - импорты связываются либо с экспортами настоящих DLL гостя (файлы
//    рядом с EXE или в каталогах поиска), либо с HLE-реализациями
//    (host_functions и kernel32_*.cpp) — для системных DLL всегда HLE;
//  - перенаправленные экспорты ("NTDLL.RtlAllocateHeap"), импорт по
//    ординалу, статический TLS с колбэками, DllMain в порядке зависимостей.
//
// Используется и на Switch (main.cpp), и в хостовых прогонах (tests/).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../cpu/cpu_state.h"
#include "../cpu/guest_memory.h"
#include "guest_thread.h"
#include "heap.h"
#include "process_local_state.h"
#include "guest_scheduler.h"
#include "handle_table.h"
#include "../loader/pe_parser.h"

namespace rt {

struct Module {
    std::string name;       // имя в нижнем регистре, с расширением ("mfc42u.dll")
    std::string path;       // путь к файлу (пусто для HLE-модулей)
    uint32_t base = 0;      // HMODULE
    uint32_t size = 0;
    uint32_t entryRva = 0;
    bool isDll = false;
    bool hle = false;       // эмулируемая DLL: функции — host-адреса
    bool initialized = false;
    bool initializing = false;
    bool initializationFailed = false;
    uint32_t initializationExceptionCode = 0, initializationExceptionAddress = 0;
    bool threadCallsDisabled = false;
    bool processDetached = false;
    bool unloaded = false; // retain metadata for snapshots across guest callbacks
    bool unloading = false;
    bool pinned = false;
    uint32_t pendingLoads = 0; // roots held across cooperative loader transactions
    uint32_t loadReferences = 0; // explicit LoadLibrary/GetModuleHandleEx roots

    // Разложенный образ (копия того, что в памяти гостя на момент загрузки) —
    // для ресурсов и разбора каталогов.
    std::vector<uint8_t> image;
    uint32_t resourceRva = 0;

    // Экспорт
    uint32_t exportDirRva = 0, exportDirSize = 0;
    uint32_t ordinalBase = 1;
    std::vector<uint32_t> exportRvas;              // по (ординал - base)
    std::map<std::string, uint32_t> exportByName;  // имя -> индекс в exportRvas

    std::vector<Module*> dependencies;

    // Статический TLS
    uint32_t tlsIndex = 0xFFFFFFFFu;
    uint32_t tlsTemplateStart = 0, tlsTemplateEnd = 0, tlsZeroFill = 0, tlsCallbacks = 0;
};

class Process {
public:
    explicit Process(cpu::PagedGuestMemory& mem);

    cpu::PagedGuestMemory& Memory() { return mem_; }
    GuestHeap& Heap() {return defaultHeap_;}
    template<class T> T& LocalState() {return localState_.Get<T>();}

    // Каталоги для поиска DLL (после каталога EXE).
    void AddSearchPath(const std::string& dir) { searchPaths_.push_back(dir); }
    // Эти DLL всегда эмулируются, даже если файл найден (по умолчанию —
    // системные: kernel32, user32, gdi32, ...).
    void ForceHle(const std::string& dllName);
    // Эти DLL грузить с диска нельзя: только HLE (например, чтобы
    // временно сравнить с HLE-реализацией MFC).
    bool IsAlwaysHle(const std::string& lowerName) const;

    // Загружает EXE со всеми зависимостями. Не запускает инициализацию.
    Module* LoadMainExe(const std::string& path, std::string& error);

    // LoadLibrary: находит уже загруженный модуль или грузит новый (с
    // зависимостями). initialize — вызвать DllMain/TLS сразу (нужен cpu).
    Module* LoadLibrary(const std::string& name, std::string& error, bool reference=true);
    Module* LoadLibraryInitialized(cpu::CpuState& state, const std::string& name,
                                   std::string& error, uint32_t& failureCode);
    bool ReferenceModule(Module* module, bool pin=false);
    bool FreeLibrary(cpu::CpuState& state, uint32_t handle);

    Module* MainModule() { return main_; }
    Module* FindByHandle(uint32_t handle);
    Module* FindByName(const std::string& name);
    Module* FindByAddress(uint32_t addr);
    const std::vector<std::unique_ptr<Module>>& Modules() const { return modules_; }

    // GetProcAddress: имя или ординал (если name пусто). 0 — нет такой функции.
    uint32_t GetProcAddress(Module* m, const std::string& name, uint16_t ordinal = 0, cpu::CpuState* state=nullptr);

    // Готовит главный поток: стек, TEB/PEB, область CRT/MFC/кучи для
    // старых HLE-функций, статический TLS. Заполняет cpu (EIP = точка
    // входа EXE, адрес возврата — выход из процесса).
    bool PrepareMainThread(cpu::CpuState& cpu, std::string& error);

    // Вызывает TLS-колбэки и DllMain(DLL_PROCESS_ATTACH) всех ещё не
    // инициализированных модулей в порядке зависимостей. false — какая-то
    // DLL вернула FALSE.
    bool InitializeModules(cpu::CpuState& cpu, std::string& error);

    // То же для модулей, загруженных во время работы (LoadLibrary из
    // гостя): вызывается изнутри host-функции, состояние cpu не трогает,
    // кроме EAX/ECX/EDX (как обычный вызов функции).
    bool InitializeNewModules(cpu::CpuState& cpu, std::string& error);

    // Recursive per-guest loader lock; waiters park their native continuation.
    class LoaderLock {
    public:
        explicit LoaderLock(Process& process);
        ~LoaderLock();
        explicit operator bool() const { return acquired_; }
        LoaderLock(const LoaderLock&) = delete;
        LoaderLock& operator=(const LoaderLock&) = delete;
    private:
        Process& process_;
        bool acquired_;
    };
    void RequestThreadExit(GuestThread& thread, uint32_t code);

    // Командная строка процесса (GetCommandLine, __getmainargs). По
    // умолчанию — путь к EXE.
    void SetCommandLine(const std::string& cmd) { commandLine_ = cmd; }
    const std::string& CommandLine() const { return commandLine_; }

    uint32_t TebAddress() const { return mainThread_.teb ? mainThread_.teb : kTebAddress; }
    uint32_t CurrentThreadId() const {
        const GuestThread* current = scheduler_.Current();
        return current ? current->id : mainThread_.id;
    }
    uint32_t CurrentTebAddress() const {
        const GuestThread* current = scheduler_.Current();
        return current ? current->teb : TebAddress();
    }
    const GuestThread& MainThread() const { return mainThread_; }
    GuestThread& MainThread() { return mainThread_; }
    // Allocate a separate x86 worker stack, TEB and TLS; does not schedule it.
    GuestThread* CreateGuestThread(uint32_t startAddress, uint32_t parameter,
                                   uint32_t stackReserve, bool suspended,
                                   std::string& error);
    GuestThread* FindThread(uint32_t id);
    GuestScheduler& Scheduler() { return scheduler_; }
    const GuestScheduler& Scheduler() const { return scheduler_; }
    HandleTable& Handles() { return handles_; }
    uint32_t PebAddress() const { return kPebAddress; }
    uint32_t TlsArrayAddress() const { return tlsArray_; }

    static constexpr uint32_t kTebAddress = 0x7FFDE000u;
    static constexpr uint32_t kPebAddress = 0x7FFDF000u;
    static constexpr uint32_t kTlsSlots = 64;

    static std::string NormalizeName(const std::string& name);

private:
    friend class GuestExecution;
    // Only after all native frames of this thread have returned to scheduler.
    void ReleaseThreadMemory(GuestThread& thread);
    void StopThreads(uint32_t code, GuestThread* except=nullptr);
    bool NotifyProcessExit(GuestThread& thread);
    bool NotifyThread(GuestThread& thread, uint32_t reason);
    bool AcquireLoaderLock();
    void ReleaseLoaderLock();
    bool CallTls(cpu::CpuState& state, Module& module, uint32_t reason, uint32_t reserved=0);
    Module* LoadModuleFile(const std::string& path, const std::string& lowerName, std::string& error);
    Module* CreateHleModule(const std::string& lowerName);
    std::string FindFile(const std::string& lowerName) const;
    bool BindImports(Module* m, const pe::PEParser& parser, std::string& error);
    uint32_t ResolveExport(Module* m, const std::string& name, uint16_t ordinal, int depth,
                           std::vector<std::pair<Module*,Module*>>* edges=nullptr);
    void ParseExports(Module* m);
    void ApplyRelocations(Module* m, const pe::PEParser& parser, uint32_t delta);
    void ParseTls(Module* m, const pe::PEParser& parser);
    bool InitializeModule(cpu::CpuState& cpu, Module* m, std::string& error, uint32_t reserved=0);
    bool SetupStaticTls(Module* m, std::string& error);
    bool CollectUnusedModules(cpu::CpuState& state);
    std::vector<Module*> ReachableModules() const;
    bool RollbackNewModules(cpu::CpuState& state, size_t first);
    uint32_t loadFailureCode_ = 126;
    void ReleaseModuleMemory(Module& module);
    bool collectingModules_ = false;

    cpu::PagedGuestMemory& mem_;
    GuestHeap defaultHeap_;
    ProcessLocalState localState_;
    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<Module*> initializationOrder_;
    uint32_t loaderOwner_ = 0;
    uint32_t loaderDepth_ = 0;
    Module* main_ = nullptr;
    std::vector<std::string> searchPaths_;
    std::vector<std::string> forcedHle_;
    std::string exeDir_;
    std::string commandLine_;
    uint32_t tlsArray_ = 0;
    uint32_t crtGlobals_ = 0;
    bool threadReady_ = false;
    bool shuttingDown_ = false;
    GuestThread* processExitThread_ = nullptr;
    GuestThread mainThread_{};
    std::vector<std::unique_ptr<GuestThread>> workerThreads_;
    uint32_t nextThreadId_ = 0x1008u;
    GuestScheduler scheduler_{};
    HandleTable handles_{};
};

// Текущий процесс — для HLE-функций (LoadLibrary, GetProcAddress, ...).
extern Process* g_process;
// Standalone legacy/native harnesses without a Process keep their historical
// singleton behavior. Production HLE and sequential-session tests bind Process.
template<class T> T& LocalState() {
    if(g_process)return g_process->LocalState<T>();
    static T standalone;
    return standalone;
}

// Адрес host-функции «возврат из точки входа EXE» (выход из процесса с
// кодом из EAX). Кладётся на стек как адрес возврата главного потока.
uint32_t EntryReturnAddress();

} // namespace rt

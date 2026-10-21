// process.cpp — загрузка модулей и подготовка главного потока (см. process.h)

#include "process.h"
#include "guest_execution.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "files.h"
#include "../win32/crt_globals.h"
#include "../win32/host_functions.h"
#include "../win32/iat_patcher.h"
#include "../win32/message_loop.h"
#include "../win32/seh_abi.h"
#include "../win32/user32.h"

namespace rt {

Process* g_process = nullptr;

namespace {

// Системные DLL, которые всегда эмулируются: их «настоящие» версии из
// Windows работают поверх ядра NT, которого у нас нет.
const char* const kSystemDlls[] = {
    "kernel32.dll", "ntdll.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comctl32.dll", "comdlg32.dll",
    "shell32.dll", "shlwapi.dll", "ole32.dll", "oleaut32.dll", "winmm.dll", "gdiplus.dll", "version.dll",
    "ws2_32.dll", "wsock32.dll", "imm32.dll", "winspool.drv", "uxtheme.dll", "rpcrt4.dll", "msimg32.dll",
    "kernelbase.dll",
};

uint32_t Read32(const std::vector<uint8_t>& img, uint32_t rva) {
    if (static_cast<size_t>(rva) + 4 > img.size()) return 0;
    uint32_t v;
    std::memcpy(&v, img.data() + rva, 4);
    return v;
}
uint16_t Read16(const std::vector<uint8_t>& img, uint32_t rva) {
    if (static_cast<size_t>(rva) + 2 > img.size()) return 0;
    uint16_t v;
    std::memcpy(&v, img.data() + rva, 2);
    return v;
}
void Write32(std::vector<uint8_t>& img, uint32_t rva, uint32_t v) {
    if (static_cast<size_t>(rva) + 4 <= img.size()) std::memcpy(img.data() + rva, &v, 4);
}
std::string ReadStr(const std::vector<uint8_t>& img, uint32_t rva) {
    std::string s;
    for (size_t i = rva; i < img.size() && img[i]; ++i) s.push_back(static_cast<char>(img[i]));
    return s;
}

bool FileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string DirName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Выход из процесса, когда точка входа EXE сделала RET (как BaseProcessStart).
win32::HostCallOutcome Impl_EntryReturn(cpu::CpuState& cpu, cpu::IGuestMemory&) {
    win32::HostCallOutcome r;
    r.requestExit = true;
    r.exitCode = static_cast<int32_t>(cpu.Get(cpu::Reg32::EAX));
    r.returnValue = cpu.Get(cpu::Reg32::EAX);
    return r;
}

const win32::HostFunctionEntry kRuntimeEntries[] = {
    {"INTERNAL", "EntryReturn", 0, win32::CallConv::Cdecl, &Impl_EntryReturn},
};
const bool kRegistered = win32::RegisterHostFunctions(kRuntimeEntries, 1);

} // namespace

uint32_t EntryReturnAddress() {
    (void)kRegistered;
    static int idx = win32::FindHostFunction("INTERNAL", "EntryReturn");
    return win32::IndexToHostAddress(idx);
}

Process::Process(cpu::PagedGuestMemory& mem) : mem_(mem) {
    win32::SetImportBindingMemory(&mem_);
    scheduler_.SetExitCallback([this](GuestThread& thread) {
        win32::user::OnGuiThreadExit(thread.id);
        for (const auto& object : handles_.Objects()) {
            bool signal = false;
            if (object->type == ObjectKind::Thread)
                signal = static_cast<ThreadObject&>(*object).threadId == thread.id;
            else if (object->type == ObjectKind::Mutex) {
                auto& mutex = static_cast<SyncObject&>(*object);
                if (mutex.ownerThread == thread.id) {
                    mutex.ownerThread = mutex.recursion = 0;
                    mutex.abandoned = mutex.signaled = true;
                    signal = true;
                }
            }
            if (signal)
                for (uint32_t handle : handles_.Aliases(object.get()))
                    scheduler_.Signal(handle, true);
        }
    });
}

Process::LoaderLock::LoaderLock(Process& process)
    : process_(process), acquired_(process.AcquireLoaderLock()) {}
Process::LoaderLock::~LoaderLock() { if (acquired_) process_.ReleaseLoaderLock(); }

bool Process::AcquireLoaderLock() {
    constexpr uint32_t waitToken=0xfffffff1u; // internal scheduler token, never a HANDLE
    auto* current=scheduler_.Current();
    const uint32_t id=current ? current->id : 0x1004u;
    if (win32::g_guestExitRequested || !GuestExecution::Checkpoint()) return false;
    while (loaderOwner_ && loaderOwner_!=id) {
        if (!current || !GuestExecution::Active()) return false;
        scheduler_.Wait(*current,waitToken);
        GuestExecution::Yield();
        if (!GuestExecution::Checkpoint()) return false;
    }
    loaderOwner_=id;
    ++loaderDepth_;
    return true;
}
void Process::ReleaseLoaderLock() {
    if (--loaderDepth_==0) {
        loaderOwner_=0;
        scheduler_.Signal(0xfffffff1u,true);
    }
}
void Process::RequestThreadExit(GuestThread& thread, uint32_t code) {
    thread.pendingApcs.clear();
    thread.alertableWait=false;
    if (!GuestExecution::Active()) { scheduler_.Exit(thread,code); return; }
    thread.exitRequested=true;
    thread.requestedExitCode=code;
    if (thread.detaching) thread.detachAborted=true;
}

std::string Process::NormalizeName(const std::string& name) {
    std::string base = name;
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    for (char& ch : base) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    if (base.find('.') == std::string::npos) base += ".dll";
    return base;
}

void Process::ForceHle(const std::string& dllName) { forcedHle_.push_back(NormalizeName(dllName)); }

bool Process::IsAlwaysHle(const std::string& lowerName) const {
    for (const char* s : kSystemDlls) if (lowerName == s) return true;
    return std::find(forcedHle_.begin(), forcedHle_.end(), lowerName) != forcedHle_.end();
}

std::string Process::FindFile(const std::string& lowerName) const {
    std::vector<std::string> dirs;
    if (!exeDir_.empty()) dirs.push_back(exeDir_);
    dirs.insert(dirs.end(), searchPaths_.begin(), searchPaths_.end());
    for (const std::string& d : dirs) {
        std::string p = d + "/" + lowerName;
        if (FileExists(p)) return p;
        // Имя на диске может быть в другом регистре (MFC42u.DLL)
        std::string upper = lowerName;
        for (char& ch : upper) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (FileExists(d + "/" + upper)) return d + "/" + upper;
        size_t dot = lowerName.find('.');
        if (dot != std::string::npos) {
            std::string mixed = upper.substr(0, dot) + lowerName.substr(dot);
            if (FileExists(d + "/" + mixed)) return d + "/" + mixed;
            mixed = upper.substr(0, dot - 1) + lowerName.substr(dot - 1, 1) + upper.substr(dot);
            if (FileExists(d + "/" + mixed)) return d + "/" + mixed; // MFC42u.DLL
        }
    }
    return "";
}

Module* Process::FindByHandle(uint32_t handle) {
    for (auto& m : modules_) if (m->base == handle) return m.get();
    return nullptr;
}

Module* Process::FindByName(const std::string& name) {
    std::string n = NormalizeName(name);
    for (auto& m : modules_) if (m->name == n) return m.get();
    return nullptr;
}

Module* Process::FindByAddress(uint32_t addr) {
    for (auto& m : modules_) if (addr >= m->base && addr < m->base + m->size) return m.get();
    return nullptr;
}

Module* Process::CreateHleModule(const std::string& lowerName) {
    auto m = std::make_unique<Module>();
    m->name = lowerName;
    m->hle = true;
    m->isDll = true;
    m->initialized = true;
    // Уникальный HMODULE: маленький регион с заголовком "MZ" (некоторые
    // программы проверяют, что по HMODULE лежит PE).
    m->size = 0x1000;
    m->base = mem_.Allocate(m->size, cpu::kProtRead, 0x70000000u, 0x7FF00000u);
    if (m->base) {
        mem_.Write16(m->base, 0x5A4D);
    }
    Module* raw = m.get();
    modules_.push_back(std::move(m));
    return raw;
}

Module* Process::LoadMainExe(const std::string& path, std::string& error) {
    exeDir_ = DirName(path);
    FileSystem().SetRoot(exeDir_); // C:\ гостя — каталог EXE
    Module* m = LoadModuleFile(path, NormalizeName(path), error);
    if (!m) return nullptr;
    if (m->isDll) { error = "это DLL, а не EXE"; return nullptr; }
    main_ = m;
    win32::g_imageBase = m->base;
    win32::g_imageBuffer = m->image.data();
    win32::g_imageBufferSize = m->image.size();
    win32::g_resourceDirRva = m->resourceRva;
    return m;
}

Module* Process::LoadLibrary(const std::string& name, std::string& error) {
    LoaderLock lock(*this);
    if (!lock) { error="loader operation interrupted"; return nullptr; }
    std::string n = NormalizeName(name);
    if (Module* m = FindByName(n)) {
        if (m->initializationFailed) { error=n+": previous load/initialization failed"; return nullptr; }
        return m;
    }
    if (!IsAlwaysHle(n)) {
        std::string path = FindFile(n);
        if (!path.empty()) return LoadModuleFile(path, n, error);
    }
    return CreateHleModule(n);
}

Module* Process::LoadModuleFile(const std::string& path, const std::string& lowerName, std::string& error) {
    pe::PEParser parser;
    pe::LoadResult lr = parser.LoadFromFile(path);
    if (lr != pe::LoadResult::Ok) {
        error = path + ": " + pe::ToString(lr);
        return nullptr;
    }
    auto mod = std::make_unique<Module>();
    Module* m = mod.get();
    m->name = lowerName;
    m->path = path;
    m->isDll = parser.IsDll();
    m->size = (parser.GetSizeOfImage() + 0xFFF) & ~0xFFFu;
    m->entryRva = parser.GetEntryPointRva();
    m->resourceRva = parser.GetResourceDirectoryRva();

    uint32_t preferred = parser.GetImageBase();
    bool hasRelocs = parser.GetDataDirectory(5).size != 0 && !(parser.GetCharacteristics() & 0x0001);
    uint32_t base = 0;
    if (mem_.IsRangeFree(preferred, m->size) && mem_.Map(preferred, m->size, cpu::kProtRWX)) {
        base = preferred;
    } else if (hasRelocs) {
        base = mem_.Allocate(m->size, cpu::kProtRWX, 0x00400000u, 0x7F000000u);
    }
    if (!base) {
        error = path + ": нельзя разместить по 0x" + std::to_string(preferred) +
                (hasRelocs ? " (нет места)" : " (адрес занят, а релокаций нет)");
        return nullptr;
    }
    m->base = base;

    m->image.assign(m->size, 0);
    parser.MapImageToMemory(m->image.data(), m->image.size());
    if (base != preferred) ApplyRelocations(m, parser, base - preferred);

    modules_.push_back(std::move(mod));
    ParseExports(m);
    ParseTls(m, parser);

    if (!BindImports(m, parser, error)) { m->initializationFailed=true; return nullptr; }

    mem_.LoadBytes(base, m->image.data(), m->image.size());
    if (cpu::g_consoleAvailable) {
        std::printf("[LOADER] %s -> 0x%08x (%u КиБ)%s\n", m->name.c_str(), base, m->size / 1024,
                    base != preferred ? " с релокацией" : "");
    }
    // Если поток уже работает (LoadLibrary во время исполнения) — TLS сразу
    if (threadReady_ && !SetupStaticTls(m,error)) { m->initializationFailed=true; return nullptr; }
    return m;
}

void Process::ApplyRelocations(Module* m, const pe::PEParser& parser, uint32_t delta) {
    pe::DataDirectory dir = parser.GetDataDirectory(5);
    uint32_t p = dir.virtualAddress, end = dir.virtualAddress + dir.size;
    while (p + 8 <= end) {
        uint32_t pageRva = Read32(m->image, p);
        uint32_t blockSize = Read32(m->image, p + 4);
        if (blockSize < 8) break;
        for (uint32_t off = 8; off + 2 <= blockSize; off += 2) {
            uint16_t e = Read16(m->image, p + off);
            uint32_t type = e >> 12, rva = pageRva + (e & 0xFFF);
            if (type == 3) Write32(m->image, rva, Read32(m->image, rva) + delta);          // HIGHLOW
            else if (type == 1) {                                                          // HIGH
                uint16_t v = Read16(m->image, rva);
                v = static_cast<uint16_t>(v + (delta >> 16));
                std::memcpy(m->image.data() + rva, &v, 2);
            } else if (type == 2) {                                                        // LOW
                uint16_t v = Read16(m->image, rva);
                v = static_cast<uint16_t>(v + (delta & 0xFFFF));
                std::memcpy(m->image.data() + rva, &v, 2);
            }
        }
        p += blockSize;
    }
}

void Process::ParseExports(Module* m) {
    if (m->image.size() < 0x40) return;
    uint32_t peOff = Read32(m->image, 0x3C);
    uint32_t dirRva = Read32(m->image, peOff + 0x78);
    uint32_t dirSize = Read32(m->image, peOff + 0x7C);
    if (!dirRva || static_cast<size_t>(dirRva) + 40 > m->image.size()) return;
    m->exportDirRva = dirRva;
    m->exportDirSize = dirSize;
    m->ordinalBase = Read32(m->image, dirRva + 16);
    uint32_t nFunc = Read32(m->image, dirRva + 20), nNames = Read32(m->image, dirRva + 24);
    uint32_t funcs = Read32(m->image, dirRva + 28), names = Read32(m->image, dirRva + 32), ords = Read32(m->image, dirRva + 36);
    if (nFunc > 65536) nFunc = 65536;
    m->exportRvas.resize(nFunc);
    for (uint32_t i = 0; i < nFunc; ++i) m->exportRvas[i] = Read32(m->image, funcs + i * 4);
    for (uint32_t i = 0; i < nNames && i < 65536; ++i) {
        std::string n = ReadStr(m->image, Read32(m->image, names + i * 4));
        uint16_t idx = Read16(m->image, ords + i * 2);
        m->exportByName[n] = idx;
    }
}

void Process::ParseTls(Module* m, const pe::PEParser& parser) {
    pe::DataDirectory dir = parser.GetDataDirectory(9);
    if (!dir.virtualAddress || dir.size < 24) return;
    // Поля TLS-каталога — абсолютные VA (уже с учётом релокаций)
    m->tlsTemplateStart = Read32(m->image, dir.virtualAddress + 0);
    m->tlsTemplateEnd = Read32(m->image, dir.virtualAddress + 4);
    uint32_t indexVa = Read32(m->image, dir.virtualAddress + 8);
    m->tlsCallbacks = Read32(m->image, dir.virtualAddress + 12);
    m->tlsZeroFill = Read32(m->image, dir.virtualAddress + 16);
    m->tlsIndex = nextTlsIndex_++;
    if (indexVa >= m->base && indexVa < m->base + m->size) Write32(m->image, indexVa - m->base, m->tlsIndex);
}

uint32_t Process::ResolveExport(Module* m, const std::string& name, uint16_t ordinal, int depth) {
    if (!m || depth > 8) return 0;
    if (m->hle) {
        pe::ImportedFunction f;
        f.name = name;
        f.ordinal = ordinal;
        f.iatRva = 0;
        // Ищем под каноничными именами DLL из таблиц ("KERNEL32.dll")
        int idx = name.empty() ? win32::FindHostFunctionByOrdinal(m->name.c_str(), ordinal)
                               : win32::FindHostFunction(m->name.c_str(), name.c_str());
        return idx >= 0 ? win32::IndexToHostAddress(idx) : 0;
    }
    uint32_t index;
    if (!name.empty()) {
        auto it = m->exportByName.find(name);
        if (it == m->exportByName.end()) return 0;
        index = it->second;
    } else {
        if (ordinal < m->ordinalBase) return 0;
        index = ordinal - m->ordinalBase;
    }
    if (index >= m->exportRvas.size()) return 0;
    uint32_t rva = m->exportRvas[index];
    if (!rva) return 0;
    if (rva >= m->exportDirRva && rva < m->exportDirRva + m->exportDirSize) {
        // Перенаправление: "DLL.Функция" или "DLL.#ординал"
        std::string fwd = ReadStr(m->image, rva);
        size_t dot = fwd.find('.');
        if (dot == std::string::npos) return 0;
        std::string err;
        Module* target = LoadLibrary(fwd.substr(0, dot), err);
        std::string fn = fwd.substr(dot + 1);
        if (!fn.empty() && fn[0] == '#') return ResolveExport(target, "", static_cast<uint16_t>(std::atoi(fn.c_str() + 1)), depth + 1);
        return ResolveExport(target, fn, 0, depth + 1);
    }
    return m->base + rva;
}

uint32_t Process::GetProcAddress(Module* m, const std::string& name, uint16_t ordinal) {
    LoaderLock lock(*this);
    if (!lock || !m || m->initializationFailed) return 0;
    return ResolveExport(m, name, ordinal, 0);
}

bool Process::BindImports(Module* m, const pe::PEParser& parser, std::string& error) {
    for (const auto& imp : parser.GetImports()) {
        std::string lower = NormalizeName(imp.dllName);
        Module* dep = nullptr;
        if (!IsAlwaysHle(lower)) {
            dep = FindByName(lower);
            if (!dep) {
                std::string path = FindFile(lower);
                if (!path.empty()) {
                    dep = LoadModuleFile(path, lower, error);
                    if (!dep) return false;
                }
            }
            if (dep && dep->hle) dep = nullptr;
        }
        if (dep) {
            if (std::find(m->dependencies.begin(), m->dependencies.end(), dep) == m->dependencies.end())
                m->dependencies.push_back(dep);
            for (const auto& f : imp.functions) {
                uint32_t addr = ResolveExport(dep, f.name, f.ordinal, 0);
                if (!addr) {
                    if (cpu::g_consoleAvailable)
                        std::printf("[LOADER] %s: в %s нет экспорта %s%s\n", m->name.c_str(), dep->name.c_str(),
                                    f.name.empty() ? "#" : "", f.name.empty() ? std::to_string(f.ordinal).c_str() : f.name.c_str());
                    // как HLE-заглушка: вызов будет виден в логе
                    bool impl = false;
                    addr = win32::ResolveHostImport(imp.dllName, f, crtGlobals_, impl);
                }
                Write32(m->image, f.iatRva, addr);
            }
        } else {
            // Эмулируемая DLL: HMODULE-заглушка для GetModuleHandle + host-адреса
            if (!FindByName(lower)) CreateHleModule(lower);
            for (const auto& f : imp.functions) {
                bool impl = false;
                uint32_t addr = win32::ResolveHostImport(imp.dllName, f, crtGlobals_, impl);
                Write32(m->image, f.iatRva, addr);
            }
        }
    }
    return true;
}

bool Process::SetupStaticTls(Module* m, std::string& error) {
    if (m->tlsIndex==0xffffffffu) return true;
    if (m->tlsIndex>=kTlsSlots) { error="static TLS index limit exceeded"; return false; }
    uint32_t tmplSize = m->tlsTemplateEnd > m->tlsTemplateStart ? m->tlsTemplateEnd - m->tlsTemplateStart : 0;
    if (tmplSize>0x1000000u || m->tlsZeroFill>0x1000000u-tmplSize) {
        error="static TLS template too large"; return false;
    }
    const uint32_t total=tmplSize+m->tlsZeroFill ? tmplSize+m->tlsZeroFill : 4u;
    const auto setup=[&](GuestThread& thread) {
        if (!thread.tlsArray || thread.memoryReleased || thread.status==GuestThreadStatus::Exited) return true;
        uint32_t block=mem_.Allocate(total,cpu::kProtRW);
        if (!block) { error="cannot allocate static TLS block"; return false; }
        thread.memoryRegions.emplace_back(block,total);
        for (uint32_t i=0;i<tmplSize;++i) mem_.Write8(block+i,mem_.Read8(m->tlsTemplateStart+i));
        mem_.Write32(thread.tlsArray+m->tlsIndex*4,block);
        return true;
    };
    if (!setup(mainThread_)) return false;
    for (auto& thread:workerThreads_) if (!setup(*thread)) return false;
    return true;
}

bool Process::PrepareMainThread(cpu::CpuState& cpu, std::string& error) {
    if (!main_) { error = "EXE не загружен"; return false; }

    // TEB и PEB по адресам Windows XP
    if (!mem_.Map(kTebAddress, 0x2000, cpu::kProtRW)) { error = "TEB/PEB: адрес занят"; return false; }

    pe::PEParser parser;
    parser.LoadFromFile(main_->path);
    uint32_t stackSize = parser.GetSizeOfStackReserve();
    if (stackSize < 0x40000) stackSize = 0x40000;
    if (stackSize > 0x1000000) stackSize = 0x1000000;
    uint32_t stackBase = mem_.Allocate(stackSize, cpu::kProtRW, 0x00010000u, 0x7F000000u);
    if (!stackBase) { error = "нет места под стек"; return false; }
    uint32_t stackTop = stackBase + stackSize;
    // TEB shares the initial mapping with process-owned PEB: unmap only TEB.
    mainThread_.memoryRegions.emplace_back(kTebAddress,0x1000u);
    mainThread_.memoryRegions.emplace_back(stackBase,stackSize);

    // Старые HLE-функции ожидают области CRT-глобалов, «состояния MFC» и
    // простой кучи (см. host_functions.h)
    crtGlobals_ = mem_.Allocate(win32::CRT_GLOBALS_SIZE, cpu::kProtRW);
    uint32_t mfcState = mem_.Allocate(win32::MFC_STATE_SIZE, cpu::kProtRW);
    uint32_t heap = mem_.Allocate(win32::HEAP_SIZE, cpu::kProtRW);
    tlsArray_ = mem_.Allocate(kTlsSlots * 4, cpu::kProtRW);
    if (!crtGlobals_ || !mfcState || !heap || !tlsArray_) { error = "нет места под служебные области"; return false; }
    mainThread_.memoryRegions.emplace_back(tlsArray_,kTlsSlots*4u);
    if (commandLine_.empty()) commandLine_ = main_->path;
    win32::InitCrtGlobals(mem_, crtGlobals_, commandLine_.c_str());
    win32::g_mfcStateBase = mfcState;
    win32::g_heapBase = heap;
    win32::g_heapNext = heap;
    win32::g_tebAddr = kTebAddress;

    // Импорты данных CRT (_acmdln и т.п.) указывали на crtGlobals_, которого
    // при загрузке ещё не было: перепривязываем все HLE-импорты.
    for (auto& mp : modules_) {
        Module* m = mp.get();
        if (m->hle || m->path.empty()) continue;
        pe::PEParser p2;
        if (p2.LoadFromFile(m->path) != pe::LoadResult::Ok) continue;
        for (const auto& imp : p2.GetImports()) {
            std::string lower = NormalizeName(imp.dllName);
            Module* dep = FindByName(lower);
            if (dep && !dep->hle) continue;
            for (const auto& f : imp.functions) {
                bool impl = false;
                uint32_t addr = win32::ResolveHostImport(imp.dllName, f, crtGlobals_, impl);
                mem_.Write32(m->base + f.iatRva, addr);
                Write32(m->image, f.iatRva, addr);
            }
        }
    }

    // TEB (NT_TIB + немного полей), PEB
    uint32_t t = kTebAddress;
    mem_.Write32(t + 0x00, win32::seh::kEndOfChain);  // FS:[0] guest SEH registration chain
    mem_.Write32(t + 0x04, stackTop);
    mem_.Write32(t + 0x08, stackBase);
    mem_.Write32(t + 0x18, t);            // Self
    mem_.Write32(t + 0x20, 0x1000);       // ClientId.UniqueProcess
    mem_.Write32(t + 0x24, 0x1004);       // ClientId.UniqueThread
    mem_.Write32(t + 0x2C, tlsArray_);    // ThreadLocalStoragePointer
    mem_.Write32(t + 0x30, kPebAddress);
    uint32_t p = kPebAddress;
    mem_.Write8(p + 0x02, 0);             // BeingDebugged
    mem_.Write32(p + 0x08, main_->base);  // ImageBaseAddress
    mem_.Write32(p + 0xA4, 5);            // OSMajorVersion (XP = 5.1)
    mem_.Write32(p + 0xA8, 1);            // OSMinorVersion
    mem_.Write16(p + 0xAC, 2600);         // OSBuildNumber
    mem_.Write32(p + 0xB0, 2);            // OSPlatformId (VER_PLATFORM_WIN32_NT)

    mainThread_.tlsArray=tlsArray_;
    for (auto& m : modules_) if (!SetupStaticTls(m.get(),error)) return false;
    threadReady_ = true;

    cpu = cpu::CpuState{};
    cpu.ESP() = stackTop - 16;
    cpu.ESP() -= 4;
    mem_.Write32(cpu.ESP(), EntryReturnAddress());
    cpu.eip = main_->base + main_->entryRva;
    mainThread_.id = 0x1004u;
    mainThread_.teb = kTebAddress;
    mainThread_.stackBase = stackTop;
    mainThread_.stackLimit = stackBase;
    mainThread_.tlsArray = tlsArray_;
    mainThread_.status = GuestThreadStatus::Runnable;
    mainThread_.cpu = cpu;
    if (!scheduler_.Add(mainThread_)) { error = "не удалось зарегистрировать главный поток"; return false; }
    return true;
}

GuestThread* Process::FindThread(uint32_t id) {
    if (mainThread_.id == id && id) return &mainThread_;
    for (auto& worker : workerThreads_)
        if (worker->id == id) return worker.get();
    return nullptr;
}

GuestThread* Process::CreateGuestThread(uint32_t startAddress, uint32_t parameter,
                                        uint32_t stackReserve, bool suspended,
                                        std::string& error) {
    if (!threadReady_ || !main_) {
        error = "guest process main thread is not ready";
        return nullptr;
    }
    if (!startAddress || !mem_.IsMapped(startAddress)) {
        error = "worker thread start address is not mapped";
        return nullptr;
    }
    if (nextThreadId_ >= 0xfffffff0u) {
        error = "guest thread ID exhausted";
        return nullptr;
    }
    if (!stackReserve) stackReserve = 0x100000u;
    if (stackReserve < 0x10000u) stackReserve = 0x10000u;
    if (stackReserve > 0x1000000u) {
        error = "guest worker stack reserve exceeds 16 MiB";
        return nullptr;
    }
    stackReserve = (stackReserve + 0xffffu) & ~0xffffu;
    // Keep TEB/PEB reservation for the first thread untouched.
    const uint32_t teb=mem_.Allocate(0x1000u,cpu::kProtRW);
    if (!teb) { error = "cannot allocate worker TEB"; return nullptr; }
    const uint32_t stack=mem_.Allocate(stackReserve,cpu::kProtRW,0x10000u,0x7f000000u);
    if (!stack) { mem_.Unmap(teb,0x1000u); error = "cannot allocate worker stack"; return nullptr; }
    const uint32_t tls=mem_.Allocate(kTlsSlots*4u,cpu::kProtRW);
    if (!tls) {
        mem_.Unmap(stack,stackReserve);
        mem_.Unmap(teb,0x1000u);
        error="cannot allocate worker TLS vector";
        return nullptr;
    }
    const uint32_t id=nextThreadId_;
    auto worker=std::unique_ptr<GuestThread>(new GuestThread());
    worker->id=id;
    worker->teb=teb;
    worker->stackLimit=stack;
    worker->stackBase=stack+stackReserve;
    worker->tlsArray=tls;
    worker->needsThreadAttach=true;
    worker->status=suspended?GuestThreadStatus::Suspended:GuestThreadStatus::Runnable;
    worker->suspendCount=suspended?1u:0u;
    worker->cpu=cpu::CpuState{};
    worker->cpu.eip=startAddress;
    worker->cpu.ESP()=worker->stackBase-16u;
    worker->cpu.ESP()-=4u;
    mem_.Write32(worker->cpu.ESP(),parameter);
    worker->cpu.ESP()-=4u;
    // A returning LPTHREAD_START_ROUTINE must terminate *only its thread*.
    // The dedicated HLE trampoline reads its DWORD result from EAX.
    const int returnIndex=win32::FindHostFunction(
        "KERNEL32.dll","__SwitchRecompilerWorkerReturn");
    if (returnIndex<0) {
        mem_.Unmap(tls,kTlsSlots*4u);
        mem_.Unmap(stack,stackReserve);
        mem_.Unmap(teb,0x1000u);
        error="worker return trampoline not registered";
        return nullptr;
    }
    mem_.Write32(worker->cpu.ESP(),win32::IndexToHostAddress(returnIndex));
    mem_.Write32(teb+0x00,win32::seh::kEndOfChain);
    mem_.Write32(teb+0x04,worker->stackBase);
    mem_.Write32(teb+0x08,worker->stackLimit);
    mem_.Write32(teb+0x18,teb);
    mem_.Write32(teb+0x20,0x1000u);
    mem_.Write32(teb+0x24,id);
    mem_.Write32(teb+0x2c,tls);
    mem_.Write32(teb+0x30,kPebAddress);
    // Each DLL with static TLS needs an independent initialized TLS block.
    std::vector<std::pair<uint32_t,uint32_t>> tlsBlocks;
    const auto cleanup = [&]() {
        for (const auto& item : tlsBlocks) mem_.Unmap(item.first,item.second);
        mem_.Unmap(tls,kTlsSlots*4u);
        mem_.Unmap(stack,stackReserve);
        mem_.Unmap(teb,0x1000u);
    };
    for (const auto& mod : modules_) {
        if (mod->tlsIndex==0xffffffffu || mod->tlsIndex>=kTlsSlots) continue;
        const uint32_t templateSize=mod->tlsTemplateEnd>mod->tlsTemplateStart
            ? mod->tlsTemplateEnd-mod->tlsTemplateStart:0u;
        if (templateSize > 0x1000000u ||
            mod->tlsZeroFill > 0x1000000u-templateSize) {
            error="worker TLS template too large";
            cleanup();
            return nullptr;
        }
        const uint32_t block=mem_.Allocate(templateSize+mod->tlsZeroFill?
                                           templateSize+mod->tlsZeroFill:4u,cpu::kProtRW);
        if(!block) {
            error="cannot allocate worker TLS block";
            cleanup();
            return nullptr;
        }
        tlsBlocks.emplace_back(block,templateSize+mod->tlsZeroFill?templateSize+mod->tlsZeroFill:4u);
        for(uint32_t i=0;i<templateSize;++i)
            mem_.Write8(block+i,mem_.Read8(mod->tlsTemplateStart+i));
        mem_.Write32(tls+mod->tlsIndex*4u,block);
    }
    GuestThread* result=worker.get();
    if(!scheduler_.Add(*result)) {
        error="cannot register worker with guest scheduler";
        cleanup();
        return nullptr;
    }
    worker->memoryRegions=std::move(tlsBlocks);
    worker->memoryRegions.emplace_back(tls,kTlsSlots*4u);
    worker->memoryRegions.emplace_back(stack,stackReserve);
    worker->memoryRegions.emplace_back(teb,0x1000u);
    workerThreads_.push_back(std::move(worker));
    nextThreadId_+=4u;
    return result;
}

void Process::ReleaseThreadMemory(GuestThread& thread) {
    if (thread.status!=GuestThreadStatus::Exited || thread.memoryReleased) return;
    for (const auto& region:thread.memoryRegions) mem_.Unmap(region.first,region.second);
    thread.memoryRegions.clear();
    thread.memoryReleased=true;
}

bool Process::CallTls(cpu::CpuState& state, Module& module, uint32_t reason) {
    if (!module.tlsCallbacks) return true;
    const uint32_t teb=CurrentTebAddress();
    for (uint32_t i=0;i<64;++i) {
        const uint32_t callback=mem_.Read32(module.tlsCallbacks+i*4);
        if (!callback) break;
        cpu::CpuState local=state;
        win32::CallGuestFunctionStdcall(local,mem_,callback,{module.base,reason,0},teb,teb);
        if (win32::g_guestExitRequested || !GuestExecution::Checkpoint()) return false;
    }
    return true;
}

bool Process::InitializeModule(cpu::CpuState& state, Module* module, std::string& error, uint32_t reserved) {
    if (module->initializationFailed) { error=module->name+": initialization failed"; return false; }
    if (module->initialized || module->initializing) return true;
    module->initializing=true;
    struct Reset { bool& flag; ~Reset() { flag=false; } } reset{module->initializing};
    for (Module* dependency:module->dependencies)
        if (!InitializeModule(state,dependency,error,reserved)) {
            module->initializationFailed=dependency->initializationFailed;
            return false;
        }
    if (!CallTls(state,*module,1)) { error="TLS initialization interrupted"; return false; }
    if (module->isDll && module->entryRva) {
        cpu::CpuState local=state;
        const uint32_t teb=CurrentTebAddress();
        const uint32_t ok=win32::CallGuestFunctionStdcall(local,mem_,module->base+module->entryRva,
            {module->base,1,reserved},teb,teb);
        if (win32::g_guestExitRequested || !GuestExecution::Checkpoint()) {
            error="DLL initialization interrupted"; return false;
        }
        if (!ok) {
            module->initializationFailed=true;
            error=module->name+": DllMain returned FALSE"; return false;
        }
    }
    module->initialized=true;
    if (!module->hle) initializationOrder_.push_back(module);
    return true;
}

bool Process::InitializeModules(cpu::CpuState& state, std::string& error) {
    LoaderLock lock(*this);
    if (!lock) { error="loader initialization interrupted"; return false; }
    // Indexed traversal remains valid if a callback loads another module.
    for (size_t i=0;i<modules_.size();++i) {
        Module* module=modules_[i].get();
        if (module!=main_ && !InitializeModule(state,module,error,1)) return false;
    }
    return main_ && InitializeModule(state,main_,error,1);
}

bool Process::InitializeNewModules(cpu::CpuState& state, std::string& error) {
    LoaderLock lock(*this);
    if (!lock) { error="loader initialization interrupted"; return false; }
    for (size_t i=0;i<modules_.size();++i) {
        Module* module=modules_[i].get();
        if (module==main_ || module->initializationFailed) continue;
        if (!InitializeModule(state,module,error)) return false;
    }
    return true;
}

bool Process::NotifyThread(GuestThread& thread, uint32_t reason) {
    LoaderLock lock(*this);
    if (!lock) return false;
    // A DLL loaded by this notification gets PROCESS_ATTACH on this thread,
    // never a retrospective THREAD_ATTACH. Detach visits current modules.
    auto order=initializationOrder_;
    if (reason==3) std::reverse(order.begin(),order.end());
    for (Module* module:order) {
        if (module->threadCallsDisabled) continue;
        if (!CallTls(thread.cpu,*module,reason)) return false;
        if (module->isDll && module->entryRva) {
            cpu::CpuState local=thread.cpu;
            win32::CallGuestFunctionStdcall(local,mem_,module->base+module->entryRva,
                {module->base,reason,0},thread.teb,thread.teb);
            if (win32::g_guestExitRequested || !GuestExecution::Checkpoint()) return false;
        }
    }
    return true;
}

} // namespace rt

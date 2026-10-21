// kernel32_module.cpp — KERNEL32: модули, выход из процесса, консоль, TLS.
//
// Работают поверх rt::Process: настоящие DLL гостя и эмулируемые системные
// DLL выглядят для программы одинаково (HMODULE, GetProcAddress по имени
// и ординалу, GetModuleFileName).

#include <cstdio>
#include <cstring>

#include "hle_util.h"
#include "seh_dispatch.h"
#include "../runtime/files.h"
#include "../runtime/process.h"
#include "../runtime/resources.h"

namespace win32 {

// Куда идёт вывод консольных программ гостя. По умолчанию — stdout/stderr
// хоста (если консоль доступна). main.cpp на Switch может подменить.
void (*g_consoleWriteImpl)(const char* data, size_t len, bool isStderr) = nullptr;

namespace {

using namespace hle;

constexpr uint32_t kStdInput = 0x03, kStdOutput = 0x07, kStdError = 0x0B;

void ConsoleWrite(const char* data, size_t len, bool err) {
    if (g_consoleWriteImpl) { g_consoleWriteImpl(data, len, err); return; }
    if (!cpu::g_consoleAvailable) return;
    std::fwrite(data, 1, len, err ? stderr : stdout);
    std::fflush(err ? stderr : stdout);
}

std::string GuestModulePath(const rt::Module* m) {
    if (m->hle) return "C:\\WINDOWS\\system32\\" + m->name;
    std::string base = m->path;
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    return "C:\\" + base;
}

rt::Module* ModuleFromHandle(uint32_t h) {
    if (!rt::g_process) return nullptr;
    if (h == 0) return rt::g_process->MainModule();
    return rt::g_process->FindByHandle(h);
}

// Без rt::Process (старые хостовые тесты) модулей нет: LoadLibrary даёт
// фиктивный хендл, GetProcAddress ищет HLE-функцию по имени в любой DLL.
constexpr uint32_t kLegacyModuleHandle = 0x69000000u;

HostCallOutcome LoadLibraryCommon(cpu::CpuState& c, cpu::IGuestMemory& m, const std::string& name) {
    if (!rt::g_process) return Ret(name.empty() ? 0 : kLegacyModuleHandle);
    if (name.empty()) { SetLastError(m, ERROR_MOD_NOT_FOUND); return Ret(0); }
    rt::Process::LoaderLock lock(*rt::g_process);
    if (!lock) return Ret(0);
    std::string err;
    rt::Module* mod = rt::g_process->LoadLibrary(name, err);
    if (!mod) {
        if (cpu::g_consoleAvailable) std::printf("[LoadLibrary] %s: %s\n", name.c_str(), err.c_str());
        SetLastError(m, ERROR_MOD_NOT_FOUND);
        return Ret(0);
    }
    if (!rt::g_process->InitializeNewModules(c, err)) {
        if (cpu::g_consoleAvailable) std::printf("[LoadLibrary] %s: %s\n", name.c_str(), err.c_str());
        SetLastError(m, 1114); // ERROR_DLL_INIT_FAILED
        return Ret(0);
    }
    return Ret(mod->base);
}

HostCallOutcome K32_LoadLibraryA(cpu::CpuState& c, cpu::IGuestMemory& m) { return LoadLibraryCommon(c, m, ReadAnsi(m, Arg(c, m, 0))); }
HostCallOutcome K32_LoadLibraryW(cpu::CpuState& c, cpu::IGuestMemory& m) { return LoadLibraryCommon(c, m, WideToUtf8(ReadWide(m, Arg(c, m, 0)))); }
HostCallOutcome K32_LoadLibraryExA(cpu::CpuState& c, cpu::IGuestMemory& m) { return LoadLibraryCommon(c, m, ReadAnsi(m, Arg(c, m, 0))); }
HostCallOutcome K32_LoadLibraryExW(cpu::CpuState& c, cpu::IGuestMemory& m) { return LoadLibraryCommon(c, m, WideToUtf8(ReadWide(m, Arg(c, m, 0)))); }

HostCallOutcome K32_FreeLibrary(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); } // модули не выгружаем

HostCallOutcome GetModuleHandleCommon(cpu::IGuestMemory& m, uint32_t namePtr, const std::string& name) {
    if (!rt::g_process) return Ret(g_imageBase);
    rt::Process::LoaderLock lock(*rt::g_process);
    if (!lock) return Ret(0);
    if (!namePtr) return Ret(rt::g_process->MainModule() ? rt::g_process->MainModule()->base : g_imageBase);
    rt::Module* mod = rt::g_process->FindByName(name);
    if (mod && mod->initializationFailed) mod=nullptr;
    if (!mod) {
        // Системные DLL «загружены всегда»
        std::string n = rt::Process::NormalizeName(name);
        if (rt::g_process->IsAlwaysHle(n)) {
            std::string err;
            mod = rt::g_process->LoadLibrary(n, err);
        }
    }
    if (!mod) { SetLastError(m, ERROR_MOD_NOT_FOUND); return Ret(0); }
    return Ret(mod->base);
}
HostCallOutcome K32_GetModuleHandleA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    return GetModuleHandleCommon(m, p, ReadAnsi(m, p));
}
HostCallOutcome K32_GetModuleHandleW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    return GetModuleHandleCommon(m, p, WideToUtf8(ReadWide(m, p)));
}
// BOOL GetModuleHandleEx(DWORD flags, LPCSTR name, HMODULE* out)
HostCallOutcome GetModuleHandleExCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    uint32_t flags = Arg(c, m, 0), name = Arg(c, m, 1), out = Arg(c, m, 2);
    uint32_t h = 0;
    if (flags & 4) { // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        rt::Module* mod = rt::g_process ? rt::g_process->FindByAddress(name) : nullptr;
        h = mod ? mod->base : 0;
    } else {
        h = GetModuleHandleCommon(m, name, wide ? WideToUtf8(ReadWide(m, name)) : ReadAnsi(m, name)).returnValue;
    }
    if (out) m.Write32(out, h);
    if (!h) { SetLastError(m, ERROR_MOD_NOT_FOUND); return Ret(0); }
    return Ret(1);
}
HostCallOutcome K32_GetModuleHandleExA(cpu::CpuState& c, cpu::IGuestMemory& m) { return GetModuleHandleExCommon(c, m, false); }
HostCallOutcome K32_GetModuleHandleExW(cpu::CpuState& c, cpu::IGuestMemory& m) { return GetModuleHandleExCommon(c, m, true); }

HostCallOutcome K32_GetProcAddress(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0), p = Arg(c, m, 1);
    if (!rt::g_process) {
        if (p < 0x10000) return Ret(0);
        std::string fn = ReadAnsi(m, p);
        // более поздние записи переопределяют ранние — ищем с конца
        for (int i = static_cast<int>(HostFunctionCount()) - 1; i >= 0; --i) {
            const HostFunctionEntry& e = GetHostFunction(i);
            if (e.funcName && fn == e.funcName) return Ret(IndexToHostAddress(i));
        }
        if (cpu::g_consoleAvailable) std::printf("[GetProcAddress] %s не найдена\n", fn.c_str());
        return Ret(0);
    }
    rt::Module* mod = ModuleFromHandle(h);
    if (!mod) {
        cpu::RecordDynamicImport("(invalid HMODULE)", p < 0x10000 ? "(ordinal)" : ReadAnsi(m, p).c_str(), 0);
        SetLastError(m, ERROR_INVALID_HANDLE);
        return Ret(0);
    }
    std::string procName = p < 0x10000 ? "#" + std::to_string(static_cast<uint16_t>(p)) : ReadAnsi(m, p);
    uint32_t addr = p < 0x10000 ? rt::g_process->GetProcAddress(mod, "", static_cast<uint16_t>(p))
                                : rt::g_process->GetProcAddress(mod, procName);
    cpu::RecordDynamicImport(mod->name.c_str(), procName.c_str(), addr);
    if (!addr) {
        if (cpu::g_consoleAvailable && p >= 0x10000)
            std::printf("[GetProcAddress] %s!%s не найдена\n", mod->name.c_str(), ReadAnsi(m, p).c_str());
        SetLastError(m, ERROR_PROC_NOT_FOUND);
    }
    return Ret(addr);
}

// DWORD GetModuleFileName(HMODULE, LPSTR, DWORD)
HostCallOutcome K32_GetModuleFileNameA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    rt::Module* mod = ModuleFromHandle(Arg(c, m, 0));
    uint32_t buf = Arg(c, m, 1), size = Arg(c, m, 2);
    if (!mod) { SetLastError(m, ERROR_MOD_NOT_FOUND); return Ret(0); }
    std::string path = GuestModulePath(mod);
    uint32_t n = WriteAnsi(m, buf, path, size);
    if (path.size() >= size) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(size); }
    return Ret(n);
}
HostCallOutcome K32_GetModuleFileNameW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    rt::Module* mod = ModuleFromHandle(Arg(c, m, 0));
    uint32_t buf = Arg(c, m, 1), size = Arg(c, m, 2);
    if (!mod) { SetLastError(m, ERROR_MOD_NOT_FOUND); return Ret(0); }
    std::u16string path = AnsiToWide(GuestModulePath(mod));
    uint32_t n = WriteWide(m, buf, path, size);
    if (path.size() >= size) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(size); }
    return Ret(n);
}

HostCallOutcome K32_DisableThreadLibraryCalls(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (!rt::g_process) { SetLastError(m,ERROR_INVALID_PARAMETER); return Ret(0); }
    rt::Process::LoaderLock lock(*rt::g_process);
    if (!lock) return Ret(0);
    auto* module=rt::g_process ? rt::g_process->FindByHandle(Arg(c,m,0)) : nullptr;
    if (!module || !module->isDll || module->tlsIndex!=0xffffffffu) {
        SetLastError(m,ERROR_INVALID_PARAMETER); return Ret(0);
    }
    module->threadCallsDisabled=true;
    return Ret(1);
}

HostCallOutcome K32_ExitProcess(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HostCallOutcome r;
    r.requestExit = true;
    r.exitCode = static_cast<int32_t>(Arg(c, m, 0));
    return r;
}

// HANDLE references are process-owned and independent of guest thread IDs.
rt::GuestThread* ThreadFromHandle(uint32_t handle) {
    if (!rt::g_process) return nullptr;
    if (handle == 0xFFFFFFFEu) return rt::g_process->Scheduler().Current()
        ? rt::g_process->Scheduler().Current() : &rt::g_process->MainThread();
    auto object = rt::Handles().Get(handle);
    if (!object || object->type != rt::ObjectKind::Thread) return nullptr;
    return rt::g_process->FindThread(static_cast<rt::ThreadObject&>(*object).threadId);
}
HostCallOutcome K32_CreateThread(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t stack=Arg(c,m,1), start=Arg(c,m,2);
    const uint32_t parameter=Arg(c,m,3), flags=Arg(c,m,4), idPtr=Arg(c,m,5);
    if (!rt::g_process || (flags & ~0x00000004u)) {
        SetLastError(m,ERROR_INVALID_PARAMETER);
        return Ret(0);
    }
    std::string error;
    rt::GuestThread* worker=rt::g_process->CreateGuestThread(
        start,parameter,stack,(flags & 0x4u)!=0,error);
    if (!worker) {
        SetLastError(m,ERROR_NOT_ENOUGH_MEMORY);
        return Ret(0);
    }
    if (idPtr && m.IsRangeMapped(idPtr,4)) m.Write32(idPtr,worker->id);
    uint32_t handle=rt::Handles().Add(std::make_shared<rt::ThreadObject>(worker->id));
    return Ret(handle);
}
HostCallOutcome K32_GetExitCodeThread(cpu::CpuState& c, cpu::IGuestMemory& m) {
    rt::GuestThread* t=ThreadFromHandle(Arg(c,m,0));
    uint32_t output=Arg(c,m,1);
    if (!t) { SetLastError(m,ERROR_INVALID_HANDLE); return Ret(0); }
    if (!output || !m.IsRangeMapped(output,4)) {
        SetLastError(m,ERROR_INVALID_PARAMETER);return Ret(0);
    }
    m.Write32(output,t->exitCode);return Ret(1);
}
HostCallOutcome K32_SuspendThread(cpu::CpuState& c, cpu::IGuestMemory& m) {
    rt::GuestThread* t=ThreadFromHandle(Arg(c,m,0));
    if(!t || t->status==rt::GuestThreadStatus::Exited) {
        SetLastError(m,ERROR_INVALID_HANDLE);return Ret(0xffffffffu);
    }
    return Ret(rt::g_process->Scheduler().Suspend(*t));
}
HostCallOutcome K32_ResumeThread(cpu::CpuState& c, cpu::IGuestMemory& m) {
    rt::GuestThread* t=ThreadFromHandle(Arg(c,m,0));
    if(!t || t->status==rt::GuestThreadStatus::Exited) {
        SetLastError(m,ERROR_INVALID_HANDLE);return Ret(0xffffffffu);
    }
    uint32_t previous=rt::g_process->Scheduler().Resume(*t);
    if(t->status==rt::GuestThreadStatus::Suspended && !t->suspendCount)
        t->status=rt::GuestThreadStatus::Runnable;
    return Ret(previous);
}
HostCallOutcome K32_WorkerReturn(cpu::CpuState& c, cpu::IGuestMemory&) {
    rt::GuestThread* t=rt::g_process ? rt::g_process->Scheduler().Current() : nullptr;
    if (!t || t==&rt::g_process->MainThread())
        cpu::FatalError("worker return trampoline used outside worker thread");
    rt::g_process->RequestThreadExit(*t,c.Get(cpu::Reg32::EAX));
    HostCallOutcome out;
    out.customReturn=true;
    return out;
}
HostCallOutcome K32_ExitThread(cpu::CpuState& c, cpu::IGuestMemory& m) {
    rt::GuestThread* t=rt::g_process ? rt::g_process->Scheduler().Current() : nullptr;
    if (!t) cpu::FatalError("ExitThread outside a scheduled guest thread");
    rt::g_process->RequestThreadExit(*t,Arg(c,m,0));
    HostCallOutcome out;
    out.customReturn=true; // nonreturning; outer loop sees Exited state
    return out;
}

// --- Консоль ---

HostCallOutcome K32_GetStdHandle(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int32_t which = static_cast<int32_t>(Arg(c, m, 0));
    if (which == -10) return Ret(kStdInput);
    if (which == -11) return Ret(kStdOutput);
    if (which == -12) return Ret(kStdError);
    SetLastError(m, ERROR_INVALID_HANDLE);
    return Ret(0xFFFFFFFFu);
}

}  // namespace

// Win32 HANDLE utilities implemented by the KERNEL32 thread layer.
rt::GuestThread* HleThreadFromHandle(uint32_t h) { return ThreadFromHandle(h); }

// Используется файловым вводом-выводом (kernel32_file.cpp) для консольных хендлов.
bool HleConsoleWrite(uint32_t handle, const char* data, size_t len) {
    auto file=rt::Files().Get(handle);
    if (!file || (file->kind!=rt::OpenFile::ConsoleOut &&
                  file->kind!=rt::OpenFile::ConsoleErr)) return false;
    ConsoleWrite(data, len, file->kind==rt::OpenFile::ConsoleErr);
    return true;
}
bool HleIsConsoleHandle(uint32_t handle) {
    auto file=rt::Files().Get(handle);
    return file && (file->kind==rt::OpenFile::ConsoleIn ||
                    file->kind==rt::OpenFile::ConsoleOut || file->kind==rt::OpenFile::ConsoleErr);
}

} // namespace win32

namespace rt {
void ConsoleWrite(bool isStderr, const char* data, size_t len) { win32::HleConsoleWrite(isStderr ? FileTable::kStdErr : FileTable::kStdOut, data, len); }
} // namespace rt

namespace win32 {

namespace {

// BOOL WriteConsoleA(HANDLE, const void*, DWORD n, LPDWORD written, LPVOID)
HostCallOutcome K32_WriteConsoleA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0), buf = Arg(c, m, 1), n = Arg(c, m, 2), written = Arg(c, m, 3);
    std::string s;
    s.resize(n);
    for (uint32_t i = 0; i < n; ++i) s[i] = static_cast<char>(m.Read8(buf + i));
    if (!HleConsoleWrite(h, s.data(), s.size())) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    if (written) m.Write32(written, n);
    return Ret(1);
}
HostCallOutcome K32_WriteConsoleW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0), buf = Arg(c, m, 1), n = Arg(c, m, 2), written = Arg(c, m, 3);
    std::u16string w;
    for (uint32_t i = 0; i < n; ++i) w.push_back(static_cast<char16_t>(m.Read16(buf + i * 2)));
    std::string s = WideToUtf8(w);
    if (!HleConsoleWrite(h, s.data(), s.size())) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    if (written) m.Write32(written, n);
    return Ret(1);
}
HostCallOutcome K32_GetConsoleMode(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0), out = Arg(c, m, 1);
    if (!HleIsConsoleHandle(h)) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    if (out) m.Write32(out, h == kStdInput ? 0x7u : 0x3u);
    return Ret(1);
}
HostCallOutcome K32_SetConsoleMode(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }
HostCallOutcome K32_GetConsoleCP(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(437); }
HostCallOutcome K32_GetConsoleOutputCP(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(437); }
HostCallOutcome K32_SetConsoleCtrlHandler(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }

// --- Последняя ошибка (в TEB) ---
HostCallOutcome K32_GetLastError(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(GetLastError(m)); }
HostCallOutcome K32_SetLastError(cpu::CpuState& c, cpu::IGuestMemory& m) { SetLastError(m, Arg(c, m, 0)); return Ret(0); }

// --- TLS: TEB+0xE10, 64 слота ---
bool g_tlsUsed[64] = {};
constexpr uint32_t kTlsSlotsOffset = 0xE10;

HostCallOutcome K32_TlsAlloc(cpu::CpuState&, cpu::IGuestMemory& m) {
    for (uint32_t i = 0; i < 64; ++i) {
        // слоты, занятые статическим TLS модулей, тут не пересекаются: это другой массив
        if (!g_tlsUsed[i]) {
            g_tlsUsed[i] = true;
            if (CurrentHleTeb()) m.Write32(CurrentHleTeb() + kTlsSlotsOffset + i * 4, 0);
            return Ret(i);
        }
    }
    SetLastError(m, ERROR_NOT_ENOUGH_MEMORY);
    return Ret(0xFFFFFFFFu);
}
HostCallOutcome K32_TlsFree(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t i = Arg(c, m, 0);
    if (i >= 64 || !g_tlsUsed[i]) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    g_tlsUsed[i] = false;
    return Ret(1);
}
HostCallOutcome K32_TlsGetValue(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t i = Arg(c, m, 0);
    if (i >= 64 || !g_tlsUsed[i]) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    SetLastError(m, ERROR_SUCCESS);
    return Ret(CurrentHleTeb() ? m.Read32(CurrentHleTeb() + kTlsSlotsOffset + i * 4) : 0);
}
HostCallOutcome K32_TlsSetValue(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t i = Arg(c, m, 0), v = Arg(c, m, 1);
    if (i >= 64 || !g_tlsUsed[i]) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    if (CurrentHleTeb()) m.Write32(CurrentHleTeb() + kTlsSlotsOffset + i * 4, v);
    return Ret(1);
}

// --- Ресурсы: HRSRC — адрес записи IMAGE_RESOURCE_DATA_ENTRY в памяти
// модуля, HGLOBAL — адрес самих данных (ресурсы в Windows не «грузятся»)

rt::ResId ResArg(cpu::IGuestMemory& m, uint32_t p, bool unicode) {
    if (p < 0x10000) return rt::ResId::Num(static_cast<uint16_t>(p));
    std::u16string s = unicode ? ReadWide(m, p) : AnsiToWide(ReadAnsi(m, p));
    if (!s.empty() && s[0] == '#') {
        uint32_t v = 0;
        for (size_t i = 1; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (s[i] - '0');
        return rt::ResId::Num(static_cast<uint16_t>(v));
    }
    for (char16_t& ch : s) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    return rt::ResId::Str(s);
}
HostCallOutcome FindResourceCommon(cpu::CpuState& c, cpu::IGuestMemory& m, uint32_t hmod, uint32_t name, uint32_t type,
                                   uint16_t lang, bool unicode) {
    rt::Module* mod = ModuleFromHandle(hmod);
    rt::ResourceData d;
    if (!mod || !rt::FindResource(mod, ResArg(m, type, unicode), ResArg(m, name, unicode), d, lang)) {
        SetLastError(m, 1813); // ERROR_RESOURCE_TYPE_NOT_FOUND
        return Ret(0);
    }
    (void)c;
    return Ret(mod->base + d.entryRva);
}
HostCallOutcome K32_FindResourceA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindResourceCommon(c, m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), 0, false); }
HostCallOutcome K32_FindResourceW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindResourceCommon(c, m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), 0, true); }
// FindResourceEx: (hModule, lpType, lpName, wLanguage) — тип и имя в другом порядке
HostCallOutcome K32_FindResourceExA(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindResourceCommon(c, m, Arg(c, m, 0), Arg(c, m, 2), Arg(c, m, 1), static_cast<uint16_t>(Arg(c, m, 3)), false); }
HostCallOutcome K32_FindResourceExW(cpu::CpuState& c, cpu::IGuestMemory& m) { return FindResourceCommon(c, m, Arg(c, m, 0), Arg(c, m, 2), Arg(c, m, 1), static_cast<uint16_t>(Arg(c, m, 3)), true); }
HostCallOutcome K32_LoadResource(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 1);
    rt::Module* mod = rt::g_process ? rt::g_process->FindByAddress(h) : nullptr;
    if (!mod) return Ret(0);
    return Ret(mod->base + m.Read32(h));
}
HostCallOutcome K32_LockResource(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(Arg(c, m, 0)); }
HostCallOutcome K32_SizeofResource(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 1);
    return Ret(h ? m.Read32(h + 4) : 0);
}
HostCallOutcome K32_FreeResource(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

// RtlUnwind on Windows XP x86 is exported by KERNEL32.
// A successful targeted unwind resumes at TargetIp instead of returning.
HostCallOutcome K32_RtlUnwind(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t targetFrame=Arg(c,m,0);
    const uint32_t targetIp=Arg(c,m,1);
    const uint32_t record=Arg(c,m,2);
    const uint32_t value=Arg(c,m,3);
    seh::UnwindResult result=seh::UnwindToFrame(
        c,m,g_tebAddr,targetFrame,targetIp,record,value);
    if(result!=seh::UnwindResult::Completed) {
        cpu::g_lastKnownEip=c.eip;
        cpu::FatalError(result==seh::UnwindResult::InvalidTarget ?
            "RtlUnwind: target frame or continuation is invalid" :
            result==seh::UnwindResult::InvalidChain ?
            "RtlUnwind: invalid guest SEH registration chain" :
            "RtlUnwind: nested/collided unwind requires support");
    }
    HostCallOutcome out;
    out.customReturn=true;
    return out;
}

const HostFunctionEntry kEntries[] = {
    {"KERNEL32.dll", "RtlUnwind", 16, CallConv::Stdcall, &K32_RtlUnwind},
    {"KERNEL32.dll", "FindResourceA", 12, CallConv::Stdcall, &K32_FindResourceA},
    {"KERNEL32.dll", "FindResourceW", 12, CallConv::Stdcall, &K32_FindResourceW},
    {"KERNEL32.dll", "FindResourceExA", 16, CallConv::Stdcall, &K32_FindResourceExA},
    {"KERNEL32.dll", "FindResourceExW", 16, CallConv::Stdcall, &K32_FindResourceExW},
    {"KERNEL32.dll", "LoadResource", 8, CallConv::Stdcall, &K32_LoadResource},
    {"KERNEL32.dll", "LockResource", 4, CallConv::Stdcall, &K32_LockResource},
    {"KERNEL32.dll", "SizeofResource", 8, CallConv::Stdcall, &K32_SizeofResource},
    {"KERNEL32.dll", "FreeResource", 4, CallConv::Stdcall, &K32_FreeResource},
    {"KERNEL32.dll", "LoadLibraryA", 4, CallConv::Stdcall, &K32_LoadLibraryA},
    {"KERNEL32.dll", "LoadLibraryW", 4, CallConv::Stdcall, &K32_LoadLibraryW},
    {"KERNEL32.dll", "LoadLibraryExA", 12, CallConv::Stdcall, &K32_LoadLibraryExA},
    {"KERNEL32.dll", "LoadLibraryExW", 12, CallConv::Stdcall, &K32_LoadLibraryExW},
    {"KERNEL32.dll", "FreeLibrary", 4, CallConv::Stdcall, &K32_FreeLibrary},
    {"KERNEL32.dll", "GetModuleHandleA", 4, CallConv::Stdcall, &K32_GetModuleHandleA},
    {"KERNEL32.dll", "GetModuleHandleW", 4, CallConv::Stdcall, &K32_GetModuleHandleW},
    {"KERNEL32.dll", "GetModuleHandleExA", 12, CallConv::Stdcall, &K32_GetModuleHandleExA},
    {"KERNEL32.dll", "GetModuleHandleExW", 12, CallConv::Stdcall, &K32_GetModuleHandleExW},
    {"KERNEL32.dll", "GetProcAddress", 8, CallConv::Stdcall, &K32_GetProcAddress},
    {"KERNEL32.dll", "GetModuleFileNameA", 12, CallConv::Stdcall, &K32_GetModuleFileNameA},
    {"KERNEL32.dll", "GetModuleFileNameW", 12, CallConv::Stdcall, &K32_GetModuleFileNameW},
    {"KERNEL32.dll", "DisableThreadLibraryCalls", 4, CallConv::Stdcall, &K32_DisableThreadLibraryCalls},
    {"KERNEL32.dll", "ExitProcess", 4, CallConv::Stdcall, &K32_ExitProcess},
    {"KERNEL32.dll", "CreateThread", 24, CallConv::Stdcall, &K32_CreateThread},
    {"KERNEL32.dll", "ExitThread", 4, CallConv::Stdcall, &K32_ExitThread},
    {"KERNEL32.dll", "__SwitchRecompilerWorkerReturn", 0, CallConv::Stdcall, &K32_WorkerReturn},
    {"KERNEL32.dll", "GetExitCodeThread", 8, CallConv::Stdcall, &K32_GetExitCodeThread},
    {"KERNEL32.dll", "SuspendThread", 4, CallConv::Stdcall, &K32_SuspendThread},
    {"KERNEL32.dll", "ResumeThread", 4, CallConv::Stdcall, &K32_ResumeThread},
    {"KERNEL32.dll", "GetStdHandle", 4, CallConv::Stdcall, &K32_GetStdHandle},
    {"KERNEL32.dll", "WriteConsoleA", 20, CallConv::Stdcall, &K32_WriteConsoleA},
    {"KERNEL32.dll", "WriteConsoleW", 20, CallConv::Stdcall, &K32_WriteConsoleW},
    {"KERNEL32.dll", "GetConsoleMode", 8, CallConv::Stdcall, &K32_GetConsoleMode},
    {"KERNEL32.dll", "SetConsoleMode", 8, CallConv::Stdcall, &K32_SetConsoleMode},
    {"KERNEL32.dll", "GetConsoleCP", 0, CallConv::Stdcall, &K32_GetConsoleCP},
    {"KERNEL32.dll", "GetConsoleOutputCP", 0, CallConv::Stdcall, &K32_GetConsoleOutputCP},
    {"KERNEL32.dll", "SetConsoleCtrlHandler", 8, CallConv::Stdcall, &K32_SetConsoleCtrlHandler},
    {"KERNEL32.dll", "GetLastError", 0, CallConv::Stdcall, &K32_GetLastError},
    {"KERNEL32.dll", "SetLastError", 4, CallConv::Stdcall, &K32_SetLastError},
    {"KERNEL32.dll", "TlsAlloc", 0, CallConv::Stdcall, &K32_TlsAlloc},
    {"KERNEL32.dll", "TlsFree", 4, CallConv::Stdcall, &K32_TlsFree},
    {"KERNEL32.dll", "TlsGetValue", 4, CallConv::Stdcall, &K32_TlsGetValue},
    {"KERNEL32.dll", "TlsSetValue", 8, CallConv::Stdcall, &K32_TlsSetValue},
};

const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace

} // namespace win32

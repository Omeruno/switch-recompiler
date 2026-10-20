// kernel32_module.cpp — KERNEL32: модули, выход из процесса, консоль, TLS.
//
// Работают поверх rt::Process: настоящие DLL гостя и эмулируемые системные
// DLL выглядят для программы одинаково (HMODULE, GetProcAddress по имени
// и ординалу, GetModuleFileName).

#include <cstdio>
#include <cstring>

#include "hle_util.h"
#include "../runtime/files.h"
#include "../runtime/process.h"

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

HostCallOutcome LoadLibraryCommon(cpu::CpuState& c, cpu::IGuestMemory& m, const std::string& name) {
    if (!rt::g_process || name.empty()) { SetLastError(m, ERROR_MOD_NOT_FOUND); return Ret(0); }
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
    if (!namePtr) return Ret(rt::g_process->MainModule() ? rt::g_process->MainModule()->base : g_imageBase);
    rt::Module* mod = rt::g_process->FindByName(name);
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
    rt::Module* mod = ModuleFromHandle(h);
    if (!mod) { SetLastError(m, ERROR_INVALID_HANDLE); return Ret(0); }
    uint32_t addr = p < 0x10000 ? rt::g_process->GetProcAddress(mod, "", static_cast<uint16_t>(p))
                                : rt::g_process->GetProcAddress(mod, ReadAnsi(m, p));
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

HostCallOutcome K32_DisableThreadLibraryCalls(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }

HostCallOutcome K32_ExitProcess(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HostCallOutcome r;
    r.requestExit = true;
    r.exitCode = static_cast<int32_t>(Arg(c, m, 0));
    return r;
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

bool IsConsoleOut(uint32_t h) { return h == kStdOutput || h == kStdError; }

}  // namespace

// Используется файловым вводом-выводом (kernel32_file.cpp) для консольных хендлов.
bool HleConsoleWrite(uint32_t handle, const char* data, size_t len) {
    if (!IsConsoleOut(handle)) return false;
    ConsoleWrite(data, len, handle == kStdError);
    return true;
}
bool HleIsConsoleHandle(uint32_t handle) { return handle == kStdInput || IsConsoleOut(handle); }

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
            if (g_tebAddr) m.Write32(g_tebAddr + kTlsSlotsOffset + i * 4, 0);
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
    if (i >= 64) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    SetLastError(m, ERROR_SUCCESS);
    return Ret(g_tebAddr ? m.Read32(g_tebAddr + kTlsSlotsOffset + i * 4) : 0);
}
HostCallOutcome K32_TlsSetValue(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t i = Arg(c, m, 0), v = Arg(c, m, 1);
    if (i >= 64) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    if (g_tebAddr) m.Write32(g_tebAddr + kTlsSlotsOffset + i * 4, v);
    return Ret(1);
}

const HostFunctionEntry kEntries[] = {
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

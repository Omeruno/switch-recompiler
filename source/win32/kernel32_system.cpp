// kernel32_system.cpp — KERNEL32: сведения о системе, кодовые страницы,
// окружение, командная строка, время, обработчик необработанных исключений.
//
// Изображаем Windows XP SP3 (5.1.2600), ANSI-кодовая страница 1252.

#include <chrono>
#include <ctime>
#include <map>
#include <string>

#include "hle_util.h"
#include "../runtime/clock.h"
#include "../runtime/process.h"

namespace win32 {

uint32_t HleProcessHeapHandle(cpu::IGuestMemory& m); // kernel32_memory.cpp

namespace {

using namespace hle;

// --- Кодовая страница 1252 (0x80..0x9F отличаются от Latin-1) ---
const uint16_t kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};
uint16_t Cp1252ToWide(uint8_t b) { return (b >= 0x80 && b < 0xA0) ? kCp1252High[b - 0x80] : b; }
int WideToCp1252(uint16_t w) {
    if (w < 0x80 || (w >= 0xA0 && w < 0x100)) return w;
    for (int i = 0; i < 32; ++i) if (kCp1252High[i] == w) return 0x80 + i;
    return -1;
}

std::u16string DecodeMb(uint32_t cp, const std::string& in) {
    std::u16string out;
    if (cp == 65001) { // UTF-8
        for (size_t i = 0; i < in.size();) {
            uint8_t b = static_cast<uint8_t>(in[i]);
            uint32_t c = 0xFFFD;
            size_t n = 1;
            if (b < 0x80) c = b;
            else if ((b >> 5) == 6 && i + 1 < in.size()) { c = ((b & 0x1F) << 6) | (in[i + 1] & 0x3F); n = 2; }
            else if ((b >> 4) == 14 && i + 2 < in.size()) { c = ((b & 0x0F) << 12) | ((in[i + 1] & 0x3F) << 6) | (in[i + 2] & 0x3F); n = 3; }
            else if ((b >> 3) == 30 && i + 3 < in.size()) { c = ((b & 0x07) << 18) | ((in[i + 1] & 0x3F) << 12) | ((in[i + 2] & 0x3F) << 6) | (in[i + 3] & 0x3F); n = 4; }
            if (c >= 0x10000) { c -= 0x10000; out.push_back(static_cast<char16_t>(0xD800 + (c >> 10))); out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF))); }
            else out.push_back(static_cast<char16_t>(c));
            i += n;
        }
        return out;
    }
    for (unsigned char ch : in) out.push_back(cp == 28591 ? ch : Cp1252ToWide(ch));
    return out;
}
std::string EncodeMb(uint32_t cp, const std::u16string& in, bool& usedDefault) {
    if (cp == 65001) return WideToUtf8(in);
    std::string out;
    for (char16_t w : in) {
        int b = WideToCp1252(w);
        if (b < 0) { b = '?'; usedDefault = true; }
        out.push_back(static_cast<char>(b));
    }
    return out;
}

// int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int cb, LPWSTR dst, int cch)
HostCallOutcome K32_MultiByteToWideChar(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cp = Arg(c, m, 0), src = Arg(c, m, 2), dst = Arg(c, m, 4);
    int32_t cb = static_cast<int32_t>(Arg(c, m, 3)), cch = static_cast<int32_t>(Arg(c, m, 5));
    std::string in;
    if (cb < 0) { in = ReadAnsi(m, src); in.push_back('\0'); }
    else for (int32_t i = 0; i < cb; ++i) in.push_back(static_cast<char>(m.Read8(src + i)));
    std::u16string w = DecodeMb(cp, in);
    if (cch == 0) return Ret(static_cast<uint32_t>(w.size()));
    if (static_cast<int32_t>(w.size()) > cch) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(0); }
    for (size_t i = 0; i < w.size(); ++i) m.Write16(dst + static_cast<uint32_t>(i * 2), w[i]);
    return Ret(static_cast<uint32_t>(w.size()));
}
// int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int cch, LPSTR dst, int cb, LPCSTR def, LPBOOL used)
HostCallOutcome K32_WideCharToMultiByte(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cp = Arg(c, m, 0), src = Arg(c, m, 2), dst = Arg(c, m, 4), usedPtr = Arg(c, m, 7);
    int32_t cch = static_cast<int32_t>(Arg(c, m, 3)), cb = static_cast<int32_t>(Arg(c, m, 5));
    std::u16string in;
    if (cch < 0) { in = ReadWide(m, src); in.push_back(0); }
    else for (int32_t i = 0; i < cch; ++i) in.push_back(static_cast<char16_t>(m.Read16(src + i * 2)));
    bool used = false;
    std::string out = EncodeMb(cp, in, used);
    if (usedPtr) m.Write32(usedPtr, used ? 1 : 0);
    if (cb == 0) return Ret(static_cast<uint32_t>(out.size()));
    if (static_cast<int32_t>(out.size()) > cb) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(0); }
    for (size_t i = 0; i < out.size(); ++i) m.Write8(dst + static_cast<uint32_t>(i), static_cast<uint8_t>(out[i]));
    return Ret(static_cast<uint32_t>(out.size()));
}
HostCallOutcome K32_GetACP(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1252); }
HostCallOutcome K32_GetOEMCP(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(437); }
HostCallOutcome K32_IsValidCodePage(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cp = Arg(c, m, 0);
    return Ret(cp == 1252 || cp == 437 || cp == 65001 || cp == 28591 || cp == 850 ? 1 : 0);
}
// BOOL GetCPInfo(UINT, LPCPINFO): MaxCharSize, DefaultChar[2], LeadByte[12]
HostCallOutcome K32_GetCPInfo(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t cp = Arg(c, m, 0), info = Arg(c, m, 1);
    m.Write32(info, cp == 65001 ? 4 : 1);
    m.Write8(info + 4, '?');
    m.Write8(info + 5, 0);
    for (int i = 0; i < 12; ++i) m.Write8(info + 6 + i, 0);
    return Ret(1);
}
HostCallOutcome K32_IsDBCSLeadByte(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

// --- Версия и система ---
HostCallOutcome K32_GetVersion(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0x0A280105u); } // 5.1 build 2600
HostCallOutcome GetVersionExCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    uint32_t p = Arg(c, m, 0);
    uint32_t size = m.Read32(p);
    m.Write32(p + 4, 5);
    m.Write32(p + 8, 1);
    m.Write32(p + 12, 2600);
    m.Write32(p + 16, 2); // VER_PLATFORM_WIN32_NT
    if (wide) WriteWide(m, p + 20, u"Service Pack 3", 128);
    else WriteAnsi(m, p + 20, "Service Pack 3", 128);
    uint32_t ext = wide ? 20 + 256 : 20 + 128;
    if (size >= ext + 8) { // OSVERSIONINFOEX
        m.Write16(p + ext, 3);     // wServicePackMajor
        m.Write16(p + ext + 2, 0);
        m.Write16(p + ext + 4, 0x100); // wSuiteMask: VER_SUITE_SINGLEUSERTS
        m.Write8(p + ext + 6, 1);  // wProductType: VER_NT_WORKSTATION
        m.Write8(p + ext + 7, 0);
    }
    return Ret(1);
}
HostCallOutcome K32_GetVersionExA(cpu::CpuState& c, cpu::IGuestMemory& m) { return GetVersionExCommon(c, m, false); }
HostCallOutcome K32_GetVersionExW(cpu::CpuState& c, cpu::IGuestMemory& m) { return GetVersionExCommon(c, m, true); }
HostCallOutcome K32_GetSystemInfo(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    m.Write32(p + 0, 0);           // PROCESSOR_ARCHITECTURE_INTEL
    m.Write32(p + 4, 0x1000);      // dwPageSize
    m.Write32(p + 8, 0x10000);     // lpMinimumApplicationAddress
    m.Write32(p + 12, 0x7FFEFFFFu);
    m.Write32(p + 16, 1);          // dwActiveProcessorMask
    m.Write32(p + 20, 1);          // dwNumberOfProcessors
    m.Write32(p + 24, 586);        // PROCESSOR_INTEL_PENTIUM
    m.Write32(p + 28, 0x10000);    // dwAllocationGranularity
    m.Write16(p + 32, 15);         // wProcessorLevel
    m.Write16(p + 34, 0x0209);     // wProcessorRevision
    return Ret(0);
}
HostCallOutcome K32_IsProcessorFeaturePresent(cpu::CpuState& c, cpu::IGuestMemory& m) {
    switch (Arg(c, m, 0)) {
        case 0:  // PF_FLOATING_POINT_PRECISION_ERRATA
        case 1:  // PF_FLOATING_POINT_EMULATED
            return Ret(0);
        case 2: case 3: case 6: case 7: case 8: case 10: // CX8, MMX, XMMI, 3DNOW(нет), RDTSC, XMMI64
            return Ret(Arg(c, m, 0) == 7 ? 0 : 1);
        default:
            return Ret(0);
    }
}
HostCallOutcome K32_IsDebuggerPresent(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }
HostCallOutcome K32_GetCurrentProcess(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0xFFFFFFFFu); }
HostCallOutcome K32_GetCurrentThread(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0xFFFFFFFEu); }
HostCallOutcome K32_GetCurrentProcessId(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0x1000); }
HostCallOutcome K32_GetCurrentThreadId(cpu::CpuState&, cpu::IGuestMemory&) {
    return Ret(rt::g_process ? rt::g_process->CurrentThreadId() : 0x1004u);
}

// --- Обработчик необработанных исключений ---
struct SystemState {
    uint32_t unhandledFilter=0,errorMode=0,cmdLineA=0,cmdLineW=0,flsNext=1;
    std::map<uint32_t,uint32_t> fls;
    std::map<std::string,std::string> env={
        {"PATH","C:\\WINDOWS\\system32;C:\\WINDOWS"},{"SYSTEMROOT","C:\\WINDOWS"},
        {"WINDIR","C:\\WINDOWS"},{"TEMP","C:\\TEMP"},{"TMP","C:\\TEMP"},
        {"USERNAME","Player"},{"COMPUTERNAME","SWITCH"},{"OS","Windows_NT"},
        {"NUMBER_OF_PROCESSORS","1"},{"PROCESSOR_ARCHITECTURE","x86"}
    };
};
SystemState& System() {return rt::LocalState<SystemState>();}

HostCallOutcome K32_SetUnhandledExceptionFilter(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t old = System().unhandledFilter;
    System().unhandledFilter = Arg(c, m, 0);
    return Ret(old);
}
HostCallOutcome K32_UnhandledExceptionFilter(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); } // EXCEPTION_EXECUTE_HANDLER
HostCallOutcome K32_SetErrorMode(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto& mode=System().errorMode;
    uint32_t old = mode;
    mode = Arg(c, m, 0);
    return Ret(old);
}

// Указатели EncodePointer/DecodePointer (XP SP2+): кодирование — XOR с
// константой, как это нужно CRT (лишь бы Decode(Encode(p)) == p).
HostCallOutcome K32_EncodePointer(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(Arg(c, m, 0) ^ 0x5A5A5A5Au); }
HostCallOutcome K32_DecodePointer(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(Arg(c, m, 0) ^ 0x5A5A5A5Au); }

// --- Командная строка и окружение ---

uint32_t AllocGuest(cpu::IGuestMemory& m, uint32_t size) {
    return m.Allocate(size ? size : 1, cpu::kProtRW);
}
std::string CommandLineString() {
    if (!rt::g_process) return "app.exe";
    // путь EXE в командной строке — как его видит гость (C:\name.exe)
    std::string cmd = rt::g_process->CommandLine();
    std::string exe = rt::g_process->MainModule() ? rt::g_process->MainModule()->path : "";
    if (!exe.empty() && cmd.compare(0, exe.size(), exe) == 0) {
        std::string base = exe.substr(exe.find_last_of("/\\") + 1);
        cmd = "\"C:\\" + base + "\"" + cmd.substr(exe.size());
    }
    return cmd;
}
HostCallOutcome K32_GetCommandLineA(cpu::CpuState&, cpu::IGuestMemory& m) {
    if (!System().cmdLineA) {
        std::string s = CommandLineString();
        System().cmdLineA = AllocGuest(m, static_cast<uint32_t>(s.size() + 1));
        WriteAnsi(m, System().cmdLineA, s, static_cast<uint32_t>(s.size() + 1));
    }
    return Ret(System().cmdLineA);
}
HostCallOutcome K32_GetCommandLineW(cpu::CpuState&, cpu::IGuestMemory& m) {
    if (!System().cmdLineW) {
        std::u16string s = AnsiToWide(CommandLineString());
        System().cmdLineW = AllocGuest(m, static_cast<uint32_t>((s.size() + 1) * 2));
        WriteWide(m, System().cmdLineW, s, static_cast<uint32_t>(s.size() + 1));
    }
    return Ret(System().cmdLineW);
}

std::map<std::string,std::string>& Env() {return System().env;}

std::string UpperKey(std::string k) { for (char& ch : k) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32); return k; }

HostCallOutcome K32_GetEnvironmentStringsA(cpu::CpuState&, cpu::IGuestMemory& m) {
    std::string block;
    for (auto& kv : Env()) { block += kv.first + "=" + kv.second; block.push_back('\0'); }
    block.push_back('\0');
    uint32_t p = AllocGuest(m, static_cast<uint32_t>(block.size()));
    for (size_t i = 0; i < block.size(); ++i) m.Write8(p + static_cast<uint32_t>(i), static_cast<uint8_t>(block[i]));
    return Ret(p);
}
HostCallOutcome K32_GetEnvironmentStringsW(cpu::CpuState&, cpu::IGuestMemory& m) {
    std::u16string block;
    for (auto& kv : Env()) { block += AnsiToWide(kv.first + "=" + kv.second); block.push_back(0); }
    block.push_back(0);
    uint32_t p = AllocGuest(m, static_cast<uint32_t>(block.size() * 2));
    for (size_t i = 0; i < block.size(); ++i) m.Write16(p + static_cast<uint32_t>(i * 2), block[i]);
    return Ret(p);
}
HostCallOutcome K32_FreeEnvironmentStrings(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    uint32_t base = m.RegionBase(p);
    if (base == p) m.Unmap(base, m.RegionSize(base));
    return Ret(1);
}
// DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buf, DWORD size)
HostCallOutcome K32_GetEnvironmentVariableA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto it = Env().find(UpperKey(ReadAnsi(m, Arg(c, m, 0))));
    if (it == Env().end()) { SetLastError(m, 203); return Ret(0); } // ERROR_ENVVAR_NOT_FOUND
    uint32_t size = Arg(c, m, 2);
    if (it->second.size() + 1 > size) return Ret(static_cast<uint32_t>(it->second.size() + 1));
    return Ret(WriteAnsi(m, Arg(c, m, 1), it->second, size));
}
HostCallOutcome K32_GetEnvironmentVariableW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto it = Env().find(UpperKey(WideToUtf8(ReadWide(m, Arg(c, m, 0)))));
    if (it == Env().end()) { SetLastError(m, 203); return Ret(0); }
    uint32_t size = Arg(c, m, 2);
    if (it->second.size() + 1 > size) return Ret(static_cast<uint32_t>(it->second.size() + 1));
    return Ret(WriteWide(m, Arg(c, m, 1), AnsiToWide(it->second), size));
}
HostCallOutcome K32_SetEnvironmentVariableA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string k = UpperKey(ReadAnsi(m, Arg(c, m, 0)));
    if (Arg(c, m, 1)) Env()[k] = ReadAnsi(m, Arg(c, m, 1)); else Env().erase(k);
    return Ret(1);
}
HostCallOutcome K32_SetEnvironmentVariableW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string k = UpperKey(WideToUtf8(ReadWide(m, Arg(c, m, 0))));
    if (Arg(c, m, 1)) Env()[k] = WideToUtf8(ReadWide(m, Arg(c, m, 1))); else Env().erase(k);
    return Ret(1);
}

HostCallOutcome GetStartupInfoCommon(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    for (uint32_t i = 0; i < 68; i += 4) m.Write32(p + i, 0);
    m.Write32(p, 68);
    m.Write32(p + 56, 0x03); // hStdInput
    m.Write32(p + 60, 0x07); // hStdOutput
    m.Write32(p + 64, 0x0B); // hStdError
    return Ret(0);
}

// --- Время ---
// FILETIME: сотни наносекунд с 1601-01-01
uint64_t NowFileTime() { return rt::Clock::FileTime(); }
uint32_t TickCount() { return rt::Clock::NowMs(); }
HostCallOutcome K32_GetTickCount(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(g_getTickCountImpl ? g_getTickCountImpl() : TickCount()); }
HostCallOutcome K32_QueryPerformanceFrequency(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    m.Write32(p, 10000000);
    m.Write32(p + 4, 0);
    return Ret(1);
}
HostCallOutcome K32_QueryPerformanceCounter(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint64_t t = rt::Clock::NowUs() * 10; // частота 10 МГц
    uint32_t p = Arg(c, m, 0);
    m.Write32(p, static_cast<uint32_t>(t));
    m.Write32(p + 4, static_cast<uint32_t>(t >> 32));
    return Ret(1);
}
HostCallOutcome K32_GetSystemTimeAsFileTime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint64_t t = NowFileTime();
    uint32_t p = Arg(c, m, 0);
    m.Write32(p, static_cast<uint32_t>(t));
    m.Write32(p + 4, static_cast<uint32_t>(t >> 32));
    return Ret(0);
}
void WriteSystemTime(cpu::IGuestMemory& m, uint32_t p, const std::tm& t, uint32_t ms) {
    m.Write16(p + 0, static_cast<uint16_t>(t.tm_year + 1900));
    m.Write16(p + 2, static_cast<uint16_t>(t.tm_mon + 1));
    m.Write16(p + 4, static_cast<uint16_t>(t.tm_wday));
    m.Write16(p + 6, static_cast<uint16_t>(t.tm_mday));
    m.Write16(p + 8, static_cast<uint16_t>(t.tm_hour));
    m.Write16(p + 10, static_cast<uint16_t>(t.tm_min));
    m.Write16(p + 12, static_cast<uint16_t>(t.tm_sec));
    m.Write16(p + 14, static_cast<uint16_t>(ms));
}
HostCallOutcome GetTimeCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool local) {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);
    uint32_t ms = static_cast<uint32_t>(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm t{};
#if defined(_WIN32)
    if (local) localtime_s(&t, &tt); else gmtime_s(&t, &tt);
#else
    if (local) localtime_r(&tt, &t); else gmtime_r(&tt, &t);
#endif
    WriteSystemTime(m, Arg(c, m, 0), t, ms);
    return Ret(0);
}
HostCallOutcome K32_GetSystemTime(cpu::CpuState& c, cpu::IGuestMemory& m) { return GetTimeCommon(c, m, false); }
HostCallOutcome K32_GetLocalTime(cpu::CpuState& c, cpu::IGuestMemory& m) { return GetTimeCommon(c, m, true); }
// BOOL FileTimeToSystemTime(const FILETIME*, LPSYSTEMTIME)
HostCallOutcome K32_FileTimeToSystemTime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t ft = Arg(c, m, 0);
    uint64_t v = m.Read32(ft) | (static_cast<uint64_t>(m.Read32(ft + 4)) << 32);
    std::time_t tt = static_cast<std::time_t>((v - 116444736000000000ull) / 10000000ull);
    std::tm t{};
#if defined(_WIN32)
    gmtime_s(&t, &tt);
#else
    gmtime_r(&tt, &t);
#endif
    WriteSystemTime(m, Arg(c, m, 1), t, static_cast<uint32_t>((v / 10000) % 1000));
    return Ret(1);
}
HostCallOutcome K32_SystemTimeToFileTime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t st = Arg(c, m, 0), ft = Arg(c, m, 1);
    std::tm t{};
    t.tm_year = m.Read16(st) - 1900;
    t.tm_mon = m.Read16(st + 2) - 1;
    t.tm_mday = m.Read16(st + 6);
    t.tm_hour = m.Read16(st + 8);
    t.tm_min = m.Read16(st + 10);
    t.tm_sec = m.Read16(st + 12);
    // timegm: дни от эпохи по формуле, без зависимости от часового пояса хоста
    int y = t.tm_year + 1900, mo = t.tm_mon + 1;
    int a = (14 - mo) / 12, yy = y + 4800 - a, mm = mo + 12 * a - 3;
    int64_t jdn = t.tm_mday + (153 * mm + 2) / 5 + 365LL * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    int64_t days = jdn - 2440588;
    int64_t secs = days * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    uint64_t v = static_cast<uint64_t>(secs) * 10000000ull + 116444736000000000ull + m.Read16(st + 14) * 10000ull;
    m.Write32(ft, static_cast<uint32_t>(v));
    m.Write32(ft + 4, static_cast<uint32_t>(v >> 32));
    return Ret(1);
}
HostCallOutcome K32_FileTimeToLocalFileTime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t a = Arg(c, m, 0), b = Arg(c, m, 1);
    m.Write32(b, m.Read32(a));
    m.Write32(b + 4, m.Read32(a + 4));
    return Ret(1);
}
HostCallOutcome K32_GetTimeZoneInformation(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    for (uint32_t i = 0; i < 172; i += 4) m.Write32(p + i, 0);
    WriteWide(m, p + 4, u"Coordinated Universal Time", 32);
    return Ret(0); // TIME_ZONE_ID_UNKNOWN
}

HostCallOutcome K32_OutputDebugStringA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (cpu::g_consoleAvailable) std::printf("[OutputDebugString] %s", ReadAnsi(m, Arg(c, m, 0)).c_str());
    return Ret(0);
}
HostCallOutcome K32_OutputDebugStringW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (cpu::g_consoleAvailable) std::printf("[OutputDebugString] %s", WideToUtf8(ReadWide(m, Arg(c, m, 0))).c_str());
    return Ret(0);
}
HostCallOutcome K32_Beep(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }

// --- Локаль: ASCII-уровень, en-US ---
HostCallOutcome K32_GetUserDefaultLCID(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0x0409); }
HostCallOutcome K32_GetUserDefaultLangID(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0x0409); }
// BOOL GetStringTypeW(DWORD type, LPCWSTR, int n, LPWORD out): только CT_CTYPE1
HostCallOutcome K32_GetStringTypeW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t type = Arg(c, m, 0), s = Arg(c, m, 1), out = Arg(c, m, 3);
    int32_t n = static_cast<int32_t>(Arg(c, m, 2));
    if (n < 0) n = static_cast<int32_t>(ReadWide(m, s).size()) + 1;
    for (int32_t i = 0; i < n; ++i) {
        uint16_t ch = m.Read16(s + i * 2), t = 0;
        if (type == 1) {
            if (ch >= 'A' && ch <= 'Z') t |= 0x001 | 0x100;
            if (ch >= 'a' && ch <= 'z') t |= 0x002 | 0x100;
            if (ch >= '0' && ch <= '9') t |= 0x004;
            if (ch == ' ' || (ch >= 9 && ch <= 13)) t |= 0x008;
            if (ch == ' ' || ch == '\t') t |= 0x040;
            if (ch < 32 || ch == 127) t |= 0x020;
            if (ch > 32 && ch < 127 && !(t & 0x107)) t |= 0x010;
            if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) t |= 0x080;
            if (ch >= 0xC0 && ch <= 0x24F) t |= 0x100;
        }
        m.Write16(out + i * 2, t);
    }
    return Ret(1);
}
HostCallOutcome K32_GetStringTypeA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    // GetStringTypeA(LCID, DWORD type, LPCSTR, int, LPWORD)
    uint32_t type = Arg(c, m, 1), s = Arg(c, m, 2), out = Arg(c, m, 4);
    int32_t n = static_cast<int32_t>(Arg(c, m, 3));
    if (n < 0) n = static_cast<int32_t>(ReadAnsi(m, s).size()) + 1;
    for (int32_t i = 0; i < n; ++i) {
        uint16_t ch = m.Read8(s + i), t = 0;
        if (type == 1) {
            if (ch >= 'A' && ch <= 'Z') t |= 0x101;
            if (ch >= 'a' && ch <= 'z') t |= 0x102;
            if (ch >= '0' && ch <= '9') t |= 0x084;
            if (ch == ' ' || (ch >= 9 && ch <= 13)) t |= 0x008;
            if (ch == ' ' || ch == '\t') t |= 0x040;
            if (ch < 32 || ch == 127) t |= 0x020;
            if (ch > 32 && ch < 127 && !(t & 0x107)) t |= 0x010;
            if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) t |= 0x080;
        }
        m.Write16(out + i * 2, t);
    }
    return Ret(1);
}
// int LCMapStringW(LCID, DWORD flags, LPCWSTR src, int n, LPWSTR dst, int cch): LCMAP_LOWERCASE/UPPERCASE
HostCallOutcome K32_LCMapStringW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t flags = Arg(c, m, 1), src = Arg(c, m, 2), dst = Arg(c, m, 4);
    int32_t n = static_cast<int32_t>(Arg(c, m, 3)), cch = static_cast<int32_t>(Arg(c, m, 5));
    if (n < 0) n = static_cast<int32_t>(ReadWide(m, src).size()) + 1;
    if (cch == 0) return Ret(static_cast<uint32_t>(n));
    if (cch < n) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(0); }
    for (int32_t i = 0; i < n; ++i) {
        uint16_t ch = m.Read16(src + i * 2);
        if ((flags & 0x100) && ch >= 'A' && ch <= 'Z') ch = static_cast<uint16_t>(ch + 32);
        if ((flags & 0x200) && ch >= 'a' && ch <= 'z') ch = static_cast<uint16_t>(ch - 32);
        m.Write16(dst + i * 2, ch);
    }
    return Ret(static_cast<uint32_t>(n));
}
HostCallOutcome K32_LCMapStringA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t flags = Arg(c, m, 1), src = Arg(c, m, 2), dst = Arg(c, m, 4);
    int32_t n = static_cast<int32_t>(Arg(c, m, 3)), cch = static_cast<int32_t>(Arg(c, m, 5));
    if (n < 0) n = static_cast<int32_t>(ReadAnsi(m, src).size()) + 1;
    if (cch == 0) return Ret(static_cast<uint32_t>(n));
    if (cch < n) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(0); }
    for (int32_t i = 0; i < n; ++i) {
        uint8_t ch = m.Read8(src + i);
        if ((flags & 0x100) && ch >= 'A' && ch <= 'Z') ch = static_cast<uint8_t>(ch + 32);
        if ((flags & 0x200) && ch >= 'a' && ch <= 'z') ch = static_cast<uint8_t>(ch - 32);
        m.Write8(dst + i, ch);
    }
    return Ret(static_cast<uint32_t>(n));
}
// int CompareStringA/W(LCID, DWORD flags, s1, n1, s2, n2): 1 <, 2 =, 3 >
HostCallOutcome CompareCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    uint32_t flags = Arg(c, m, 1), s1 = Arg(c, m, 2), s2 = Arg(c, m, 4);
    int32_t n1 = static_cast<int32_t>(Arg(c, m, 3)), n2 = static_cast<int32_t>(Arg(c, m, 5));
    std::u16string a = wide ? ReadWide(m, s1) : AnsiToWide(ReadAnsi(m, s1));
    std::u16string b = wide ? ReadWide(m, s2) : AnsiToWide(ReadAnsi(m, s2));
    if (n1 >= 0 && static_cast<size_t>(n1) < a.size()) a.resize(n1);
    if (n2 >= 0 && static_cast<size_t>(n2) < b.size()) b.resize(n2);
    if (flags & 1) { // NORM_IGNORECASE
        for (auto& ch : a) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
        for (auto& ch : b) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    }
    int r = a.compare(b);
    return Ret(r < 0 ? 1 : r == 0 ? 2 : 3);
}
HostCallOutcome K32_CompareStringA(cpu::CpuState& c, cpu::IGuestMemory& m) { return CompareCommon(c, m, false); }
HostCallOutcome K32_CompareStringW(cpu::CpuState& c, cpu::IGuestMemory& m) { return CompareCommon(c, m, true); }
// int GetLocaleInfoA/W(LCID, LCTYPE, LPSTR, int)
HostCallOutcome LocaleInfoCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    uint32_t type = Arg(c, m, 1) & 0x0FFFFFFF, buf = Arg(c, m, 2), cch = Arg(c, m, 3);
    std::string v;
    switch (type) {
        case 0x0E: v = "."; break;          // LOCALE_SDECIMAL
        case 0x0F: v = ","; break;          // LOCALE_STHOUSAND
        case 0x10: v = "3;0"; break;        // LOCALE_SGROUPING
        case 0x14: v = "$"; break;          // LOCALE_SCURRENCY
        case 0x1004: v = "1252"; break;     // LOCALE_IDEFAULTANSICODEPAGE
        case 0x0B: v = "437"; break;        // LOCALE_IDEFAULTCODEPAGE
        case 0x59: v = "en"; break;         // LOCALE_SISO639LANGNAME
        case 0x5A: v = "US"; break;         // LOCALE_SISO3166CTRYNAME
        case 0x01: v = "0409"; break;       // LOCALE_ILANGUAGE
        case 0x02: v = "English (United States)"; break;
        case 0x1F: v = "M/d/yyyy"; break;   // LOCALE_SSHORTDATE
        case 0x20: v = "dddd, MMMM dd, yyyy"; break;
        case 0x1003: v = "h:mm:ss tt"; break;
        default: v = ""; break;
    }
    if (cch == 0) return Ret(static_cast<uint32_t>(v.size() + 1));
    if (wide) WriteWide(m, buf, AnsiToWide(v), cch); else WriteAnsi(m, buf, v, cch);
    return Ret(static_cast<uint32_t>(v.size() + 1));
}
HostCallOutcome K32_GetLocaleInfoA(cpu::CpuState& c, cpu::IGuestMemory& m) { return LocaleInfoCommon(c, m, false); }
HostCallOutcome K32_GetLocaleInfoW(cpu::CpuState& c, cpu::IGuestMemory& m) { return LocaleInfoCommon(c, m, true); }

// --- FLS (Vista+, но CRT ищет через GetProcAddress): как TLS ---
std::map<uint32_t,uint32_t>& Fls() {return System().fls;}

HostCallOutcome K32_FlsAlloc(cpu::CpuState&, cpu::IGuestMemory&) { Fls()[System().flsNext] = 0; return Ret(System().flsNext++); }
HostCallOutcome K32_FlsFree(cpu::CpuState& c, cpu::IGuestMemory& m) { Fls().erase(Arg(c, m, 0)); return Ret(1); }
HostCallOutcome K32_FlsGetValue(cpu::CpuState& c, cpu::IGuestMemory& m) { SetLastError(m, 0); return Ret(Fls()[Arg(c, m, 0)]); }
HostCallOutcome K32_FlsSetValue(cpu::CpuState& c, cpu::IGuestMemory& m) { Fls()[Arg(c, m, 0)] = Arg(c, m, 1); return Ret(1); }

HostCallOutcome K32_GetSystemDirectoryA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = "C:\\WINDOWS\\system32";
    uint32_t cap = Arg(c, m, 1);
    if (cap <= s.size()) return Ret(static_cast<uint32_t>(s.size() + 1));
    return Ret(WriteAnsi(m, Arg(c, m, 0), s, cap));
}
HostCallOutcome K32_GetSystemDirectoryW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::u16string s = u"C:\\WINDOWS\\system32";
    uint32_t cap = Arg(c, m, 1);
    if (cap <= s.size()) return Ret(static_cast<uint32_t>(s.size() + 1));
    return Ret(WriteWide(m, Arg(c, m, 0), s, cap));
}
HostCallOutcome K32_GetWindowsDirectoryA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = "C:\\WINDOWS";
    uint32_t cap = Arg(c, m, 1);
    if (cap <= s.size()) return Ret(static_cast<uint32_t>(s.size() + 1));
    return Ret(WriteAnsi(m, Arg(c, m, 0), s, cap));
}
HostCallOutcome K32_GetWindowsDirectoryW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::u16string s = u"C:\\WINDOWS";
    uint32_t cap = Arg(c, m, 1);
    if (cap <= s.size()) return Ret(static_cast<uint32_t>(s.size() + 1));
    return Ret(WriteWide(m, Arg(c, m, 0), s, cap));
}
HostCallOutcome K32_GetComputerNameA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t buf = Arg(c, m, 0), sizePtr = Arg(c, m, 1);
    uint32_t cap = m.Read32(sizePtr);
    std::string n = "SWITCH";
    if (cap <= n.size()) { m.Write32(sizePtr, static_cast<uint32_t>(n.size() + 1)); SetLastError(m, 111); return Ret(0); }
    m.Write32(sizePtr, WriteAnsi(m, buf, n, cap));
    return Ret(1);
}

const HostFunctionEntry kEntries[] = {
    {"KERNEL32.dll", "MultiByteToWideChar", 24, CallConv::Stdcall, &K32_MultiByteToWideChar},
    {"KERNEL32.dll", "WideCharToMultiByte", 32, CallConv::Stdcall, &K32_WideCharToMultiByte},
    {"KERNEL32.dll", "GetACP", 0, CallConv::Stdcall, &K32_GetACP},
    {"KERNEL32.dll", "GetOEMCP", 0, CallConv::Stdcall, &K32_GetOEMCP},
    {"KERNEL32.dll", "IsValidCodePage", 4, CallConv::Stdcall, &K32_IsValidCodePage},
    {"KERNEL32.dll", "GetCPInfo", 8, CallConv::Stdcall, &K32_GetCPInfo},
    {"KERNEL32.dll", "IsDBCSLeadByte", 4, CallConv::Stdcall, &K32_IsDBCSLeadByte},
    {"KERNEL32.dll", "IsDBCSLeadByteEx", 8, CallConv::Stdcall, &K32_IsDBCSLeadByte},
    {"KERNEL32.dll", "GetVersion", 0, CallConv::Stdcall, &K32_GetVersion},
    {"KERNEL32.dll", "GetVersionExA", 4, CallConv::Stdcall, &K32_GetVersionExA},
    {"KERNEL32.dll", "GetVersionExW", 4, CallConv::Stdcall, &K32_GetVersionExW},
    {"KERNEL32.dll", "GetSystemInfo", 4, CallConv::Stdcall, &K32_GetSystemInfo},
    {"KERNEL32.dll", "GetNativeSystemInfo", 4, CallConv::Stdcall, &K32_GetSystemInfo},
    {"KERNEL32.dll", "IsProcessorFeaturePresent", 4, CallConv::Stdcall, &K32_IsProcessorFeaturePresent},
    {"KERNEL32.dll", "IsDebuggerPresent", 0, CallConv::Stdcall, &K32_IsDebuggerPresent},
    {"KERNEL32.dll", "GetCurrentProcess", 0, CallConv::Stdcall, &K32_GetCurrentProcess},
    {"KERNEL32.dll", "GetCurrentThread", 0, CallConv::Stdcall, &K32_GetCurrentThread},
    {"KERNEL32.dll", "GetCurrentProcessId", 0, CallConv::Stdcall, &K32_GetCurrentProcessId},
    {"KERNEL32.dll", "GetCurrentThreadId", 0, CallConv::Stdcall, &K32_GetCurrentThreadId},
    {"KERNEL32.dll", "SetUnhandledExceptionFilter", 4, CallConv::Stdcall, &K32_SetUnhandledExceptionFilter},
    {"KERNEL32.dll", "UnhandledExceptionFilter", 4, CallConv::Stdcall, &K32_UnhandledExceptionFilter},
    {"KERNEL32.dll", "SetErrorMode", 4, CallConv::Stdcall, &K32_SetErrorMode},
    {"KERNEL32.dll", "EncodePointer", 4, CallConv::Stdcall, &K32_EncodePointer},
    {"KERNEL32.dll", "DecodePointer", 4, CallConv::Stdcall, &K32_DecodePointer},
    {"KERNEL32.dll", "GetCommandLineA", 0, CallConv::Stdcall, &K32_GetCommandLineA},
    {"KERNEL32.dll", "GetCommandLineW", 0, CallConv::Stdcall, &K32_GetCommandLineW},
    {"KERNEL32.dll", "GetEnvironmentStrings", 0, CallConv::Stdcall, &K32_GetEnvironmentStringsA},
    {"KERNEL32.dll", "GetEnvironmentStringsA", 0, CallConv::Stdcall, &K32_GetEnvironmentStringsA},
    {"KERNEL32.dll", "GetEnvironmentStringsW", 0, CallConv::Stdcall, &K32_GetEnvironmentStringsW},
    {"KERNEL32.dll", "FreeEnvironmentStringsA", 4, CallConv::Stdcall, &K32_FreeEnvironmentStrings},
    {"KERNEL32.dll", "FreeEnvironmentStringsW", 4, CallConv::Stdcall, &K32_FreeEnvironmentStrings},
    {"KERNEL32.dll", "GetEnvironmentVariableA", 12, CallConv::Stdcall, &K32_GetEnvironmentVariableA},
    {"KERNEL32.dll", "GetEnvironmentVariableW", 12, CallConv::Stdcall, &K32_GetEnvironmentVariableW},
    {"KERNEL32.dll", "SetEnvironmentVariableA", 8, CallConv::Stdcall, &K32_SetEnvironmentVariableA},
    {"KERNEL32.dll", "SetEnvironmentVariableW", 8, CallConv::Stdcall, &K32_SetEnvironmentVariableW},
    {"KERNEL32.dll", "GetStartupInfoA", 4, CallConv::Stdcall, &GetStartupInfoCommon},
    {"KERNEL32.dll", "GetStartupInfoW", 4, CallConv::Stdcall, &GetStartupInfoCommon},
    {"KERNEL32.dll", "GetTickCount", 0, CallConv::Stdcall, &K32_GetTickCount},
    {"KERNEL32.dll", "QueryPerformanceFrequency", 4, CallConv::Stdcall, &K32_QueryPerformanceFrequency},
    {"KERNEL32.dll", "QueryPerformanceCounter", 4, CallConv::Stdcall, &K32_QueryPerformanceCounter},
    {"KERNEL32.dll", "GetSystemTimeAsFileTime", 4, CallConv::Stdcall, &K32_GetSystemTimeAsFileTime},
    {"KERNEL32.dll", "GetSystemTime", 4, CallConv::Stdcall, &K32_GetSystemTime},
    {"KERNEL32.dll", "GetLocalTime", 4, CallConv::Stdcall, &K32_GetLocalTime},
    {"KERNEL32.dll", "FileTimeToSystemTime", 8, CallConv::Stdcall, &K32_FileTimeToSystemTime},
    {"KERNEL32.dll", "SystemTimeToFileTime", 8, CallConv::Stdcall, &K32_SystemTimeToFileTime},
    {"KERNEL32.dll", "FileTimeToLocalFileTime", 8, CallConv::Stdcall, &K32_FileTimeToLocalFileTime},
    {"KERNEL32.dll", "LocalFileTimeToFileTime", 8, CallConv::Stdcall, &K32_FileTimeToLocalFileTime},
    {"KERNEL32.dll", "GetTimeZoneInformation", 4, CallConv::Stdcall, &K32_GetTimeZoneInformation},
    {"KERNEL32.dll", "OutputDebugStringA", 4, CallConv::Stdcall, &K32_OutputDebugStringA},
    {"KERNEL32.dll", "OutputDebugStringW", 4, CallConv::Stdcall, &K32_OutputDebugStringW},
    {"KERNEL32.dll", "Beep", 8, CallConv::Stdcall, &K32_Beep},
    {"KERNEL32.dll", "GetUserDefaultLCID", 0, CallConv::Stdcall, &K32_GetUserDefaultLCID},
    {"KERNEL32.dll", "GetSystemDefaultLCID", 0, CallConv::Stdcall, &K32_GetUserDefaultLCID},
    {"KERNEL32.dll", "GetThreadLocale", 0, CallConv::Stdcall, &K32_GetUserDefaultLCID},
    {"KERNEL32.dll", "GetUserDefaultLangID", 0, CallConv::Stdcall, &K32_GetUserDefaultLangID},
    {"KERNEL32.dll", "GetSystemDefaultLangID", 0, CallConv::Stdcall, &K32_GetUserDefaultLangID},
    {"KERNEL32.dll", "GetStringTypeW", 16, CallConv::Stdcall, &K32_GetStringTypeW},
    {"KERNEL32.dll", "GetStringTypeA", 20, CallConv::Stdcall, &K32_GetStringTypeA},
    {"KERNEL32.dll", "LCMapStringW", 24, CallConv::Stdcall, &K32_LCMapStringW},
    {"KERNEL32.dll", "LCMapStringA", 24, CallConv::Stdcall, &K32_LCMapStringA},
    {"KERNEL32.dll", "CompareStringA", 24, CallConv::Stdcall, &K32_CompareStringA},
    {"KERNEL32.dll", "CompareStringW", 24, CallConv::Stdcall, &K32_CompareStringW},
    {"KERNEL32.dll", "GetLocaleInfoA", 16, CallConv::Stdcall, &K32_GetLocaleInfoA},
    {"KERNEL32.dll", "GetLocaleInfoW", 16, CallConv::Stdcall, &K32_GetLocaleInfoW},
    {"KERNEL32.dll", "FlsAlloc", 4, CallConv::Stdcall, &K32_FlsAlloc},
    {"KERNEL32.dll", "FlsFree", 4, CallConv::Stdcall, &K32_FlsFree},
    {"KERNEL32.dll", "FlsGetValue", 4, CallConv::Stdcall, &K32_FlsGetValue},
    {"KERNEL32.dll", "FlsSetValue", 8, CallConv::Stdcall, &K32_FlsSetValue},
    {"KERNEL32.dll", "GetSystemDirectoryA", 8, CallConv::Stdcall, &K32_GetSystemDirectoryA},
    {"KERNEL32.dll", "GetSystemDirectoryW", 8, CallConv::Stdcall, &K32_GetSystemDirectoryW},
    {"KERNEL32.dll", "GetWindowsDirectoryA", 8, CallConv::Stdcall, &K32_GetWindowsDirectoryA},
    {"KERNEL32.dll", "GetWindowsDirectoryW", 8, CallConv::Stdcall, &K32_GetWindowsDirectoryW},
    {"KERNEL32.dll", "GetComputerNameA", 8, CallConv::Stdcall, &K32_GetComputerNameA},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32

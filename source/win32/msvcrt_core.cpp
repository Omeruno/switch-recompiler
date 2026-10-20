// msvcrt_core.cpp — msvcrt.dll: запуск и завершение программы, куча,
// errno, управление FPU, setjmp/longjmp, сигналы, сортировка, случайные
// числа, окружение, преобразования строк в числа, время.
//
// Все функции — cdecl (стек очищает вызывающий).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

#include "msvcrt.h"
#include "iat_patcher.h"
#include "../runtime/heap.h"
#include "../runtime/process.h"

namespace win32 {

uint32_t HleProcessHeapHandle(cpu::IGuestMemory& m); // kernel32_memory.cpp

namespace crt {

using namespace hle;

namespace {
Globals g_globals;
bool g_globalsReady = false;
std::vector<uint32_t> g_atexit;   // функции atexit/_onexit (вызываются в обратном порядке)
}

uint32_t NewString(cpu::IGuestMemory& m, const std::string& s) {
    uint32_t p = Malloc(m, static_cast<uint32_t>(s.size() + 1));
    WriteAnsi(m, p, s, static_cast<uint32_t>(s.size() + 1));
    return p;
}
uint32_t NewWString(cpu::IGuestMemory& m, const std::u16string& s) {
    uint32_t p = Malloc(m, static_cast<uint32_t>((s.size() + 1) * 2));
    WriteWide(m, p, s, static_cast<uint32_t>(s.size() + 1));
    return p;
}

uint32_t Malloc(cpu::IGuestMemory& m, uint32_t size, bool zero) {
    HleProcessHeapHandle(m);
    return rt::ProcessHeap().Alloc(size, zero);
}
void Free(cpu::IGuestMemory& m, uint32_t p) {
    if (!p) return;
    HleProcessHeapHandle(m);
    rt::ProcessHeap().Free(p);
}

namespace {

// Командная строка Windows -> argv (правила CommandLineToArgv: кавычки, \")
std::vector<std::string> SplitCommandLine(const std::string& cmd) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < cmd.size()) {
        while (i < cmd.size() && (cmd[i] == ' ' || cmd[i] == '\t')) ++i;
        if (i >= cmd.size()) break;
        std::string arg;
        bool quoted = false;
        while (i < cmd.size() && (quoted || (cmd[i] != ' ' && cmd[i] != '\t'))) {
            if (cmd[i] == '\\') {
                size_t n = 0;
                while (i < cmd.size() && cmd[i] == '\\') { ++n; ++i; }
                if (i < cmd.size() && cmd[i] == '"') {
                    arg.append(n / 2, '\\');
                    if (n % 2) { arg.push_back('"'); ++i; }
                } else {
                    arg.append(n, '\\');
                }
                continue;
            }
            if (cmd[i] == '"') { quoted = !quoted; ++i; continue; }
            arg.push_back(cmd[i++]);
        }
        out.push_back(arg);
    }
    return out;
}

std::string GuestCommandLine(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int idx = FindHostFunction("KERNEL32.dll", "GetCommandLineA");
    (void)c;
    if (idx < 0) return "app.exe";
    cpu::CpuState tmp;
    HostCallOutcome r = GetHostFunction(idx).fn(tmp, m);
    return ReadAnsi(m, r.returnValue);
}

void BuildCtypeTable(cpu::IGuestMemory& m, uint32_t table) {
    // _pctype указывает на table+2 (индекс -1 тоже допустим)
    m.Write16(table, 0);
    for (int ch = 0; ch < 256; ++ch) {
        uint16_t t = 0;
        if (ch >= 'A' && ch <= 'Z') t |= 0x101;          // _UPPER | _ALPHA
        if (ch >= 'a' && ch <= 'z') t |= 0x102;          // _LOWER | _ALPHA
        if (ch >= '0' && ch <= '9') t |= 0x004 | 0x080;  // _DIGIT | _HEX
        if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) t |= 0x080;
        if (ch == ' ' || (ch >= 9 && ch <= 13)) t |= 0x008; // _SPACE
        if (ch == ' ') t |= 0x040;                       // _BLANK
        if (ch < 32 || ch == 127) t |= 0x020;            // _CONTROL
        if (ch > 32 && ch < 127 && !(t & 0x107)) t |= 0x010; // _PUNCT
        m.Write16(table + 2 + ch * 2, t);
    }
}

} // namespace

Globals& G(cpu::IGuestMemory& m) {
    if (g_globalsReady) return g_globals;
    g_globalsReady = true;
    Globals& g = g_globals;
    uint32_t block = m.Allocate(0x2000, cpu::kProtRW);
    uint32_t p = block;
    auto take = [&](uint32_t n) { uint32_t r = p; p += (n + 15) & ~15u; return r; };
    g.errnoAddr = take(4);
    g.doserrnoAddr = take(4);
    g.iob = take(kFileSize * kIobCount);
    g.mbCurMax = take(4);
    m.Write32(g.mbCurMax, 1);
    g.fmode = take(4);
    g.commode = take(4);
    g.argc = take(4);
    g.argv = take(4);
    g.wargv = take(4);
    g.environ = take(4);
    g.wenviron = take(4);
    g.acmdln = take(4);
    g.wcmdln = take(4);
    g.ctypeTable = take(2 * 257);
    BuildCtypeTable(m, g.ctypeTable);
    g.pctype = take(4);
    m.Write32(g.pctype, g.ctypeTable + 2);
    g.pwctype = take(4);
    m.Write32(g.pwctype, g.ctypeTable + 2);
    g.pgmptr = take(4);
    g.wpgmptr = take(4);
    g.osver = take(4); m.Write32(g.osver, 2600);
    g.winver = take(4); m.Write32(g.winver, 0x0501);
    g.winmajor = take(4); m.Write32(g.winmajor, 5);
    g.winminor = take(4); m.Write32(g.winminor, 1);
    g.timezone = take(4);
    g.daylight = take(4);
    g.tzname = take(8);
    // FILE _iob[]: _ptr, _cnt, _base, _flag, _file, _charbuf, _bufsiz, _tmpfname
    for (uint32_t i = 0; i < kIobCount; ++i) {
        uint32_t f = g.iob + i * kFileSize;
        m.Write32(f + 16, i < 3 ? i : 0xFFFFFFFFu);
        m.Write32(f + 12, i == 0 ? 0x0001 : (i < 3 ? 0x0002 : 0)); // _IOREAD / _IOWRT
    }
    // Командная строка и argv/environ
    cpu::CpuState tmp;
    std::string cmd = GuestCommandLine(tmp, m);
    uint32_t a = NewString(m, cmd);
    m.Write32(g.acmdln, a);
    m.Write32(g.wcmdln, NewWString(m, AnsiToWide(cmd)));
    std::vector<std::string> args = SplitCommandLine(cmd);
    if (args.empty()) args.push_back("app.exe");
    uint32_t argv = Malloc(m, static_cast<uint32_t>((args.size() + 1) * 4));
    uint32_t wargv = Malloc(m, static_cast<uint32_t>((args.size() + 1) * 4));
    for (size_t i = 0; i < args.size(); ++i) {
        m.Write32(argv + static_cast<uint32_t>(i * 4), NewString(m, args[i]));
        m.Write32(wargv + static_cast<uint32_t>(i * 4), NewWString(m, AnsiToWide(args[i])));
    }
    m.Write32(argv + static_cast<uint32_t>(args.size() * 4), 0);
    m.Write32(wargv + static_cast<uint32_t>(args.size() * 4), 0);
    m.Write32(g.argc, static_cast<uint32_t>(args.size()));
    m.Write32(g.argv, argv);
    m.Write32(g.wargv, wargv);
    m.Write32(g.pgmptr, m.Read32(argv));
    m.Write32(g.wpgmptr, m.Read32(wargv));
    // окружение: из GetEnvironmentStringsA
    int idx = FindHostFunction("KERNEL32.dll", "GetEnvironmentStringsA");
    std::vector<std::string> env;
    if (idx >= 0) {
        uint32_t block2 = GetHostFunction(idx).fn(tmp, m).returnValue;
        for (uint32_t q = block2; m.Read8(q);) {
            std::string s = ReadAnsi(m, q);
            env.push_back(s);
            q += static_cast<uint32_t>(s.size() + 1);
        }
    }
    uint32_t envp = Malloc(m, static_cast<uint32_t>((env.size() + 1) * 4));
    uint32_t wenvp = Malloc(m, static_cast<uint32_t>((env.size() + 1) * 4));
    for (size_t i = 0; i < env.size(); ++i) {
        m.Write32(envp + static_cast<uint32_t>(i * 4), NewString(m, env[i]));
        m.Write32(wenvp + static_cast<uint32_t>(i * 4), NewWString(m, AnsiToWide(env[i])));
    }
    m.Write32(envp + static_cast<uint32_t>(env.size() * 4), 0);
    m.Write32(wenvp + static_cast<uint32_t>(env.size() * 4), 0);
    m.Write32(g.environ, envp);
    m.Write32(g.wenviron, wenvp);
    m.Write32(g.tzname, NewString(m, "UTC"));
    m.Write32(g.tzname + 4, NewString(m, "UTC"));
    return g;
}

void SetErrno(cpu::IGuestMemory& m, int e) { m.Write32(G(m).errnoAddr, static_cast<uint32_t>(e)); }

namespace {

// ===== Запуск =====

// int __getmainargs(int* argc, char*** argv, char*** env, int wildcard, _startupinfo*)
HostCallOutcome C_getmainargs(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Globals& g = G(m);
    m.Write32(Arg(c, m, 0), m.Read32(g.argc));
    m.Write32(Arg(c, m, 1), m.Read32(g.argv));
    m.Write32(Arg(c, m, 2), m.Read32(g.environ));
    return Ret(0);
}
HostCallOutcome C_wgetmainargs(cpu::CpuState& c, cpu::IGuestMemory& m) {
    Globals& g = G(m);
    m.Write32(Arg(c, m, 0), m.Read32(g.argc));
    m.Write32(Arg(c, m, 1), m.Read32(g.wargv));
    m.Write32(Arg(c, m, 2), m.Read32(g.wenviron));
    return Ret(0);
}
HostCallOutcome C_set_app_type(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }
HostCallOutcome C_p_fmode(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).fmode); }
HostCallOutcome C_p_commode(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).commode); }
HostCallOutcome C_p_initenv(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).environ); }
HostCallOutcome C_p_environ(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).environ); }
HostCallOutcome C_p_wenviron(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).wenviron); }
HostCallOutcome C_p_argc(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).argc); }
HostCallOutcome C_p_argv(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).argv); }
HostCallOutcome C_p_wargv(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).wargv); }
HostCallOutcome C_p_pgmptr(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).pgmptr); }
HostCallOutcome C_p_acmdln(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).acmdln); }
HostCallOutcome C_p_wcmdln(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).wcmdln); }
HostCallOutcome C_p_mb_cur_max(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).mbCurMax); }
HostCallOutcome C_mb_cur_max_func(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }
HostCallOutcome C_pctype_func(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(m.Read32(G(m).pctype)); }
HostCallOutcome C_lc_codepage_func(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); } // "C"-локаль
HostCallOutcome C_setusermatherr(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

// void _initterm(PVFV* begin, PVFV* end)
HostCallOutcome C_initterm(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t b = Arg(c, m, 0), e = Arg(c, m, 1);
    for (uint32_t p = b; p < e; p += 4) {
        uint32_t fn = m.Read32(p);
        if (fn && fn != 0xFFFFFFFFu) CallCdecl(c, m, fn, {});
    }
    return Ret(0);
}
// int _initterm_e(PIFV* begin, PIFV* end): останавливается на ненулевом результате
HostCallOutcome C_initterm_e(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t b = Arg(c, m, 0), e = Arg(c, m, 1);
    for (uint32_t p = b; p < e; p += 4) {
        uint32_t fn = m.Read32(p);
        if (fn && fn != 0xFFFFFFFFu) {
            uint32_t r = CallCdecl(c, m, fn, {});
            if (r) return Ret(r);
        }
    }
    return Ret(0);
}
HostCallOutcome C_onexit(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t fn = Arg(c, m, 0);
    g_atexit.push_back(fn);
    return Ret(fn);
}
HostCallOutcome C_atexit(cpu::CpuState& c, cpu::IGuestMemory& m) {
    g_atexit.push_back(Arg(c, m, 0));
    return Ret(0);
}
// _onexit_t __dllonexit(_onexit_t func, PVFV** pbegin, PVFV** pend)
HostCallOutcome C_dllonexit(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t fn = Arg(c, m, 0);
    g_atexit.push_back(fn);
    return Ret(fn);
}

void RunAtExit(cpu::CpuState& c, cpu::IGuestMemory& m) {
    while (!g_atexit.empty()) {
        uint32_t fn = g_atexit.back();
        g_atexit.pop_back();
        if (fn) CallCdecl(c, m, fn, {});
    }
    FlushAll(m);
}

HostCallOutcome ExitWith(int32_t code) {
    HostCallOutcome r;
    r.requestExit = true;
    r.exitCode = code;
    return r;
}
HostCallOutcome C_exit(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int32_t code = static_cast<int32_t>(Arg(c, m, 0));
    RunAtExit(c, m);
    return ExitWith(code);
}
HostCallOutcome C__exit(cpu::CpuState& c, cpu::IGuestMemory& m) { FlushAll(m); return ExitWith(static_cast<int32_t>(Arg(c, m, 0))); }
HostCallOutcome C_cexit(cpu::CpuState& c, cpu::IGuestMemory& m) { RunAtExit(c, m); return Ret(0); }
HostCallOutcome C_c_exit(cpu::CpuState&, cpu::IGuestMemory& m) { FlushAll(m); return Ret(0); }
HostCallOutcome C_amsg_exit(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string msg = "runtime error R60" + std::to_string(Arg(c, m, 0) % 100) + "\n";
    StreamWrite(m, G(m).iob + 2 * kFileSize, msg.data(), msg.size());
    return ExitWith(255);
}
HostCallOutcome C_abort(cpu::CpuState&, cpu::IGuestMemory& m) {
    const char msg[] = "\nabnormal program termination\n";
    StreamWrite(m, G(m).iob + 2 * kFileSize, msg, sizeof(msg) - 1);
    FlushAll(m);
    return ExitWith(3);
}
HostCallOutcome C_purecall(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const char msg[] = "R6025 - pure virtual function call\n";
    StreamWrite(m, G(m).iob + 2 * kFileSize, msg, sizeof(msg) - 1);
    (void)c;
    return ExitWith(255);
}
HostCallOutcome C_XcptFilter(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); } // EXCEPTION_CONTINUE_SEARCH

// ===== errno =====
HostCallOutcome C_errno(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).errnoAddr); }
HostCallOutcome C_doserrno(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(G(m).doserrnoAddr); }
HostCallOutcome C_get_errno(cpu::CpuState& c, cpu::IGuestMemory& m) { m.Write32(Arg(c, m, 0), m.Read32(G(m).errnoAddr)); return Ret(0); }
HostCallOutcome C_set_errno(cpu::CpuState& c, cpu::IGuestMemory& m) { SetErrno(m, static_cast<int>(Arg(c, m, 0))); return Ret(0); }

// ===== FPU: _controlfp и компания =====
// Формат MS: _EM_INVALID 0x10, _EM_DENORMAL 0x80000, _EM_ZERODIVIDE 0x08,
// _EM_OVERFLOW 0x04, _EM_UNDERFLOW 0x02, _EM_INEXACT 0x01; _RC_* 0x300;
// _PC_64 0, _PC_53 0x10000, _PC_24 0x20000; _IC_AFFINE 0x40000.
uint32_t MsFromX87(uint16_t cw) {
    uint32_t r = 0;
    if (cw & 0x01) r |= 0x10;
    if (cw & 0x02) r |= 0x80000;
    if (cw & 0x04) r |= 0x08;
    if (cw & 0x08) r |= 0x04;
    if (cw & 0x10) r |= 0x02;
    if (cw & 0x20) r |= 0x01;
    r |= static_cast<uint32_t>((cw >> 10) & 3) << 8;
    switch ((cw >> 8) & 3) {
        case 0: r |= 0x20000; break;
        case 2: r |= 0x10000; break;
        default: break; // 64 бита
    }
    if (cw & 0x1000) r |= 0x40000;
    return r;
}
uint16_t X87FromMs(uint32_t ms, uint16_t cw) {
    cw &= ~0x1F3Fu;
    if (ms & 0x10) cw |= 0x01;
    if (ms & 0x80000) cw |= 0x02;
    if (ms & 0x08) cw |= 0x04;
    if (ms & 0x04) cw |= 0x08;
    if (ms & 0x02) cw |= 0x10;
    if (ms & 0x01) cw |= 0x20;
    cw |= static_cast<uint16_t>(((ms >> 8) & 3) << 10);
    uint32_t pc = ms & 0x30000;
    cw |= static_cast<uint16_t>((pc == 0x20000 ? 0 : pc == 0x10000 ? 2 : 3) << 8);
    if (ms & 0x40000) cw |= 0x1000;
    return static_cast<uint16_t>(cw | 0x40);
}
HostCallOutcome C_control87(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t nw = Arg(c, m, 0), mask = Arg(c, m, 1);
    uint32_t cur = MsFromX87(c.fpuControl);
    if (mask) {
        cur = (cur & ~mask) | (nw & mask);
        c.fpuControl = X87FromMs(cur, c.fpuControl);
        // MXCSR: те же маски исключений и режим округления
        uint32_t mx = c.mxcsr & ~0x7F80u;
        mx |= (static_cast<uint32_t>(c.fpuControl & 0x3F) << 7);
        mx |= static_cast<uint32_t>((c.fpuControl >> 10) & 3) << 13;
        c.mxcsr = mx;
    }
    return Ret(cur);
}
HostCallOutcome C_controlfp(cpu::CpuState& c, cpu::IGuestMemory& m) {
    // _controlfp не трогает _EM_DENORMAL
    uint32_t mask = Arg(c, m, 1) & ~0x80000u;
    uint32_t nw = Arg(c, m, 0);
    uint32_t cur = MsFromX87(c.fpuControl);
    if (mask) {
        cur = (cur & ~mask) | (nw & mask);
        c.fpuControl = X87FromMs(cur, c.fpuControl);
    }
    return Ret(cur);
}
uint32_t MsStatus(cpu::CpuState& c) {
    uint16_t sw = c.fpuStatus;
    uint32_t mx = c.mxcsr;
    uint32_t r = 0;
    if ((sw | mx) & 0x01) r |= 0x10;     // _SW_INVALID
    if ((sw | mx) & 0x02) r |= 0x80000;  // _SW_DENORMAL
    if ((sw | mx) & 0x04) r |= 0x08;     // _SW_ZERODIVIDE
    if ((sw | mx) & 0x08) r |= 0x04;
    if ((sw | mx) & 0x10) r |= 0x02;
    if ((sw | mx) & 0x20) r |= 0x01;
    return r;
}
HostCallOutcome C_statusfp(cpu::CpuState& c, cpu::IGuestMemory&) { return Ret(MsStatus(c)); }
HostCallOutcome C_clearfp(cpu::CpuState& c, cpu::IGuestMemory&) {
    uint32_t r = MsStatus(c);
    c.fpuStatus &= static_cast<uint16_t>(~0x80FFu);
    c.mxcsr &= ~0x3Fu;
    return Ret(r);
}
HostCallOutcome C_fpreset(cpu::CpuState& c, cpu::IGuestMemory&) {
    c.fpuControl = 0x027F;
    c.fpuStatus = 0;
    c.fpuTag = 0xFFFF;
    c.fpuTop = 0;
    c.mxcsr = 0x1F80;
    return Ret(0);
}

// ===== setjmp/longjmp =====
// jmp_buf (_JUMP_BUFFER): Ebp, Ebx, Edi, Esi, Esp, Eip, Registration, TryLevel, Cookie, UnwindFunc, UnwindData[6]
HostCallOutcome SetJmpCommon(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t buf = Arg(c, m, 0);
    m.Write32(buf + 0, c.regs[5]);
    m.Write32(buf + 4, c.regs[3]);
    m.Write32(buf + 8, c.regs[7]);
    m.Write32(buf + 12, c.regs[6]);
    m.Write32(buf + 16, c.ESP() + 4);            // ESP после возврата из setjmp
    m.Write32(buf + 20, m.Read32(c.ESP()));      // адрес возврата
    m.Write32(buf + 24, m.Read32(g_tebAddr));    // fs:[0]
    m.Write32(buf + 28, 0xFFFFFFFFu);
    m.Write32(buf + 32, 0x56433230);             // "VC20"
    return Ret(0);
}
HostCallOutcome C_longjmp(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t buf = Arg(c, m, 0);
    uint32_t val = Arg(c, m, 1);
    c.regs[5] = m.Read32(buf + 0);
    c.regs[3] = m.Read32(buf + 4);
    c.regs[7] = m.Read32(buf + 8);
    c.regs[6] = m.Read32(buf + 12);
    c.ESP() = m.Read32(buf + 16);
    c.eip = m.Read32(buf + 20);
    uint32_t reg = m.Read32(buf + 24);
    if (g_tebAddr) m.Write32(g_tebAddr, reg); // раскрутка SEH-цепочки до сохранённой точки
    c.regs[0] = val ? val : 1;
    HostCallOutcome r;
    r.customReturn = true;
    return r;
}

// ===== Сигналы =====
std::map<int, uint32_t> g_signals;
HostCallOutcome C_signal(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int sig = static_cast<int>(Arg(c, m, 0));
    uint32_t old = g_signals[sig];
    g_signals[sig] = Arg(c, m, 1);
    return Ret(old);
}
HostCallOutcome C_raise(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int sig = static_cast<int>(Arg(c, m, 0));
    uint32_t h = g_signals[sig];
    if (h == 1) return Ret(0); // SIG_IGN
    if (h == 0) { // SIG_DFL
        if (sig == 22) return C_abort(c, m); // SIGABRT
        FlushAll(m);
        return ExitWith(3);
    }
    g_signals[sig] = 0;
    CallCdecl(c, m, h, {static_cast<uint32_t>(sig)});
    return Ret(0);
}
HostCallOutcome C_nop(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

// ===== Куча =====
HostCallOutcome C_malloc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Malloc(m, Arg(c, m, 0));
    if (!p) SetErrno(m, kENOMEM);
    return Ret(p);
}
HostCallOutcome C_calloc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint64_t n = static_cast<uint64_t>(Arg(c, m, 0)) * Arg(c, m, 1);
    if (n > 0x7FFF0000u) { SetErrno(m, kENOMEM); return Ret(0); }
    uint32_t p = Malloc(m, static_cast<uint32_t>(n), true);
    if (!p) SetErrno(m, kENOMEM);
    return Ret(p);
}
HostCallOutcome C_realloc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), size = Arg(c, m, 1);
    HleProcessHeapHandle(m);
    if (p && size == 0) { rt::ProcessHeap().Free(p); return Ret(0); }
    uint32_t n = rt::ProcessHeap().Realloc(p, size);
    if (!n) SetErrno(m, kENOMEM);
    return Ret(n);
}
HostCallOutcome C_free(cpu::CpuState& c, cpu::IGuestMemory& m) { Free(m, Arg(c, m, 0)); return Ret(0); }
HostCallOutcome C_msize(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HleProcessHeapHandle(m);
    return Ret(rt::ProcessHeap().Size(Arg(c, m, 0)));
}
HostCallOutcome C_expand(cpu::CpuState& c, cpu::IGuestMemory& m) {
    HleProcessHeapHandle(m);
    return Ret(rt::ProcessHeap().Realloc(Arg(c, m, 0), Arg(c, m, 1), false, true));
}
HostCallOutcome C_heapchk(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(static_cast<uint32_t>(-2)); } // _HEAPOK
HostCallOutcome C_get_heap_handle(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(HleProcessHeapHandle(m)); }
uint32_t g_newHandler = 0;
HostCallOutcome C_set_new_handler(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t old = g_newHandler;
    g_newHandler = Arg(c, m, 0);
    return Ret(old);
}
HostCallOutcome C_query_new_handler(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(g_newHandler); }
HostCallOutcome C_set_new_mode(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }
HostCallOutcome C_operator_new(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t size = Arg(c, m, 0);
    uint32_t p = Malloc(m, size);
    while (!p && g_newHandler) {
        if (!CallCdecl(c, m, g_newHandler, {size})) break;
        p = Malloc(m, size);
    }
    return Ret(p);
}

// ===== Сортировка и поиск =====
// void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*))
HostCallOutcome C_qsort(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t base = Arg(c, m, 0), n = Arg(c, m, 1), size = Arg(c, m, 2), cmp = Arg(c, m, 3);
    if (n < 2 || !size) return Ret(0);
    // Сортируем индексы на хосте, сравнивая элементы функцией гостя,
    // затем переставляем память (стабильность не требуется, как и в CRT).
    std::vector<uint32_t> idx(n);
    for (uint32_t i = 0; i < n; ++i) idx[i] = i;
    // Копия, чтобы сравнения шли по исходным данным
    std::vector<uint8_t> copy(static_cast<size_t>(n) * size);
    m.ReadBytes(base, copy.data(), copy.size());
    // временный буфер в памяти гостя для двух элементов
    uint32_t tmp = Malloc(m, size * 2);
    std::stable_sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t b) {
        m.LoadBytes(tmp, copy.data() + static_cast<size_t>(a) * size, size);
        m.LoadBytes(tmp + size, copy.data() + static_cast<size_t>(b) * size, size);
        return static_cast<int32_t>(CallCdecl(c, m, cmp, {tmp, tmp + size})) < 0;
    });
    Free(m, tmp);
    for (uint32_t i = 0; i < n; ++i) m.LoadBytes(base + i * size, copy.data() + static_cast<size_t>(idx[i]) * size, size);
    return Ret(0);
}
// void* bsearch(const void* key, const void* base, size_t n, size_t size, cmp)
HostCallOutcome C_bsearch(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t key = Arg(c, m, 0), base = Arg(c, m, 1), n = Arg(c, m, 2), size = Arg(c, m, 3), cmp = Arg(c, m, 4);
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int32_t r = static_cast<int32_t>(CallCdecl(c, m, cmp, {key, base + mid * size}));
        if (r == 0) return Ret(base + mid * size);
        if (r < 0) hi = mid; else lo = mid + 1;
    }
    return Ret(0);
}

// ===== Случайные числа (тот же генератор, что в msvcrt) =====
uint32_t g_randSeed = 1;
HostCallOutcome C_srand(cpu::CpuState& c, cpu::IGuestMemory& m) { g_randSeed = Arg(c, m, 0); return Ret(0); }
HostCallOutcome C_rand(cpu::CpuState&, cpu::IGuestMemory&) {
    g_randSeed = g_randSeed * 214013u + 2531011u;
    return Ret((g_randSeed >> 16) & 0x7FFF);
}

// ===== Простая арифметика =====
HostCallOutcome C_abs(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int32_t v = static_cast<int32_t>(Arg(c, m, 0));
    return Ret(static_cast<uint32_t>(v < 0 ? -v : v));
}
// div_t div(int, int): структура в 8 байт возвращается в EDX:EAX
HostCallOutcome C_div(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int32_t a = static_cast<int32_t>(Arg(c, m, 0)), b = static_cast<int32_t>(Arg(c, m, 1));
    if (!b) return Ret(0);
    c.regs[2] = static_cast<uint32_t>(a % b);
    return Ret(static_cast<uint32_t>(a / b));
}
HostCallOutcome C_rotl(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t v = Arg(c, m, 0), n = Arg(c, m, 1) & 31;
    return Ret(n ? (v << n) | (v >> (32 - n)) : v);
}
HostCallOutcome C_rotr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t v = Arg(c, m, 0), n = Arg(c, m, 1) & 31;
    return Ret(n ? (v >> n) | (v << (32 - n)) : v);
}

// ===== Окружение =====
HostCallOutcome C_getenv(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string name = ReadAnsi(m, Arg(c, m, 0));
    for (char& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    uint32_t envp = m.Read32(G(m).environ);
    for (uint32_t i = 0;; ++i) {
        uint32_t s = m.Read32(envp + i * 4);
        if (!s) break;
        std::string kv = ReadAnsi(m, s);
        size_t eq = kv.find('=');
        std::string k = kv.substr(0, eq);
        for (char& ch : k) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (k == name) return Ret(s + static_cast<uint32_t>(eq + 1));
    }
    return Ret(0);
}
HostCallOutcome C_wgetenv(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string name = WideToUtf8(ReadWide(m, Arg(c, m, 0)));
    for (char& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    uint32_t envp = m.Read32(G(m).wenviron);
    for (uint32_t i = 0;; ++i) {
        uint32_t s = m.Read32(envp + i * 4);
        if (!s) break;
        std::string kv = WideToUtf8(ReadWide(m, s));
        size_t eq = kv.find('=');
        std::string k = kv.substr(0, eq);
        for (char& ch : k) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (k == name) return Ret(s + static_cast<uint32_t>((eq + 1) * 2));
    }
    return Ret(0);
}
HostCallOutcome C_putenv(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }
HostCallOutcome C_getpid(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0x1000); }
HostCallOutcome C_system(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0xFFFFFFFFu); }

// ===== Преобразования =====
// Читает число из строки гостя (ANSI или UTF-16) по правилам strtol/strtod
template <bool Wide>
std::string ReadNumberText(cpu::IGuestMemory& m, uint32_t p, size_t max = 512) {
    std::string s;
    for (size_t i = 0; i < max; ++i) {
        uint32_t ch = Wide ? m.Read16(p + static_cast<uint32_t>(i * 2)) : m.Read8(p + static_cast<uint32_t>(i));
        if (!ch || ch > 127) break;
        s.push_back(static_cast<char>(ch));
    }
    return s;
}
template <bool Wide>
HostCallOutcome StrToLong(cpu::CpuState& c, cpu::IGuestMemory& m, bool isUnsigned) {
    uint32_t p = Arg(c, m, 0), endp = Arg(c, m, 1);
    int base = static_cast<int>(Arg(c, m, 2));
    std::string s = ReadNumberText<Wide>(m, p);
    char* end = nullptr;
    errno = 0;
    uint32_t r;
    if (isUnsigned) {
        unsigned long long v = std::strtoull(s.c_str(), &end, base);
        bool neg = s.find('-') != std::string::npos && s.find('-') < static_cast<size_t>(end - s.c_str());
        if (v > 0xFFFFFFFFull && !neg) { r = 0xFFFFFFFFu; SetErrno(m, kERANGE); }
        else r = static_cast<uint32_t>(v);
    } else {
        long long v = std::strtoll(s.c_str(), &end, base);
        if (v > INT32_MAX) { r = INT32_MAX; SetErrno(m, kERANGE); }
        else if (v < INT32_MIN) { r = static_cast<uint32_t>(INT32_MIN); SetErrno(m, kERANGE); }
        else r = static_cast<uint32_t>(static_cast<int32_t>(v));
    }
    if (endp) m.Write32(endp, p + static_cast<uint32_t>((end - s.c_str()) * (Wide ? 2 : 1)));
    return Ret(r);
}
HostCallOutcome C_strtol(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToLong<false>(c, m, false); }
HostCallOutcome C_strtoul(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToLong<false>(c, m, true); }
HostCallOutcome C_wcstol(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToLong<true>(c, m, false); }
HostCallOutcome C_wcstoul(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToLong<true>(c, m, true); }
template <bool Wide>
HostCallOutcome StrToI64(cpu::CpuState& c, cpu::IGuestMemory& m, bool isUnsigned) {
    uint32_t p = Arg(c, m, 0), endp = Arg(c, m, 1);
    int base = static_cast<int>(Arg(c, m, 2));
    std::string s = ReadNumberText<Wide>(m, p);
    char* end = nullptr;
    uint64_t v = isUnsigned ? std::strtoull(s.c_str(), &end, base) : static_cast<uint64_t>(std::strtoll(s.c_str(), &end, base));
    if (endp) m.Write32(endp, p + static_cast<uint32_t>((end - s.c_str()) * (Wide ? 2 : 1)));
    c.regs[2] = static_cast<uint32_t>(v >> 32);
    return Ret(static_cast<uint32_t>(v));
}
HostCallOutcome C_strtoi64(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToI64<false>(c, m, false); }
HostCallOutcome C_strtoui64(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToI64<false>(c, m, true); }
HostCallOutcome C_atoi(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadNumberText<false>(m, Arg(c, m, 0));
    return Ret(static_cast<uint32_t>(static_cast<int32_t>(std::strtol(s.c_str(), nullptr, 10))));
}
HostCallOutcome C_wtoi(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadNumberText<true>(m, Arg(c, m, 0));
    return Ret(static_cast<uint32_t>(static_cast<int32_t>(std::strtol(s.c_str(), nullptr, 10))));
}
HostCallOutcome C_atoi64(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadNumberText<false>(m, Arg(c, m, 0));
    int64_t v = std::strtoll(s.c_str(), nullptr, 10);
    c.regs[2] = static_cast<uint32_t>(static_cast<uint64_t>(v) >> 32);
    return Ret(static_cast<uint32_t>(v));
}

// Возврат double из cdecl-функции — в ST(0) x87.
HostCallOutcome RetDouble(cpu::CpuState& c, double v) {
    c.fpuTop = static_cast<uint8_t>((c.fpuTop - 1) & 7);
    c.st[c.fpuTop] = v;
    int tag = v == 0.0 ? 1 : (std::isnan(v) || std::isinf(v)) ? 2 : 0;
    c.fpuTag = static_cast<uint16_t>((c.fpuTag & ~(3u << (c.fpuTop * 2))) | (static_cast<unsigned>(tag) << (c.fpuTop * 2)));
    return Ret(0);
}
HostCallOutcome C_atof(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadNumberText<false>(m, Arg(c, m, 0));
    return RetDouble(c, std::strtod(s.c_str(), nullptr));
}
template <bool Wide>
HostCallOutcome StrToD(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), endp = Arg(c, m, 1);
    std::string s = ReadNumberText<Wide>(m, p);
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    // msvcrt не понимает "inf"/"nan"/шестнадцатеричные — откатываемся
    std::string used(s.c_str(), static_cast<size_t>(end - s.c_str()));
    if (used.find_first_of("iInNxX") != std::string::npos) { v = 0; end = const_cast<char*>(s.c_str()); }
    if (endp) m.Write32(endp, p + static_cast<uint32_t>((end - s.c_str()) * (Wide ? 2 : 1)));
    if (std::isinf(v)) SetErrno(m, kERANGE);
    return RetDouble(c, v);
}
HostCallOutcome C_strtod(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToD<false>(c, m); }
HostCallOutcome C_wcstod(cpu::CpuState& c, cpu::IGuestMemory& m) { return StrToD<true>(c, m); }

// char* _itoa(int value, char* buf, int radix) и родственники
std::string ToRadix(uint64_t v, int radix, bool neg) {
    if (radix < 2 || radix > 36) radix = 10;
    std::string s;
    do { int d = static_cast<int>(v % radix); s.insert(s.begin(), static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10)); v /= radix; } while (v);
    if (neg) s.insert(s.begin(), '-');
    return s;
}
HostCallOutcome C_itoa(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int32_t v = static_cast<int32_t>(Arg(c, m, 0));
    int radix = static_cast<int>(Arg(c, m, 2));
    bool neg = radix == 10 && v < 0;
    uint64_t u = neg ? static_cast<uint64_t>(-static_cast<int64_t>(v)) : static_cast<uint32_t>(v);
    std::string s = ToRadix(u, radix, neg);
    WriteAnsi(m, Arg(c, m, 1), s, static_cast<uint32_t>(s.size() + 1));
    return Ret(Arg(c, m, 1));
}
HostCallOutcome C_ultoa(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ToRadix(Arg(c, m, 0), static_cast<int>(Arg(c, m, 2)), false);
    WriteAnsi(m, Arg(c, m, 1), s, static_cast<uint32_t>(s.size() + 1));
    return Ret(Arg(c, m, 1));
}
// char* _i64toa(__int64 value, char* buf, int radix): 64-битный аргумент занимает 2 слота
HostCallOutcome C_i64toa(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int64_t v = static_cast<int64_t>(Arg(c, m, 0) | (static_cast<uint64_t>(Arg(c, m, 1)) << 32));
    int radix = static_cast<int>(Arg(c, m, 3));
    bool neg = radix == 10 && v < 0;
    std::string s = ToRadix(neg ? static_cast<uint64_t>(-v) : static_cast<uint64_t>(v), radix, neg);
    WriteAnsi(m, Arg(c, m, 2), s, static_cast<uint32_t>(s.size() + 1));
    return Ret(Arg(c, m, 2));
}
HostCallOutcome C_ui64toa(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint64_t v = Arg(c, m, 0) | (static_cast<uint64_t>(Arg(c, m, 1)) << 32);
    std::string s = ToRadix(v, static_cast<int>(Arg(c, m, 3)), false);
    WriteAnsi(m, Arg(c, m, 2), s, static_cast<uint32_t>(s.size() + 1));
    return Ret(Arg(c, m, 2));
}
HostCallOutcome C_itow(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int32_t v = static_cast<int32_t>(Arg(c, m, 0));
    int radix = static_cast<int>(Arg(c, m, 2));
    bool neg = radix == 10 && v < 0;
    uint64_t u = neg ? static_cast<uint64_t>(-static_cast<int64_t>(v)) : static_cast<uint32_t>(v);
    std::string s = ToRadix(u, radix, neg);
    WriteWide(m, Arg(c, m, 1), AnsiToWide(s), static_cast<uint32_t>(s.size() + 1));
    return Ret(Arg(c, m, 1));
}

// ===== Время =====
uint32_t g_tmBuf = 0, g_timeStrBuf = 0;
int64_t NowSeconds() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
HostCallOutcome C_time(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int64_t t = NowSeconds();
    if (Arg(c, m, 0)) m.Write32(Arg(c, m, 0), static_cast<uint32_t>(t));
    return Ret(static_cast<uint32_t>(t));
}
HostCallOutcome C_time64(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int64_t t = NowSeconds();
    uint32_t p = Arg(c, m, 0);
    if (p) { m.Write32(p, static_cast<uint32_t>(t)); m.Write32(p + 4, static_cast<uint32_t>(static_cast<uint64_t>(t) >> 32)); }
    c.regs[2] = static_cast<uint32_t>(static_cast<uint64_t>(t) >> 32);
    return Ret(static_cast<uint32_t>(t));
}
HostCallOutcome C_clock(cpu::CpuState&, cpu::IGuestMemory&) {
    static auto start = std::chrono::steady_clock::now();
    return Ret(static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count())); // CLOCKS_PER_SEC = 1000
}
void WriteTm(cpu::IGuestMemory& m, uint32_t p, const std::tm& t) {
    m.Write32(p + 0, static_cast<uint32_t>(t.tm_sec));
    m.Write32(p + 4, static_cast<uint32_t>(t.tm_min));
    m.Write32(p + 8, static_cast<uint32_t>(t.tm_hour));
    m.Write32(p + 12, static_cast<uint32_t>(t.tm_mday));
    m.Write32(p + 16, static_cast<uint32_t>(t.tm_mon));
    m.Write32(p + 20, static_cast<uint32_t>(t.tm_year));
    m.Write32(p + 24, static_cast<uint32_t>(t.tm_wday));
    m.Write32(p + 28, static_cast<uint32_t>(t.tm_yday));
    m.Write32(p + 32, static_cast<uint32_t>(t.tm_isdst));
}
std::tm ReadTm(cpu::IGuestMemory& m, uint32_t p) {
    std::tm t{};
    t.tm_sec = static_cast<int>(m.Read32(p + 0));
    t.tm_min = static_cast<int>(m.Read32(p + 4));
    t.tm_hour = static_cast<int>(m.Read32(p + 8));
    t.tm_mday = static_cast<int>(m.Read32(p + 12));
    t.tm_mon = static_cast<int>(m.Read32(p + 16));
    t.tm_year = static_cast<int>(m.Read32(p + 20));
    t.tm_wday = static_cast<int>(m.Read32(p + 24));
    t.tm_yday = static_cast<int>(m.Read32(p + 28));
    t.tm_isdst = static_cast<int>(m.Read32(p + 32));
    return t;
}
HostCallOutcome TmCommon(cpu::IGuestMemory& m, int64_t secs, bool local) {
    if (!g_tmBuf) g_tmBuf = Malloc(m, 40, true);
    std::time_t tt = static_cast<std::time_t>(secs);
    std::tm t{};
#if defined(_WIN32)
    if (local) localtime_s(&t, &tt); else gmtime_s(&t, &tt);
#else
    if (local) localtime_r(&tt, &t); else gmtime_r(&tt, &t);
#endif
    WriteTm(m, g_tmBuf, t);
    return Ret(g_tmBuf);
}
HostCallOutcome C_localtime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return TmCommon(m, static_cast<int32_t>(m.Read32(Arg(c, m, 0))), true);
}
HostCallOutcome C_gmtime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return TmCommon(m, static_cast<int32_t>(m.Read32(Arg(c, m, 0))), false);
}
HostCallOutcome C_localtime64(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    return TmCommon(m, static_cast<int64_t>(m.Read32(p) | (static_cast<uint64_t>(m.Read32(p + 4)) << 32)), true);
}
HostCallOutcome C_mktime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    std::tm t = ReadTm(m, p);
    std::time_t r = std::mktime(&t);
    WriteTm(m, p, t);
    return Ret(static_cast<uint32_t>(r));
}
HostCallOutcome C_difftime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    return RetDouble(c, static_cast<double>(static_cast<int32_t>(Arg(c, m, 0))) - static_cast<int32_t>(Arg(c, m, 1)));
}
// size_t strftime(char* s, size_t max, const char* fmt, const struct tm*)
HostCallOutcome C_strftime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t dst = Arg(c, m, 0), max = Arg(c, m, 1);
    std::string fmt = ReadAnsi(m, Arg(c, m, 2));
    std::tm t = ReadTm(m, Arg(c, m, 3));
    // msvcrt: %#d и т.п. — убираем '#', хост его не знает в той же семантике
    std::string f2;
    for (size_t i = 0; i < fmt.size(); ++i) { if (fmt[i] == '#' && i > 0 && fmt[i - 1] == '%') continue; f2.push_back(fmt[i]); }
    char buf[1024];
    size_t n = std::strftime(buf, sizeof(buf), f2.c_str(), &t);
    if (n + 1 > max) return Ret(0);
    WriteAnsi(m, dst, std::string(buf, n), max);
    return Ret(static_cast<uint32_t>(n));
}
HostCallOutcome C_ctime(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (!g_timeStrBuf) g_timeStrBuf = Malloc(m, 32, true);
    std::time_t tt = static_cast<int32_t>(m.Read32(Arg(c, m, 0)));
    std::tm t{};
#if defined(_WIN32)
    localtime_s(&t, &tt);
#else
    localtime_r(&tt, &t);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y\n", &t);
    WriteAnsi(m, g_timeStrBuf, buf, 32);
    return Ret(g_timeStrBuf);
}
HostCallOutcome C_tzset(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

// ===== Локаль =====
uint32_t g_lconv = 0;
HostCallOutcome C_setlocale(cpu::CpuState&, cpu::IGuestMemory& m) {
    static uint32_t c = 0;
    if (!c) c = NewString(m, "C");
    return Ret(c);
}
HostCallOutcome C_localeconv(cpu::CpuState&, cpu::IGuestMemory& m) {
    if (!g_lconv) {
        g_lconv = Malloc(m, 64, true);
        uint32_t dot = NewString(m, "."), empty = NewString(m, "");
        m.Write32(g_lconv + 0, dot); // decimal_point
        for (uint32_t i = 4; i < 40; i += 4) m.Write32(g_lconv + i, empty);
        for (uint32_t i = 40; i < 48; ++i) m.Write8(g_lconv + i, 0x7F); // CHAR_MAX
    }
    return Ret(g_lconv);
}

// ===== Регистрация =====
#define CDECL_ENTRY(name, fn) {"msvcrt.dll", name, 0, CallConv::Cdecl, &fn}
const HostFunctionEntry kEntries[] = {
    CDECL_ENTRY("__getmainargs", C_getmainargs),
    CDECL_ENTRY("__wgetmainargs", C_wgetmainargs),
    CDECL_ENTRY("__set_app_type", C_set_app_type),
    CDECL_ENTRY("__p__fmode", C_p_fmode),
    CDECL_ENTRY("__p__commode", C_p_commode),
    CDECL_ENTRY("__p___initenv", C_p_initenv),
    CDECL_ENTRY("__p__environ", C_p_environ),
    CDECL_ENTRY("__p__wenviron", C_p_wenviron),
    CDECL_ENTRY("__p___argc", C_p_argc),
    CDECL_ENTRY("__p___argv", C_p_argv),
    CDECL_ENTRY("__p___wargv", C_p_wargv),
    CDECL_ENTRY("__p__pgmptr", C_p_pgmptr),
    CDECL_ENTRY("__p__acmdln", C_p_acmdln),
    CDECL_ENTRY("__p__wcmdln", C_p_wcmdln),
    CDECL_ENTRY("__p___mb_cur_max", C_p_mb_cur_max),
    CDECL_ENTRY("___mb_cur_max_func", C_mb_cur_max_func),
    CDECL_ENTRY("__pctype_func", C_pctype_func),
    CDECL_ENTRY("___lc_codepage_func", C_lc_codepage_func),
    CDECL_ENTRY("__setusermatherr", C_setusermatherr),
    CDECL_ENTRY("_initterm", C_initterm),
    CDECL_ENTRY("_initterm_e", C_initterm_e),
    CDECL_ENTRY("_onexit", C_onexit),
    CDECL_ENTRY("atexit", C_atexit),
    CDECL_ENTRY("__dllonexit", C_dllonexit),
    CDECL_ENTRY("exit", C_exit),
    CDECL_ENTRY("_exit", C__exit),
    CDECL_ENTRY("_cexit", C_cexit),
    CDECL_ENTRY("_c_exit", C_c_exit),
    CDECL_ENTRY("_amsg_exit", C_amsg_exit),
    CDECL_ENTRY("abort", C_abort),
    CDECL_ENTRY("_purecall", C_purecall),
    CDECL_ENTRY("_XcptFilter", C_XcptFilter),
    CDECL_ENTRY("_errno", C_errno),
    CDECL_ENTRY("__doserrno", C_doserrno),
    CDECL_ENTRY("_get_errno", C_get_errno),
    CDECL_ENTRY("_set_errno", C_set_errno),
    CDECL_ENTRY("_control87", C_control87),
    CDECL_ENTRY("_controlfp", C_controlfp),
    CDECL_ENTRY("_statusfp", C_statusfp),
    CDECL_ENTRY("_clearfp", C_clearfp),
    CDECL_ENTRY("_fpreset", C_fpreset),
    CDECL_ENTRY("_setjmp", SetJmpCommon),
    CDECL_ENTRY("_setjmp3", SetJmpCommon),
    CDECL_ENTRY("longjmp", C_longjmp),
    CDECL_ENTRY("signal", C_signal),
    CDECL_ENTRY("raise", C_raise),
    CDECL_ENTRY("_lock", C_nop),
    CDECL_ENTRY("_unlock", C_nop),
    CDECL_ENTRY("_lock_file", C_nop),
    CDECL_ENTRY("_unlock_file", C_nop),
    CDECL_ENTRY("malloc", C_malloc),
    CDECL_ENTRY("calloc", C_calloc),
    CDECL_ENTRY("realloc", C_realloc),
    CDECL_ENTRY("free", C_free),
    CDECL_ENTRY("_msize", C_msize),
    CDECL_ENTRY("_expand", C_expand),
    CDECL_ENTRY("_heapchk", C_heapchk),
    CDECL_ENTRY("_heapmin", C_nop),
    CDECL_ENTRY("_get_heap_handle", C_get_heap_handle),
    CDECL_ENTRY("_set_new_handler", C_set_new_handler),
    CDECL_ENTRY("_query_new_handler", C_query_new_handler),
    CDECL_ENTRY("_set_new_mode", C_set_new_mode),
    CDECL_ENTRY("??2@YAPAXI@Z", C_operator_new),
    CDECL_ENTRY("??_U@YAPAXI@Z", C_operator_new),
    CDECL_ENTRY("??3@YAXPAX@Z", C_free),
    CDECL_ENTRY("??_V@YAXPAX@Z", C_free),
    CDECL_ENTRY("qsort", C_qsort),
    CDECL_ENTRY("bsearch", C_bsearch),
    CDECL_ENTRY("srand", C_srand),
    CDECL_ENTRY("rand", C_rand),
    CDECL_ENTRY("abs", C_abs),
    CDECL_ENTRY("labs", C_abs),
    CDECL_ENTRY("div", C_div),
    CDECL_ENTRY("ldiv", C_div),
    CDECL_ENTRY("_rotl", C_rotl),
    CDECL_ENTRY("_lrotl", C_rotl),
    CDECL_ENTRY("_rotr", C_rotr),
    CDECL_ENTRY("_lrotr", C_rotr),
    CDECL_ENTRY("getenv", C_getenv),
    CDECL_ENTRY("_wgetenv", C_wgetenv),
    CDECL_ENTRY("_putenv", C_putenv),
    CDECL_ENTRY("_getpid", C_getpid),
    CDECL_ENTRY("system", C_system),
    CDECL_ENTRY("strtol", C_strtol),
    CDECL_ENTRY("strtoul", C_strtoul),
    CDECL_ENTRY("wcstol", C_wcstol),
    CDECL_ENTRY("wcstoul", C_wcstoul),
    CDECL_ENTRY("_strtoi64", C_strtoi64),
    CDECL_ENTRY("_strtoui64", C_strtoui64),
    CDECL_ENTRY("atoi", C_atoi),
    CDECL_ENTRY("atol", C_atoi),
    CDECL_ENTRY("_wtoi", C_wtoi),
    CDECL_ENTRY("_wtol", C_wtoi),
    CDECL_ENTRY("_atoi64", C_atoi64),
    CDECL_ENTRY("atof", C_atof),
    CDECL_ENTRY("strtod", C_strtod),
    CDECL_ENTRY("wcstod", C_wcstod),
    CDECL_ENTRY("_itoa", C_itoa),
    CDECL_ENTRY("_ltoa", C_itoa),
    CDECL_ENTRY("_ultoa", C_ultoa),
    CDECL_ENTRY("_i64toa", C_i64toa),
    CDECL_ENTRY("_ui64toa", C_ui64toa),
    CDECL_ENTRY("_itow", C_itow),
    CDECL_ENTRY("_ltow", C_itow),
    CDECL_ENTRY("time", C_time),
    CDECL_ENTRY("_time64", C_time64),
    CDECL_ENTRY("clock", C_clock),
    CDECL_ENTRY("localtime", C_localtime),
    CDECL_ENTRY("gmtime", C_gmtime),
    CDECL_ENTRY("_localtime64", C_localtime64),
    CDECL_ENTRY("mktime", C_mktime),
    CDECL_ENTRY("difftime", C_difftime),
    CDECL_ENTRY("strftime", C_strftime),
    CDECL_ENTRY("ctime", C_ctime),
    CDECL_ENTRY("_tzset", C_tzset),
    CDECL_ENTRY("setlocale", C_setlocale),
    CDECL_ENTRY("localeconv", C_localeconv),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

// Импорты-данные msvcrt
uint32_t D_iob(cpu::IGuestMemory& m) { return G(m).iob; }
uint32_t D_mbCurMax(cpu::IGuestMemory& m) { return G(m).mbCurMax; }
uint32_t D_initenv(cpu::IGuestMemory& m) { return G(m).environ; }
uint32_t D_wenviron(cpu::IGuestMemory& m) { return G(m).wenviron; }
uint32_t D_fmode(cpu::IGuestMemory& m) { return G(m).fmode; }
uint32_t D_commode(cpu::IGuestMemory& m) { return G(m).commode; }
uint32_t D_argc(cpu::IGuestMemory& m) { return G(m).argc; }
uint32_t D_argv(cpu::IGuestMemory& m) { return G(m).argv; }
uint32_t D_wargv(cpu::IGuestMemory& m) { return G(m).wargv; }
uint32_t D_acmdln(cpu::IGuestMemory& m) { return G(m).acmdln; }
uint32_t D_wcmdln(cpu::IGuestMemory& m) { return G(m).wcmdln; }
uint32_t D_pctype(cpu::IGuestMemory& m) { return G(m).pctype; }
uint32_t D_pwctype(cpu::IGuestMemory& m) { return G(m).pwctype; }
uint32_t D_ctype(cpu::IGuestMemory& m) { return G(m).ctypeTable; }
uint32_t D_pgmptr(cpu::IGuestMemory& m) { return G(m).pgmptr; }
uint32_t D_osver(cpu::IGuestMemory& m) { return G(m).osver; }
uint32_t D_winver(cpu::IGuestMemory& m) { return G(m).winver; }
uint32_t D_winmajor(cpu::IGuestMemory& m) { return G(m).winmajor; }
uint32_t D_winminor(cpu::IGuestMemory& m) { return G(m).winminor; }
uint32_t D_timezone(cpu::IGuestMemory& m) { return G(m).timezone; }
uint32_t D_daylight(cpu::IGuestMemory& m) { return G(m).daylight; }
uint32_t D_tzname(cpu::IGuestMemory& m) { return G(m).tzname; }

const bool kDataRegistered = [] {
    RegisterDataImport("msvcrt.dll", "_iob", &D_iob);
    RegisterDataImport("msvcrt.dll", "__mb_cur_max", &D_mbCurMax);
    RegisterDataImport("msvcrt.dll", "__initenv", &D_initenv);
    RegisterDataImport("msvcrt.dll", "_environ", &D_initenv);
    RegisterDataImport("msvcrt.dll", "_wenviron", &D_wenviron);
    RegisterDataImport("msvcrt.dll", "__winitenv", &D_wenviron);
    RegisterDataImport("msvcrt.dll", "_fmode", &D_fmode);
    RegisterDataImport("msvcrt.dll", "_commode", &D_commode);
    RegisterDataImport("msvcrt.dll", "__argc", &D_argc);
    RegisterDataImport("msvcrt.dll", "__argv", &D_argv);
    RegisterDataImport("msvcrt.dll", "__wargv", &D_wargv);
    RegisterDataImport("msvcrt.dll", "_acmdln", &D_acmdln);
    RegisterDataImport("msvcrt.dll", "_wcmdln", &D_wcmdln);
    RegisterDataImport("msvcrt.dll", "_pctype", &D_pctype);
    RegisterDataImport("msvcrt.dll", "_pwctype", &D_pwctype);
    RegisterDataImport("msvcrt.dll", "_ctype", &D_ctype);
    RegisterDataImport("msvcrt.dll", "_pgmptr", &D_pgmptr);
    RegisterDataImport("msvcrt.dll", "_osver", &D_osver);
    RegisterDataImport("msvcrt.dll", "_winver", &D_winver);
    RegisterDataImport("msvcrt.dll", "_winmajor", &D_winmajor);
    RegisterDataImport("msvcrt.dll", "_winminor", &D_winminor);
    RegisterDataImport("msvcrt.dll", "_timezone", &D_timezone);
    RegisterDataImport("msvcrt.dll", "_daylight", &D_daylight);
    RegisterDataImport("msvcrt.dll", "_tzname", &D_tzname);
    return true;
}();

} // namespace

// Вызов из других файлов (exit из stdio и т.п.)
void RunAtExitHandlers(cpu::CpuState& c, cpu::IGuestMemory& m) { RunAtExit(c, m); }

} // namespace crt
} // namespace win32

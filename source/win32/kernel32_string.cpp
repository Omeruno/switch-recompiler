// kernel32_string.cpp — KERNEL32: строковые функции lstr* (ANSI и UTF-16).

#include <cctype>

#include "hle_util.h"

namespace win32 {
namespace {

using namespace hle;

uint32_t LenA(cpu::IGuestMemory& m, uint32_t p) {
    if (!p) return 0;
    uint32_t n = 0;
    while (m.Read8(p + n)) ++n;
    return n;
}
uint32_t LenW(cpu::IGuestMemory& m, uint32_t p) {
    if (!p) return 0;
    uint32_t n = 0;
    while (m.Read16(p + n * 2)) ++n;
    return n;
}
int CmpA(cpu::IGuestMemory& m, uint32_t a, uint32_t b, bool icase) {
    for (uint32_t i = 0;; ++i) {
        int x = m.Read8(a + i), y = m.Read8(b + i);
        if (icase) { x = std::tolower(x); y = std::tolower(y); }
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}
int CmpW(cpu::IGuestMemory& m, uint32_t a, uint32_t b, bool icase) {
    for (uint32_t i = 0;; ++i) {
        int x = m.Read16(a + i * 2), y = m.Read16(b + i * 2);
        if (icase) { if (x < 128) x = std::tolower(x); if (y < 128) y = std::tolower(y); }
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

HostCallOutcome K32_lstrlenA(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(LenA(m, Arg(c, m, 0))); }
HostCallOutcome K32_lstrlenW(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(LenW(m, Arg(c, m, 0))); }
HostCallOutcome K32_lstrcpyA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    uint32_t i = 0;
    for (;; ++i) { uint8_t ch = m.Read8(s + i); m.Write8(d + i, ch); if (!ch) break; }
    return Ret(d);
}
HostCallOutcome K32_lstrcpyW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    for (uint32_t i = 0;; ++i) { uint16_t ch = m.Read16(s + i * 2); m.Write16(d + i * 2, ch); if (!ch) break; }
    return Ret(d);
}
HostCallOutcome K32_lstrcpynA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    int32_t n = static_cast<int32_t>(Arg(c, m, 2));
    if (n <= 0) return Ret(d);
    int32_t i = 0;
    for (; i < n - 1; ++i) { uint8_t ch = m.Read8(s + i); if (!ch) break; m.Write8(d + i, ch); }
    m.Write8(d + i, 0);
    return Ret(d);
}
HostCallOutcome K32_lstrcpynW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    int32_t n = static_cast<int32_t>(Arg(c, m, 2));
    if (n <= 0) return Ret(d);
    int32_t i = 0;
    for (; i < n - 1; ++i) { uint16_t ch = m.Read16(s + i * 2); if (!ch) break; m.Write16(d + i * 2, ch); }
    m.Write16(d + i * 2, 0);
    return Ret(d);
}
HostCallOutcome K32_lstrcatA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    uint32_t off = LenA(m, d);
    for (uint32_t i = 0;; ++i) { uint8_t ch = m.Read8(s + i); m.Write8(d + off + i, ch); if (!ch) break; }
    return Ret(d);
}
HostCallOutcome K32_lstrcatW(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    uint32_t off = LenW(m, d);
    for (uint32_t i = 0;; ++i) { uint16_t ch = m.Read16(s + i * 2); m.Write16(d + (off + i) * 2, ch); if (!ch) break; }
    return Ret(d);
}
HostCallOutcome K32_lstrcmpA(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(CmpA(m, Arg(c, m, 0), Arg(c, m, 1), false))); }
HostCallOutcome K32_lstrcmpiA(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(CmpA(m, Arg(c, m, 0), Arg(c, m, 1), true))); }
HostCallOutcome K32_lstrcmpW(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(CmpW(m, Arg(c, m, 0), Arg(c, m, 1), false))); }
HostCallOutcome K32_lstrcmpiW(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(CmpW(m, Arg(c, m, 0), Arg(c, m, 1), true))); }

const HostFunctionEntry kEntries[] = {
    {"KERNEL32.dll", "lstrlenA", 4, CallConv::Stdcall, &K32_lstrlenA},
    {"KERNEL32.dll", "lstrlenW", 4, CallConv::Stdcall, &K32_lstrlenW},
    {"KERNEL32.dll", "lstrcpyA", 8, CallConv::Stdcall, &K32_lstrcpyA},
    {"KERNEL32.dll", "lstrcpyW", 8, CallConv::Stdcall, &K32_lstrcpyW},
    {"KERNEL32.dll", "lstrcpynA", 12, CallConv::Stdcall, &K32_lstrcpynA},
    {"KERNEL32.dll", "lstrcpynW", 12, CallConv::Stdcall, &K32_lstrcpynW},
    {"KERNEL32.dll", "lstrcatA", 8, CallConv::Stdcall, &K32_lstrcatA},
    {"KERNEL32.dll", "lstrcatW", 8, CallConv::Stdcall, &K32_lstrcatW},
    {"KERNEL32.dll", "lstrcmpA", 8, CallConv::Stdcall, &K32_lstrcmpA},
    {"KERNEL32.dll", "lstrcmpiA", 8, CallConv::Stdcall, &K32_lstrcmpiA},
    {"KERNEL32.dll", "lstrcmpW", 8, CallConv::Stdcall, &K32_lstrcmpW},
    {"KERNEL32.dll", "lstrcmpiW", 8, CallConv::Stdcall, &K32_lstrcmpiW},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32

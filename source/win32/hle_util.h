#pragma once
// hle_util.h
//
// Общие помощники для HLE-реализаций Win32 в отдельных файлах
// (kernel32_*.cpp и т.п.): аргументы stdcall/cdecl, строки ANSI/UTF-16 в
// памяти гостя, последняя ошибка в TEB.

#include <cstdint>
#include <string>
#include <vector>

#include "../cpu/cpu_state.h"
#include "../cpu/guest_memory.h"
#include "host_functions.h"
#include "message_loop.h"

namespace win32 {
namespace hle {

// i-й аргумент на стеке (сразу после адреса возврата).
inline uint32_t Arg(cpu::CpuState& c, cpu::IGuestMemory& m, int i) {
    return m.Read32(c.ESP() + 4 + 4 * static_cast<uint32_t>(i));
}

inline HostCallOutcome Ret(uint32_t v) {
    HostCallOutcome r;
    r.returnValue = v;
    return r;
}

inline std::string ReadAnsi(cpu::IGuestMemory& m, uint32_t addr, size_t max = 32768) {
    std::string s;
    if (!addr) return s;
    for (size_t i = 0; i < max; ++i) {
        uint8_t ch = m.Read8(addr + static_cast<uint32_t>(i));
        if (!ch) break;
        s.push_back(static_cast<char>(ch));
    }
    return s;
}

inline std::u16string ReadWide(cpu::IGuestMemory& m, uint32_t addr, size_t max = 32768) {
    std::u16string s;
    if (!addr) return s;
    for (size_t i = 0; i < max; ++i) {
        uint16_t ch = m.Read16(addr + static_cast<uint32_t>(i * 2));
        if (!ch) break;
        s.push_back(static_cast<char16_t>(ch));
    }
    return s;
}

// UTF-16 -> UTF-8 (для путей и вывода в консоль хоста)
inline std::string WideToUtf8(const std::u16string& w) {
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = w[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size()) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }
    return out;
}

inline std::u16string AnsiToWide(const std::string& s) {
    std::u16string w;
    for (unsigned char ch : s) w.push_back(static_cast<char16_t>(ch));
    return w;
}

// Записывает строку с завершающим нулём, не больше cap символов (включая
// ноль). Возвращает число записанных символов без нуля.
inline uint32_t WriteAnsi(cpu::IGuestMemory& m, uint32_t addr, const std::string& s, uint32_t cap) {
    if (!addr || !cap) return 0;
    uint32_t n = static_cast<uint32_t>(s.size());
    if (n >= cap) n = cap - 1;
    for (uint32_t i = 0; i < n; ++i) m.Write8(addr + i, static_cast<uint8_t>(s[i]));
    m.Write8(addr + n, 0);
    return n;
}
inline uint32_t WriteWide(cpu::IGuestMemory& m, uint32_t addr, const std::u16string& s, uint32_t cap) {
    if (!addr || !cap) return 0;
    uint32_t n = static_cast<uint32_t>(s.size());
    if (n >= cap) n = cap - 1;
    for (uint32_t i = 0; i < n; ++i) m.Write16(addr + i * 2, s[i]);
    m.Write16(addr + n * 2, 0);
    return n;
}

// Последняя ошибка потока: TEB+0x34 (LastErrorValue), как в Windows.
inline void SetLastError(cpu::IGuestMemory& m, uint32_t code) {
    if (g_tebAddr) m.Write32(g_tebAddr + 0x34, code);
}
inline uint32_t GetLastError(cpu::IGuestMemory& m) {
    return g_tebAddr ? m.Read32(g_tebAddr + 0x34) : 0;
}

// Вызов cdecl-функции гостя из host-функции (компаратор qsort, atexit,
// инициализаторы): аргументы снимает вызывающий, то есть мы.
inline uint32_t CallCdecl(cpu::CpuState& c, cpu::IGuestMemory& m, uint32_t fn, const std::vector<uint32_t>& args) {
    uint32_t esp = c.ESP();
    uint32_t r = CallGuestFunctionStdcall(c, m, fn, args, g_tebAddr, g_tebAddr);
    c.ESP() = esp;
    return r;
}
inline uint32_t CallStdcall(cpu::CpuState& c, cpu::IGuestMemory& m, uint32_t fn, const std::vector<uint32_t>& args) {
    uint32_t esp = c.ESP();
    uint32_t r = CallGuestFunctionStdcall(c, m, fn, args, g_tebAddr, g_tebAddr);
    c.ESP() = esp;
    return r;
}

constexpr uint32_t ERROR_SUCCESS = 0;
constexpr uint32_t ERROR_FILE_NOT_FOUND = 2;
constexpr uint32_t ERROR_PATH_NOT_FOUND = 3;
constexpr uint32_t ERROR_ACCESS_DENIED = 5;
constexpr uint32_t ERROR_INVALID_HANDLE = 6;
constexpr uint32_t ERROR_NOT_ENOUGH_MEMORY = 8;
constexpr uint32_t ERROR_INVALID_PARAMETER = 87;
constexpr uint32_t ERROR_INSUFFICIENT_BUFFER = 122;
constexpr uint32_t ERROR_MOD_NOT_FOUND = 126;
constexpr uint32_t ERROR_PROC_NOT_FOUND = 127;
constexpr uint32_t ERROR_NO_MORE_FILES = 18;
constexpr uint32_t ERROR_ALREADY_EXISTS = 183;

} // namespace hle
} // namespace win32

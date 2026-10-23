// msvcrt_string.cpp — msvcrt.dll: функции строк и памяти (str*, mem*,
// wcs*, _str*, _mbs* в однобайтовом варианте), классификация символов.
//
// Работают прямо с памятью гостя; блочные операции идут кусками по
// страницам, чтобы большие memcpy/memset не были побайтовыми.

#include <cctype>
#include <cstring>
#include <vector>

#include "msvcrt.h"

namespace win32 {
namespace crt {
namespace {

using namespace hle;

uint32_t StrLen(cpu::IGuestMemory& m, uint32_t p) {
    uint32_t n = 0;
    while (m.Read8(p + n)) ++n;
    return n;
}
uint32_t WcsLen(cpu::IGuestMemory& m, uint32_t p) {
    uint32_t n = 0;
    while (m.Read16(p + n * 2)) ++n;
    return n;
}

void Copy(cpu::IGuestMemory& m, uint32_t dst, uint32_t src, uint32_t n) {
    if (!n) return;
    std::vector<uint8_t> tmp(n);
    m.ReadBytes(src, tmp.data(), n);
    m.LoadBytes(dst, tmp.data(), n);
}

int Lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int Upper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

// --- mem* ---
HostCallOutcome C_memcpy(cpu::CpuState& c, cpu::IGuestMemory& m) { Copy(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2)); return Ret(Arg(c, m, 0)); }
HostCallOutcome C_memset(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), v = Arg(c, m, 1) & 0xFF, n = Arg(c, m, 2);
    for (uint32_t off = 0; off < n;) {
        uint8_t* p = m.HostPointer(d + off);
        uint32_t chunk = cpu::PagedGuestMemory::kPageSize - ((d + off) & cpu::PagedGuestMemory::kPageMask);
        if (chunk > n - off) chunk = n - off;
        if (!p) { m.Write8(d + off, 0); break; } // сбой доступа
        std::memset(p, static_cast<int>(v), chunk);
        off += chunk;
    }
    return Ret(d);
}
HostCallOutcome C_memcmp(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t a = Arg(c, m, 0), b = Arg(c, m, 1), n = Arg(c, m, 2);
    for (uint32_t i = 0; i < n; ++i) {
        int x = m.Read8(a + i), y = m.Read8(b + i);
        if (x != y) return Ret(static_cast<uint32_t>(x < y ? -1 : 1));
    }
    return Ret(0);
}
HostCallOutcome C_memicmp(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t a = Arg(c, m, 0), b = Arg(c, m, 1), n = Arg(c, m, 2);
    for (uint32_t i = 0; i < n; ++i) {
        int x = Lower(m.Read8(a + i)), y = Lower(m.Read8(b + i));
        if (x != y) return Ret(static_cast<uint32_t>(x < y ? -1 : 1));
    }
    return Ret(0);
}
HostCallOutcome C_memchr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = Arg(c, m, 1) & 0xFF, n = Arg(c, m, 2);
    for (uint32_t i = 0; i < n; ++i) if (m.Read8(p + i) == v) return Ret(p + i);
    return Ret(0);
}

// --- str* ---
HostCallOutcome C_strlen(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(StrLen(m, Arg(c, m, 0))); }
HostCallOutcome C_strnlen(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), max = Arg(c, m, 1), n = 0;
    while (n < max && m.Read8(p + n)) ++n;
    return Ret(n);
}
HostCallOutcome C_strcpy(cpu::CpuState& c, cpu::IGuestMemory& m) { Copy(m, Arg(c, m, 0), Arg(c, m, 1), StrLen(m, Arg(c, m, 1)) + 1); return Ret(Arg(c, m, 0)); }
HostCallOutcome C_strncpy(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1), n = Arg(c, m, 2);
    uint32_t i = 0;
    for (; i < n; ++i) { uint8_t ch = m.Read8(s + i); m.Write8(d + i, ch); if (!ch) break; }
    for (; i < n; ++i) m.Write8(d + i, 0);
    return Ret(d);
}
HostCallOutcome C_strcat(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    Copy(m, d + StrLen(m, d), s, StrLen(m, s) + 1);
    return Ret(d);
}
HostCallOutcome C_strncat(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1), n = Arg(c, m, 2);
    uint32_t off = StrLen(m, d), i = 0;
    for (; i < n; ++i) { uint8_t ch = m.Read8(s + i); if (!ch) break; m.Write8(d + off + i, ch); }
    m.Write8(d + off + i, 0);
    return Ret(d);
}
int StrCmpN(cpu::IGuestMemory& m, uint32_t a, uint32_t b, uint32_t n, bool icase) {
    for (uint32_t i = 0; i < n; ++i) {
        int x = m.Read8(a + i), y = m.Read8(b + i);
        if (icase) { x = Lower(x); y = Lower(y); }
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}
HostCallOutcome C_strcmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(StrCmpN(m, Arg(c, m, 0), Arg(c, m, 1), 0xFFFFFFFFu, false))); }
HostCallOutcome C_strncmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(StrCmpN(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), false))); }
HostCallOutcome C_stricmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(StrCmpN(m, Arg(c, m, 0), Arg(c, m, 1), 0xFFFFFFFFu, true))); }
HostCallOutcome C_strnicmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(StrCmpN(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), true))); }
HostCallOutcome C_strchr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = Arg(c, m, 1) & 0xFF;
    for (;; ++p) { uint8_t ch = m.Read8(p); if (ch == v) return Ret(p); if (!ch) return Ret(0); }
}
HostCallOutcome C_strrchr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = Arg(c, m, 1) & 0xFF, last = 0;
    for (;; ++p) { uint8_t ch = m.Read8(p); if (ch == v) last = p; if (!ch) return Ret(last); }
}
HostCallOutcome C_strstr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string h = ReadAnsi(m, Arg(c, m, 0)), n = ReadAnsi(m, Arg(c, m, 1));
    size_t pos = h.find(n);
    return Ret(pos == std::string::npos ? 0 : Arg(c, m, 0) + static_cast<uint32_t>(pos));
}
HostCallOutcome C_strspn(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadAnsi(m, Arg(c, m, 0)), set = ReadAnsi(m, Arg(c, m, 1));
    return Ret(static_cast<uint32_t>(std::strspn(s.c_str(), set.c_str())));
}
HostCallOutcome C_strcspn(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadAnsi(m, Arg(c, m, 0)), set = ReadAnsi(m, Arg(c, m, 1));
    return Ret(static_cast<uint32_t>(std::strcspn(s.c_str(), set.c_str())));
}
HostCallOutcome C_strpbrk(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::string s = ReadAnsi(m, Arg(c, m, 0)), set = ReadAnsi(m, Arg(c, m, 1));
    size_t pos = s.find_first_of(set);
    return Ret(pos == std::string::npos ? 0 : Arg(c, m, 0) + static_cast<uint32_t>(pos));
}
HostCallOutcome C_wcspbrk(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::u16string s = ReadWide(m, Arg(c, m, 0)), set = ReadWide(m, Arg(c, m, 1));
    size_t pos = s.find_first_of(set);
    return Ret(pos == std::u16string::npos ? 0 : Arg(c, m, 0) + static_cast<uint32_t>(pos * 2));
}

// _splitpath: диск «X:», каталог до последней косой черты включительно,
// имя и расширение (с последней точки после каталога)
template <int W>
HostCallOutcome SplitPath(cpu::IGuestMemory& m, uint32_t path, const uint32_t out[4], const uint32_t cap[4], bool secure) {
    std::u16string p = W == 2 ? ReadWide(m, path) : AnsiToWide(ReadAnsi(m, path));
    std::u16string part[4];
    size_t i = 0;
    if (p.size() >= 2 && p[1] == u':') { part[0] = p.substr(0, 2); i = 2; }
    size_t slash = p.find_last_of(u"\\/");
    size_t nameStart = (slash == std::u16string::npos || slash < i) ? i : slash + 1;
    part[1] = p.substr(i, nameStart - i);
    size_t dot = p.find_last_of(u'.');
    if (dot == std::u16string::npos || dot < nameStart) dot = p.size();
    part[2] = p.substr(nameStart, dot - nameStart);
    part[3] = p.substr(dot);
    for (int k = 0; k < 4; ++k) {
        if (!out[k]) continue;
        if (secure && part[k].size() + 1 > cap[k]) {
            for (int j = 0; j < 4; ++j) if (out[j] && cap[j]) { if (W == 2) m.Write16(out[j], 0); else m.Write8(out[j], 0); }
            return Ret(34); // ERANGE
        }
        if (W == 2) WriteWide(m, out[k], part[k], static_cast<uint32_t>(part[k].size() + 1));
        else WriteAnsi(m, out[k], std::string(part[k].begin(), part[k].end()), static_cast<uint32_t>(part[k].size() + 1));
    }
    return Ret(0);
}
template <int W>
HostCallOutcome C_splitpath(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t out[4] = {Arg(c, m, 1), Arg(c, m, 2), Arg(c, m, 3), Arg(c, m, 4)};
    const uint32_t cap[4] = {0, 0, 0, 0};
    return SplitPath<W>(m, Arg(c, m, 0), out, cap, false);
}
template <int W>
HostCallOutcome C_splitpath_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    const uint32_t out[4] = {Arg(c, m, 1), Arg(c, m, 3), Arg(c, m, 5), Arg(c, m, 7)};
    const uint32_t cap[4] = {Arg(c, m, 2), Arg(c, m, 4), Arg(c, m, 6), Arg(c, m, 8)};
    if (!Arg(c, m, 0)) return Ret(22); // EINVAL
    return SplitPath<W>(m, Arg(c, m, 0), out, cap, true);
}
struct StringState {uint32_t strtokNext=0,errorBuf=0;};
StringState& Strings() {return rt::LocalState<StringState>();}
HostCallOutcome C_strtok(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t s = Arg(c, m, 0);
    std::string delim = ReadAnsi(m, Arg(c, m, 1));
    if (!s) s = Strings().strtokNext;
    if (!s) return Ret(0);
    while (m.Read8(s) && delim.find(static_cast<char>(m.Read8(s))) != std::string::npos) ++s;
    if (!m.Read8(s)) { Strings().strtokNext = 0; return Ret(0); }
    uint32_t start = s;
    while (m.Read8(s) && delim.find(static_cast<char>(m.Read8(s))) == std::string::npos) ++s;
    if (m.Read8(s)) { m.Write8(s, 0); Strings().strtokNext = s + 1; } else Strings().strtokNext = 0;
    return Ret(start);
}
HostCallOutcome C_strdup(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t s = Arg(c, m, 0);
    if (!s) return Ret(0);
    uint32_t n = StrLen(m, s) + 1;
    uint32_t d = Malloc(m, n);
    Copy(m, d, s, n);
    return Ret(d);
}
HostCallOutcome C_strupr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    for (uint32_t i = 0;; ++i) { uint8_t ch = m.Read8(p + i); if (!ch) break; m.Write8(p + i, static_cast<uint8_t>(Upper(ch))); }
    return Ret(p);
}
HostCallOutcome C_strlwr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    for (uint32_t i = 0;; ++i) { uint8_t ch = m.Read8(p + i); if (!ch) break; m.Write8(p + i, static_cast<uint8_t>(Lower(ch))); }
    return Ret(p);
}
HostCallOutcome C_strrev(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), n = StrLen(m, p);
    for (uint32_t i = 0; i < n / 2; ++i) {
        uint8_t a = m.Read8(p + i), b = m.Read8(p + n - 1 - i);
        m.Write8(p + i, b);
        m.Write8(p + n - 1 - i, a);
    }
    return Ret(p);
}
HostCallOutcome C_strset(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = Arg(c, m, 1) & 0xFF;
    for (uint32_t i = 0; m.Read8(p + i); ++i) m.Write8(p + i, static_cast<uint8_t>(v));
    return Ret(p);
}
HostCallOutcome C_strerror(cpu::CpuState& c, cpu::IGuestMemory& m) {
    auto& buf=Strings().errorBuf;
    if (!buf) buf = Malloc(m, 96, true);
    int e = static_cast<int>(Arg(c, m, 0));
    const char* msg;
    switch (e) {
        case 0: msg = "No error"; break;
        case 2: msg = "No such file or directory"; break;
        case 9: msg = "Bad file descriptor"; break;
        case 12: msg = "Not enough space"; break;
        case 13: msg = "Permission denied"; break;
        case 17: msg = "File exists"; break;
        case 22: msg = "Invalid argument"; break;
        case 33: msg = "Domain error"; break;
        case 34: msg = "Result too large"; break;
        default: msg = "Unknown error"; break;
    }
    WriteAnsi(m, buf, msg, 96);
    return Ret(buf);
}
HostCallOutcome C_strcoll(cpu::CpuState& c, cpu::IGuestMemory& m) { return C_strcmp(c, m); }

// --- wcs* ---
HostCallOutcome C_wcslen(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(WcsLen(m, Arg(c, m, 0))); }
HostCallOutcome C_wcscpy(cpu::CpuState& c, cpu::IGuestMemory& m) { Copy(m, Arg(c, m, 0), Arg(c, m, 1), (WcsLen(m, Arg(c, m, 1)) + 1) * 2); return Ret(Arg(c, m, 0)); }
HostCallOutcome C_wcsncpy(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1), n = Arg(c, m, 2), i = 0;
    for (; i < n; ++i) { uint16_t ch = m.Read16(s + i * 2); m.Write16(d + i * 2, ch); if (!ch) break; }
    for (; i < n; ++i) m.Write16(d + i * 2, 0);
    return Ret(d);
}
HostCallOutcome C_wcscat(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1);
    Copy(m, d + WcsLen(m, d) * 2, s, (WcsLen(m, s) + 1) * 2);
    return Ret(d);
}
int WcsCmpN(cpu::IGuestMemory& m, uint32_t a, uint32_t b, uint32_t n, bool icase) {
    for (uint32_t i = 0; i < n; ++i) {
        int x = m.Read16(a + i * 2), y = m.Read16(b + i * 2);
        if (icase) { x = Lower(x); y = Lower(y); }
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}
HostCallOutcome C_wcscmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(WcsCmpN(m, Arg(c, m, 0), Arg(c, m, 1), 0xFFFFFFFFu, false))); }
HostCallOutcome C_wcsncmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(WcsCmpN(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), false))); }
HostCallOutcome C_wcsicmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(WcsCmpN(m, Arg(c, m, 0), Arg(c, m, 1), 0xFFFFFFFFu, true))); }
HostCallOutcome C_wcsnicmp(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(WcsCmpN(m, Arg(c, m, 0), Arg(c, m, 1), Arg(c, m, 2), true))); }
HostCallOutcome C_wcschr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = Arg(c, m, 1) & 0xFFFF;
    for (;; p += 2) { uint16_t ch = m.Read16(p); if (ch == v) return Ret(p); if (!ch) return Ret(0); }
}
HostCallOutcome C_wcsrchr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), v = Arg(c, m, 1) & 0xFFFF, last = 0;
    for (;; p += 2) { uint16_t ch = m.Read16(p); if (ch == v) last = p; if (!ch) return Ret(last); }
}
HostCallOutcome C_wcsstr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::u16string h = ReadWide(m, Arg(c, m, 0)), n = ReadWide(m, Arg(c, m, 1));
    size_t pos = h.find(n);
    return Ret(pos == std::u16string::npos ? 0 : Arg(c, m, 0) + static_cast<uint32_t>(pos * 2));
}
HostCallOutcome C_wcsdup(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t s = Arg(c, m, 0);
    if (!s) return Ret(0);
    uint32_t n = (WcsLen(m, s) + 1) * 2;
    uint32_t d = Malloc(m, n);
    Copy(m, d, s, n);
    return Ret(d);
}
HostCallOutcome C_wcsupr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    for (uint32_t i = 0;; ++i) { uint16_t ch = m.Read16(p + i * 2); if (!ch) break; m.Write16(p + i * 2, static_cast<uint16_t>(Upper(ch))); }
    return Ret(p);
}
HostCallOutcome C_wcslwr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    for (uint32_t i = 0;; ++i) { uint16_t ch = m.Read16(p + i * 2); if (!ch) break; m.Write16(p + i * 2, static_cast<uint16_t>(Lower(ch))); }
    return Ret(p);
}
HostCallOutcome C_wcsspn(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::u16string s = ReadWide(m, Arg(c, m, 0)), set = ReadWide(m, Arg(c, m, 1));
    size_t i = 0;
    while (i < s.size() && set.find(s[i]) != std::u16string::npos) ++i;
    return Ret(static_cast<uint32_t>(i));
}
HostCallOutcome C_wcscspn(cpu::CpuState& c, cpu::IGuestMemory& m) {
    std::u16string s = ReadWide(m, Arg(c, m, 0)), set = ReadWide(m, Arg(c, m, 1));
    size_t i = 0;
    while (i < s.size() && set.find(s[i]) == std::u16string::npos) ++i;
    return Ret(static_cast<uint32_t>(i));
}

// mbstowcs/wcstombs в «C»-локали: байт <-> символ
HostCallOutcome C_mbstowcs(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1), n = Arg(c, m, 2);
    uint32_t len = StrLen(m, s);
    if (!d) return Ret(len);
    uint32_t i = 0;
    for (; i < n && i < len; ++i) m.Write16(d + i * 2, m.Read8(s + i));
    if (i < n) m.Write16(d + i * 2, 0);
    return Ret(i);
}
HostCallOutcome C_wcstombs(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1), n = Arg(c, m, 2);
    uint32_t len = WcsLen(m, s);
    if (!d) return Ret(len);
    uint32_t i = 0;
    for (; i < n && i < len; ++i) { uint16_t ch = m.Read16(s + i * 2); m.Write8(d + i, static_cast<uint8_t>(ch < 256 ? ch : '?')); }
    if (i < n) m.Write8(d + i, 0);
    return Ret(i);
}
HostCallOutcome C_mbtowc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1), n = Arg(c, m, 2);
    if (!s) return Ret(0);
    if (!n) return Ret(static_cast<uint32_t>(-1));
    uint8_t ch = m.Read8(s);
    if (d) m.Write16(d, ch);
    return Ret(ch ? 1 : 0);
}
HostCallOutcome C_wctomb(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), w = Arg(c, m, 1) & 0xFFFF;
    if (!d) return Ret(0);
    if (w > 255) return Ret(static_cast<uint32_t>(-1));
    m.Write8(d, static_cast<uint8_t>(w));
    return Ret(1);
}

// --- ctype ---
using CtypeFn = int (*)(int);
template <CtypeFn F> HostCallOutcome Ctype(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int ch = static_cast<int>(Arg(c, m, 0));
    if (ch < -1 || ch > 255) return Ret(0);
    return Ret(ch == -1 ? 0 : (F(ch) ? 1 : 0));
}
int IsAlpha(int ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); }
int IsDigit(int ch) { return ch >= '0' && ch <= '9'; }
int IsAlnum(int ch) { return IsAlpha(ch) || IsDigit(ch); }
int IsSpace(int ch) { return ch == ' ' || (ch >= 9 && ch <= 13); }
int IsUpper(int ch) { return ch >= 'A' && ch <= 'Z'; }
int IsLower(int ch) { return ch >= 'a' && ch <= 'z'; }
int IsXDigit(int ch) { return IsDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'); }
int IsPunct(int ch) { return ch > 32 && ch < 127 && !IsAlnum(ch); }
int IsPrint(int ch) { return ch >= 32 && ch < 127; }
int IsGraph(int ch) { return ch > 32 && ch < 127; }
int IsCntrl(int ch) { return ch < 32 || ch == 127; }
HostCallOutcome C_toupper(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(Upper(static_cast<int>(Arg(c, m, 0))))); }
HostCallOutcome C_tolower(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(Lower(static_cast<int>(Arg(c, m, 0))))); }
HostCallOutcome C_towupper(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(Upper(static_cast<int>(Arg(c, m, 0) & 0xFFFF)))); }
HostCallOutcome C_towlower(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(static_cast<uint32_t>(Lower(static_cast<int>(Arg(c, m, 0) & 0xFFFF)))); }
// int _isctype(int c, int mask)
HostCallOutcome C_isctype(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int ch = static_cast<int>(Arg(c, m, 0));
    uint32_t mask = Arg(c, m, 1);
    if (ch < -1 || ch > 255) return Ret(0);
    uint16_t t = m.Read16(G(m).ctypeTable + 2 + ch * 2);
    return Ret(t & mask);
}
HostCallOutcome C_iswctype(cpu::CpuState& c, cpu::IGuestMemory& m) {
    int ch = static_cast<int>(Arg(c, m, 0) & 0xFFFF);
    uint32_t mask = Arg(c, m, 1);
    if (ch > 255) return Ret((mask & 0x103) ? 1 : 0); // всё не-ASCII считаем буквой
    return Ret(m.Read16(G(m).ctypeTable + 2 + ch * 2) & mask);
}

#define CDECL_ENTRY(name, fn) {"msvcrt.dll", name, 0, CallConv::Cdecl, &fn}
// --- безопасные варианты *_s: (dst, размер в символах, src[, count]) -> errno ---

template <int W>
uint32_t ReadCh(cpu::IGuestMemory& m, uint32_t p, uint32_t i) { return W == 1 ? m.Read8(p + i) : m.Read16(p + i * 2); }
template <int W>
void WriteCh(cpu::IGuestMemory& m, uint32_t p, uint32_t i, uint32_t v) {
    if (W == 1) m.Write8(p + i, static_cast<uint8_t>(v)); else m.Write16(p + i * 2, static_cast<uint16_t>(v));
}
// Дописывает src (не больше count символов) к dst начиная с позиции start
template <int W>
HostCallOutcome SecureAppend(cpu::IGuestMemory& m, uint32_t dst, uint32_t size, uint32_t start, uint32_t src, uint32_t count) {
    if (!dst || !size) return Ret(22); // EINVAL
    if (!src) { WriteCh<W>(m, dst, 0, 0); return Ret(22); }
    uint32_t i = 0;
    for (; i < count; ++i) {
        uint32_t ch = ReadCh<W>(m, src, i);
        if (!ch) break;
        if (start + i + 1 >= size) { WriteCh<W>(m, dst, 0, 0); return Ret(34); } // ERANGE
        WriteCh<W>(m, dst, start + i, ch);
    }
    WriteCh<W>(m, dst, start + i, 0);
    return Ret(0);
}
template <int W>
uint32_t Len(cpu::IGuestMemory& m, uint32_t p, uint32_t max) {
    uint32_t n = 0;
    while (n < max && ReadCh<W>(m, p, n)) ++n;
    return n;
}
template <int W>
HostCallOutcome C_cpy_s(cpu::CpuState& c, cpu::IGuestMemory& m) { return SecureAppend<W>(m, Arg(c, m, 0), Arg(c, m, 1), 0, Arg(c, m, 2), 0xFFFFFFFFu); }
template <int W>
HostCallOutcome C_ncpy_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t n = Arg(c, m, 3);
    if (n == 0xFFFFFFFFu) n = Arg(c, m, 1) - 1; // _TRUNCATE
    return SecureAppend<W>(m, Arg(c, m, 0), Arg(c, m, 1), 0, Arg(c, m, 2), n);
}
template <int W>
HostCallOutcome C_cat_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t dst = Arg(c, m, 0), size = Arg(c, m, 1);
    if (!dst || !size) return Ret(22);
    uint32_t len = Len<W>(m, dst, size);
    if (len >= size) return Ret(22);
    return SecureAppend<W>(m, dst, size, len, Arg(c, m, 2), 0xFFFFFFFFu);
}
template <int W>
HostCallOutcome C_ncat_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t dst = Arg(c, m, 0), size = Arg(c, m, 1), n = Arg(c, m, 3);
    if (!dst || !size) return Ret(22);
    uint32_t len = Len<W>(m, dst, size);
    if (len >= size) return Ret(22);
    if (n == 0xFFFFFFFFu) n = size - 1 - len;
    return SecureAppend<W>(m, dst, size, len, Arg(c, m, 2), n);
}
// memcpy_s/memmove_s(dst, dstSize, src, count)
HostCallOutcome C_memcpy_s(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t dst = Arg(c, m, 0), size = Arg(c, m, 1), src = Arg(c, m, 2), n = Arg(c, m, 3);
    if (!n) return Ret(0);
    if (!dst || !src) return Ret(22);
    if (n > size) return Ret(34);
    Copy(m, dst, src, n);
    return Ret(0);
}

const HostFunctionEntry kEntries[] = {
    CDECL_ENTRY("memcpy", C_memcpy), CDECL_ENTRY("memmove", C_memcpy), CDECL_ENTRY("memset", C_memset),
    CDECL_ENTRY("memcmp", C_memcmp), CDECL_ENTRY("_memicmp", C_memicmp), CDECL_ENTRY("memchr", C_memchr),
    CDECL_ENTRY("strlen", C_strlen), CDECL_ENTRY("strnlen", C_strnlen), CDECL_ENTRY("strcpy", C_strcpy),
    CDECL_ENTRY("strncpy", C_strncpy), CDECL_ENTRY("strcat", C_strcat), CDECL_ENTRY("strncat", C_strncat),
    CDECL_ENTRY("strcmp", C_strcmp), CDECL_ENTRY("strncmp", C_strncmp), CDECL_ENTRY("_stricmp", C_stricmp),
    CDECL_ENTRY("_strcmpi", C_stricmp), CDECL_ENTRY("_strnicmp", C_strnicmp), CDECL_ENTRY("strchr", C_strchr),
    CDECL_ENTRY("strrchr", C_strrchr), CDECL_ENTRY("strstr", C_strstr), CDECL_ENTRY("strspn", C_strspn),
    CDECL_ENTRY("strcspn", C_strcspn), CDECL_ENTRY("strpbrk", C_strpbrk), CDECL_ENTRY("strtok", C_strtok),
    CDECL_ENTRY("_strdup", C_strdup), CDECL_ENTRY("_strupr", C_strupr), CDECL_ENTRY("_strlwr", C_strlwr),
    CDECL_ENTRY("_strrev", C_strrev), CDECL_ENTRY("_strset", C_strset), CDECL_ENTRY("strerror", C_strerror),
    CDECL_ENTRY("strcoll", C_strcoll), CDECL_ENTRY("_stricoll", C_stricmp),
    CDECL_ENTRY("_mbscmp", C_strcmp), CDECL_ENTRY("_mbsicmp", C_stricmp), CDECL_ENTRY("_mbschr", C_strchr),
    CDECL_ENTRY("_mbsrchr", C_strrchr), CDECL_ENTRY("_mbsstr", C_strstr), CDECL_ENTRY("_mbslen", C_strlen),
    CDECL_ENTRY("_mbsdup", C_strdup), CDECL_ENTRY("_mbsnbcpy", C_strncpy), CDECL_ENTRY("_mbsupr", C_strupr),
    CDECL_ENTRY("wcslen", C_wcslen), CDECL_ENTRY("wcscpy", C_wcscpy), CDECL_ENTRY("wcsncpy", C_wcsncpy),
    CDECL_ENTRY("wcscat", C_wcscat), CDECL_ENTRY("wcscmp", C_wcscmp), CDECL_ENTRY("wcsncmp", C_wcsncmp),
    CDECL_ENTRY("_wcsicmp", C_wcsicmp), CDECL_ENTRY("_wcsnicmp", C_wcsnicmp), CDECL_ENTRY("wcschr", C_wcschr),
    CDECL_ENTRY("wcsrchr", C_wcsrchr), CDECL_ENTRY("wcsstr", C_wcsstr), CDECL_ENTRY("_wcsdup", C_wcsdup),
    CDECL_ENTRY("_wcsupr", C_wcsupr), CDECL_ENTRY("_wcslwr", C_wcslwr), CDECL_ENTRY("wcsspn", C_wcsspn),
    CDECL_ENTRY("wcscspn", C_wcscspn), CDECL_ENTRY("wcspbrk", C_wcspbrk),
    CDECL_ENTRY("_splitpath", C_splitpath<1>), CDECL_ENTRY("_wsplitpath", C_splitpath<2>),
    CDECL_ENTRY("_splitpath_s", C_splitpath_s<1>), CDECL_ENTRY("_wsplitpath_s", C_splitpath_s<2>),
    CDECL_ENTRY("strcpy_s", C_cpy_s<1>), CDECL_ENTRY("wcscpy_s", C_cpy_s<2>), CDECL_ENTRY("strncpy_s", C_ncpy_s<1>),
    CDECL_ENTRY("wcsncpy_s", C_ncpy_s<2>), CDECL_ENTRY("strcat_s", C_cat_s<1>), CDECL_ENTRY("wcscat_s", C_cat_s<2>),
    CDECL_ENTRY("strncat_s", C_ncat_s<1>), CDECL_ENTRY("wcsncat_s", C_ncat_s<2>), CDECL_ENTRY("memcpy_s", C_memcpy_s),
    CDECL_ENTRY("memmove_s", C_memcpy_s),
    CDECL_ENTRY("mbstowcs", C_mbstowcs), CDECL_ENTRY("wcstombs", C_wcstombs), CDECL_ENTRY("mbtowc", C_mbtowc),
    CDECL_ENTRY("wctomb", C_wctomb),
    CDECL_ENTRY("isalpha", Ctype<IsAlpha>), CDECL_ENTRY("isdigit", Ctype<IsDigit>), CDECL_ENTRY("isalnum", Ctype<IsAlnum>),
    CDECL_ENTRY("isspace", Ctype<IsSpace>), CDECL_ENTRY("isupper", Ctype<IsUpper>), CDECL_ENTRY("islower", Ctype<IsLower>),
    CDECL_ENTRY("isxdigit", Ctype<IsXDigit>), CDECL_ENTRY("ispunct", Ctype<IsPunct>), CDECL_ENTRY("isprint", Ctype<IsPrint>),
    CDECL_ENTRY("isgraph", Ctype<IsGraph>), CDECL_ENTRY("iscntrl", Ctype<IsCntrl>),
    CDECL_ENTRY("iswalpha", Ctype<IsAlpha>), CDECL_ENTRY("iswdigit", Ctype<IsDigit>), CDECL_ENTRY("iswalnum", Ctype<IsAlnum>),
    CDECL_ENTRY("iswspace", Ctype<IsSpace>), CDECL_ENTRY("iswupper", Ctype<IsUpper>), CDECL_ENTRY("iswlower", Ctype<IsLower>),
    CDECL_ENTRY("iswxdigit", Ctype<IsXDigit>), CDECL_ENTRY("iswpunct", Ctype<IsPunct>), CDECL_ENTRY("iswprint", Ctype<IsPrint>),
    CDECL_ENTRY("toupper", C_toupper), CDECL_ENTRY("tolower", C_tolower), CDECL_ENTRY("_toupper", C_toupper),
    CDECL_ENTRY("_tolower", C_tolower), CDECL_ENTRY("towupper", C_towupper), CDECL_ENTRY("towlower", C_towlower),
    CDECL_ENTRY("_isctype", C_isctype), CDECL_ENTRY("iswctype", C_iswctype), CDECL_ENTRY("is_wctype", C_iswctype),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace crt
} // namespace win32

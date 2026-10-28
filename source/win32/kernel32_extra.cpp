#include <cerrno>
// kernel32_extra.cpp — KERNEL32: функции, нужные программам на MFC
// (настоящей mfc42u.dll и Paint): процессы и потоки (без многопоточности),
// атомы, MulDiv, FormatMessage, SearchPath, GetStringTypeEx, форматы даты,
// времени и чисел, профили, Unicode-версии файловых функций.

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <vector>
#include <map>
#include <sys/stat.h>

#include "hle_util.h"
#include "../runtime/files.h"
#include "../runtime/heap.h"
#include "../runtime/process.h"
#include "../runtime/resources.h"

namespace win32 {
namespace {

using namespace hle;

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
inline uint32_t A(cpu::CpuState& c, cpu::IGuestMemory& m, int i) { return Arg(c, m, i); }

std::u16string Text(cpu::IGuestMemory& m, uint32_t p, bool unicode) {
    if (!p) return std::u16string();
    return unicode ? ReadWide(m, p) : AnsiToWide(ReadAnsi(m, p));
}
uint32_t Put(cpu::IGuestMemory& m, uint32_t p, const std::u16string& s, uint32_t cap, bool unicode) {
    if (unicode) return WriteWide(m, p, s, cap);
    std::string a;
    for (char16_t ch : s) a.push_back(ch < 256 ? static_cast<char>(ch) : '?');
    return WriteAnsi(m, p, a, cap);
}

// --- Процессы и потоки (поток у программы один) ---

FN(GetProcessVersion) {
    // версия подсистемы, под которую собрана программа (у XP-программ 4.0 или 5.1)
    (void)c; (void)m;
    return Ret(0x00050001);
}
FN(ResumeThread) { (void)c; (void)m; return Ret(0); }
FN(SuspendThread) { (void)c; (void)m; return Ret(0); }
FN(SetThreadPriority) { (void)c; (void)m; return Ret(1); }
FN(GetThreadPriority) { (void)c; (void)m; return Ret(0); }
FN(TerminateThread) { (void)c; (void)m; return Ret(1); }
FN(GetExitCodeThread) {
    if (A(c, m, 1)) m.Write32(A(c, m, 1), 259); // STILL_ACTIVE
    return Ret(1);
}
FN(TerminateProcess) {
    uint32_t h = A(c, m, 0);
    if (h != 0xFFFFFFFFu) { SetLastError(m, ERROR_ACCESS_DENIED); return Ret(0); }
    HostCallOutcome r = Ret(1);
    r.requestExit = true;
    r.exitCode = static_cast<int32_t>(A(c, m, 1));
    return r;
}

// --- Арифметика ---

FN(MulDiv) {
    int64_t a = static_cast<int32_t>(A(c, m, 0)), b = static_cast<int32_t>(A(c, m, 1)), d = static_cast<int32_t>(A(c, m, 2));
    if (!d) return Ret(0xFFFFFFFFu);
    int64_t p = a * b;
    // округление половины от нуля, как в Windows
    int64_t q = (p >= 0) == (d > 0) ? (p + (d > 0 ? d : -d) / 2 * (p >= 0 ? 1 : -1)) / d : (p - (d > 0 ? d : -d) / 2 * (p >= 0 ? 1 : -1)) / d;
    if (q > 0x7FFFFFFF || q < -0x7FFFFFFFll - 1) return Ret(0xFFFFFFFFu);
    return Ret(static_cast<uint32_t>(static_cast<int32_t>(q)));
}

// --- Атомы (глобальная и локальная таблица — одна) ---

struct AtomTable {
    std::map<std::u16string, uint16_t> byName;
    std::map<uint16_t, std::pair<std::u16string, int>> byAtom; // имя и счётчик ссылок
    uint16_t next = 0xC000;
};
AtomTable& Atoms() {
    static AtomTable t;
    return t;
}
std::u16string AtomKey(std::u16string s) {
    for (char16_t& ch : s) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    return s;
}
// Строка «#123» и MAKEINTATOM — целочисленные атомы
bool IntAtom(cpu::IGuestMemory& m, uint32_t p, bool unicode, uint16_t& out) {
    if (p < 0x10000) { out = static_cast<uint16_t>(p); return true; }
    std::u16string s = Text(m, p, unicode);
    if (s.size() < 2 || s[0] != '#') return false;
    uint32_t v = 0;
    for (size_t i = 1; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return false; v = v * 10 + (s[i] - '0'); }
    if (!v || v >= 0xC000) return false;
    out = static_cast<uint16_t>(v);
    return true;
}
HostCallOutcome AddAtomCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint16_t a;
    if (IntAtom(m, A(c, m, 0), unicode, a)) return Ret(a);
    std::u16string name = Text(m, A(c, m, 0), unicode), key = AtomKey(name);
    AtomTable& t = Atoms();
    auto it = t.byName.find(key);
    if (it != t.byName.end()) { t.byAtom[it->second].second++; return Ret(it->second); }
    a = t.next++;
    t.byName[key] = a;
    t.byAtom[a] = {name, 1};
    return Ret(a);
}
HostCallOutcome FindAtomCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint16_t a;
    if (IntAtom(m, A(c, m, 0), unicode, a)) return Ret(a);
    auto it = Atoms().byName.find(AtomKey(Text(m, A(c, m, 0), unicode)));
    return Ret(it == Atoms().byName.end() ? 0 : it->second);
}
FN(DeleteAtom) {
    uint16_t a = static_cast<uint16_t>(A(c, m, 0));
    AtomTable& t = Atoms();
    auto it = t.byAtom.find(a);
    if (it == t.byAtom.end()) return Ret(0);
    if (--it->second.second <= 0) { t.byName.erase(AtomKey(it->second.first)); t.byAtom.erase(it); }
    return Ret(0);
}
HostCallOutcome GetAtomNameCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint16_t a = static_cast<uint16_t>(A(c, m, 0));
    std::u16string name;
    if (a < 0xC000) { std::string s = "#" + std::to_string(a); name = AnsiToWide(s); }
    else {
        auto it = Atoms().byAtom.find(a);
        if (it == Atoms().byAtom.end()) return Ret(0);
        name = it->second.first;
    }
    return Ret(Put(m, A(c, m, 1), name, A(c, m, 2), unicode));
}
FN(GlobalAddAtomA) { return AddAtomCommon(c, m, false); }
FN(GlobalAddAtomW) { return AddAtomCommon(c, m, true); }
FN(GlobalFindAtomA) { return FindAtomCommon(c, m, false); }
FN(GlobalFindAtomW) { return FindAtomCommon(c, m, true); }
FN(GlobalGetAtomNameA) { return GetAtomNameCommon(c, m, false); }
FN(GlobalGetAtomNameW) { return GetAtomNameCommon(c, m, true); }

// Profile APIs live in profile_module.cpp (real checked INI provider).

// --- Файлы: Unicode-версии ---

FN(GetTempFileNameW) {
    static uint32_t counter = 1;
    std::u16string dir = ReadWide(m, A(c, m, 0)), pre = ReadWide(m, A(c, m, 1)).substr(0, 3);
    uint32_t unique = A(c, m, 2);
    if (!dir.empty() && dir.back() != '\\') dir += u"\\";
    for (;;) {
        uint32_t u = unique ? unique : counter++;
        char name[16];
        std::snprintf(name, sizeof(name), "%X.TMP", u & 0xFFFF);
        std::u16string full = dir + pre + AnsiToWide(name);
        std::string host = rt::FileSystem().ToHost(WideToUtf8(full),!unique);
        if(host.empty()){SetLastError(m,errno==EROFS?19:ERROR_PATH_NOT_FOUND);return Ret(0);}
        struct stat st;
        if (unique || stat(host.c_str(), &st) != 0) {
            if (!unique) { std::FILE* f = std::fopen(host.c_str(), "wb"); if (!f){SetLastError(m,ERROR_ACCESS_DENIED);return Ret(0);} std::fclose(f); }
            WriteWide(m, A(c, m, 3), full, 260);
            return Ret(u & 0xFFFF);
        }
    }
}
FN(LockFile) { (void)c; (void)m; return Ret(1); }
FN(UnlockFile) { (void)c; (void)m; return Ret(1); }
FN(GetFileSecurityW) { (void)c; SetLastError(m, 50); return Ret(0); } // ERROR_NOT_SUPPORTED
FN(DelayLoadFailureHook) { (void)c; (void)m; return Ret(0); }

// DWORD SearchPath(path, file, ext, cch, buf, filePart*)
HostCallOutcome SearchPathCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    std::u16string path = Text(m, A(c, m, 0), unicode), file = Text(m, A(c, m, 1), unicode), ext = Text(m, A(c, m, 2), unicode);
    if (!ext.empty() && file.find(u'.') == std::u16string::npos) file += ext;
    std::vector<std::u16string> dirs;
    if (!path.empty()) {
        size_t s = 0;
        while (s <= path.size()) {
            size_t e = path.find(u';', s);
            if (e == std::u16string::npos) e = path.size();
            if (e > s) dirs.push_back(path.substr(s, e - s));
            s = e + 1;
        }
    } else {
        dirs.push_back(u"C:\\");
        dirs.push_back(Utf8PathToWide(rt::FileSystem().CurrentDirectory()));
    }
    bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '\\');
    if (absolute) dirs.assign(1, u"");
    for (const std::u16string& d : dirs) {
        std::u16string cand = d.empty() ? file : (d.back() == '\\' ? d + file : d + u"\\" + file);
        std::string full = rt::FileSystem().FullGuestPath(WideToUtf8(cand));
        struct stat st;
        if (stat(rt::FileSystem().ToHost(full).c_str(), &st) != 0) continue;
        std::u16string res = Utf8PathToWide(full);
        uint32_t cap = A(c, m, 3), buf = A(c, m, 4);
        if (cap <= res.size()) return Ret(static_cast<uint32_t>(res.size() + 1));
        uint32_t n = Put(m, buf, res, cap, unicode);
        if (A(c, m, 5)) {
            size_t slash = res.find_last_of(u'\\');
            m.Write32(A(c, m, 5), buf + static_cast<uint32_t>((slash + 1) * (unicode ? 2 : 1)));
        }
        return Ret(n);
    }
    SetLastError(m, ERROR_FILE_NOT_FOUND);
    return Ret(0);
}
FN(SearchPathA) { return SearchPathCommon(c, m, false); }
FN(SearchPathW) { return SearchPathCommon(c, m, true); }

// --- Типы символов ---

uint16_t CharType1(char16_t ch) {
    uint16_t t = 0;
    bool upper = (ch >= 'A' && ch <= 'Z') || (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7) || (ch >= 0x410 && ch <= 0x42F) || ch == 0x401;
    bool lower = (ch >= 'a' && ch <= 'z') || (ch >= 0xDF && ch <= 0xFF && ch != 0xF7) || (ch >= 0x430 && ch <= 0x44F) || ch == 0x451;
    if (upper) t |= 0x001 | 0x100;
    if (lower) t |= 0x002 | 0x100;
    if (ch >= '0' && ch <= '9') t |= 0x004;
    if (ch == ' ' || (ch >= 9 && ch <= 13) || ch == 0xA0) t |= 0x008;
    if (ch == ' ' || ch == '\t') t |= 0x040;
    if (ch < 32 || ch == 127) t |= 0x020;
    if (((ch > 32 && ch < 127) || (ch >= 0xA1 && ch <= 0xBF)) && !(t & 0x107)) t |= 0x010;
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) t |= 0x080;
    if (ch) t |= 0x200; // C1_DEFINED
    return t;
}
// GetStringTypeEx(locale, type, src, n, out)
HostCallOutcome StringTypeExCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t type = A(c, m, 1), s = A(c, m, 2), out = A(c, m, 4);
    int32_t n = static_cast<int32_t>(A(c, m, 3));
    if (n < 0) n = static_cast<int32_t>(Text(m, s, unicode).size()) + 1;
    for (int32_t i = 0; i < n; ++i) {
        char16_t ch = unicode ? static_cast<char16_t>(m.Read16(s + i * 2)) : static_cast<char16_t>(m.Read8(s + i));
        uint16_t t = 0;
        if (type == 1) t = CharType1(ch);
        else if (type == 2) t = (ch < 128 && ch >= '0' && ch <= '9') ? 3 : ch ? 1 : 0; // C2_LEFTTORIGHT / EUROPENUMBER
        else if (type == 4) t = (CharType1(ch) & 0x100) ? 0x8000 : 0; // C3_ALPHA
        m.Write16(out + i * 2, t);
    }
    return Ret(1);
}
FN(GetStringTypeExA) { return StringTypeExCommon(c, m, false); }
FN(GetStringTypeExW) { return StringTypeExCommon(c, m, true); }

// --- Дата, время, числа (en-US) ---

struct SysTime { int year, month, dow, day, hour, minute, second; };
SysTime ReadSysTime(cpu::IGuestMemory& m, uint32_t p) {
    SysTime t{};
    if (p) {
        t.year = m.Read16(p); t.month = m.Read16(p + 2); t.dow = m.Read16(p + 4); t.day = m.Read16(p + 6);
        t.hour = m.Read16(p + 8); t.minute = m.Read16(p + 10); t.second = m.Read16(p + 12);
    } else {
        std::time_t now = std::time(nullptr);
        std::tm tm = *std::localtime(&now);
        t = SysTime{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_wday, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
    }
    return t;
}
std::u16string FormatPicture(const std::u16string& pic, const SysTime& t) {
    static const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* const kMonths[] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                          "September", "October", "November", "December"};
    std::string out;
    for (size_t i = 0; i < pic.size();) {
        char16_t ch = pic[i];
        if (ch == '\'') {
            size_t e = pic.find(u'\'', i + 1);
            if (e == std::u16string::npos) e = pic.size();
            out += WideToUtf8(pic.substr(i + 1, e - i - 1));
            i = e + 1;
            continue;
        }
        size_t n = 1;
        while (i + n < pic.size() && pic[i + n] == ch) ++n;
        char buf[32];
        auto num = [&](int v, size_t width) { std::snprintf(buf, sizeof(buf), width >= 2 ? "%02d" : "%d", v); out += buf; };
        switch (ch) {
        case 'd':
            if (n <= 2) num(t.day, n);
            else out += n == 3 ? std::string(kDays[t.dow % 7]).substr(0, 3) : kDays[t.dow % 7];
            break;
        case 'M':
            if (n <= 2) num(t.month, n);
            else if (t.month >= 1 && t.month <= 12) out += n == 3 ? std::string(kMonths[t.month - 1]).substr(0, 3) : kMonths[t.month - 1];
            break;
        case 'y':
            if (n <= 2) num(t.year % 100, 2);
            else { std::snprintf(buf, sizeof(buf), "%d", t.year); out += buf; }
            break;
        case 'h': num(t.hour % 12 ? t.hour % 12 : 12, n); break;
        case 'H': num(t.hour, n); break;
        case 'm': num(t.minute, n); break;
        case 's': num(t.second, n); break;
        case 't': out += n == 1 ? (t.hour < 12 ? "A" : "P") : (t.hour < 12 ? "AM" : "PM"); break;
        case 'g': break;
        default: out += WideToUtf8(pic.substr(i, n)); break;
        }
        i += n;
    }
    return AnsiToWide(out);
}
// GetDateFormat/GetTimeFormat(locale, flags, SYSTEMTIME*, format, buf, cch)
HostCallOutcome DateTimeCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode, bool date) {
    uint32_t flags = A(c, m, 1);
    std::u16string pic = Text(m, A(c, m, 3), unicode);
    if (pic.empty()) pic = date ? ((flags & 2) ? u"dddd, MMMM dd, yyyy" : u"M/d/yyyy") : ((flags & 2) ? u"h:mm tt" : u"h:mm:ss tt");
    std::u16string s = FormatPicture(pic, ReadSysTime(m, A(c, m, 2)));
    uint32_t cap = A(c, m, 5);
    if (!cap) return Ret(static_cast<uint32_t>(s.size() + 1));
    if (cap <= s.size()) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(0); }
    return Ret(Put(m, A(c, m, 4), s, cap, unicode) + 1);
}
FN(GetDateFormatA) { return DateTimeCommon(c, m, false, true); }
FN(GetDateFormatW) { return DateTimeCommon(c, m, true, true); }
FN(GetTimeFormatA) { return DateTimeCommon(c, m, false, false); }
FN(GetTimeFormatW) { return DateTimeCommon(c, m, true, false); }
// GetNumberFormat(locale, flags, value, fmt, buf, cch): разделитель тысяч «,», дробная часть 2 знака
HostCallOutcome NumberFormatCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    std::string v = WideToUtf8(Text(m, A(c, m, 2), unicode));
    bool neg = !v.empty() && v[0] == '-';
    if (neg) v = v.substr(1);
    size_t dot = v.find('.');
    std::string ip = v.substr(0, dot), fp = dot == std::string::npos ? "" : v.substr(dot + 1);
    fp.resize(2, '0');
    std::string grouped;
    for (size_t i = 0; i < ip.size(); ++i) {
        if (i && (ip.size() - i) % 3 == 0) grouped += ',';
        grouped += ip[i];
    }
    std::u16string s = AnsiToWide((neg ? "-" : "") + (grouped.empty() ? "0" : grouped) + "." + fp);
    uint32_t cap = A(c, m, 5);
    if (!cap) return Ret(static_cast<uint32_t>(s.size() + 1));
    if (cap <= s.size()) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(0); }
    return Ret(Put(m, A(c, m, 4), s, cap, unicode) + 1);
}
FN(GetNumberFormatA) { return NumberFormatCommon(c, m, false); }
FN(GetNumberFormatW) { return NumberFormatCommon(c, m, true); }

// --- FormatMessage ---

std::u16string SystemMessage(uint32_t id) {
    switch (id) {
    case 0: return u"The operation completed successfully.\r\n";
    case 2: return u"The system cannot find the file specified.\r\n";
    case 3: return u"The system cannot find the path specified.\r\n";
    case 5: return u"Access is denied.\r\n";
    case 6: return u"The handle is invalid.\r\n";
    case 8: return u"Not enough storage is available to process this command.\r\n";
    case 32: return u"The process cannot access the file because it is being used by another process.\r\n";
    case 80: return u"The file exists.\r\n";
    case 87: return u"The parameter is incorrect.\r\n";
    case 112: return u"There is not enough space on the disk.\r\n";
    case 122: return u"The data area passed to a system call is too small.\r\n";
    case 183: return u"Cannot create a file when that file already exists.\r\n";
    }
    return u"";
}

// Сообщение из таблицы RT_MESSAGETABLE модуля
bool ModuleMessage(rt::Module* mod, uint32_t id, std::u16string& out) {
    rt::ResourceData d;
    if (!rt::FindResource(mod, rt::ResId::Num(11), rt::ResId::Num(1), d)) return false;
    const uint8_t* p = rt::ResourcePtr(mod, d);
    if (!p || d.size < 4) return false;
    auto r32 = [&](uint32_t o) { return o + 4 <= d.size ? static_cast<uint32_t>(p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) : 0u; };
    uint32_t blocks = r32(0);
    for (uint32_t b = 0; b < blocks; ++b) {
        uint32_t lo = r32(4 + b * 12), hi = r32(8 + b * 12), off = r32(12 + b * 12);
        if (id < lo || id > hi) continue;
        for (uint32_t k = lo; k <= hi && off + 4 <= d.size; ++k) {
            uint16_t len = static_cast<uint16_t>(p[off] | (p[off + 1] << 8)), fl = static_cast<uint16_t>(p[off + 2] | (p[off + 3] << 8));
            if (k == id) {
                out.clear();
                if (fl & 1) for (uint32_t i = off + 4; i + 1 < off + len && (p[i] | p[i + 1]); i += 2) out.push_back(static_cast<char16_t>(p[i] | (p[i + 1] << 8)));
                else for (uint32_t i = off + 4; i < off + len && p[i]; ++i) out.push_back(p[i]);
                return true;
            }
            off += len;
        }
    }
    return false;
}

HostCallOutcome FormatMessageCommon(cpu::CpuState& c, cpu::IGuestMemory& m, bool unicode) {
    uint32_t flags = A(c, m, 0), source = A(c, m, 1), id = A(c, m, 2), buf = A(c, m, 4), size = A(c, m, 5), args = A(c, m, 6);
    std::u16string tmpl;
    if (flags & 0x400) tmpl = Text(m, source, unicode);                            // FROM_STRING
    else {
        bool found = false;
        if (flags & 0x800) {                                                       // FROM_HMODULE
            rt::Module* mod = rt::g_process ? (source ? rt::g_process->FindByHandle(source) : rt::g_process->MainModule()) : nullptr;
            found = ModuleMessage(mod, id, tmpl);
        }
        if (!found && (flags & 0x1000)) { tmpl = SystemMessage(id); found = !tmpl.empty(); } // FROM_SYSTEM
        if (!found) { SetLastError(m, 317); return Ret(0); }                       // ERROR_MR_MID_NOT_FOUND
    }
    // подстановки %1..%99 (без !формат! — как строка или число)
    std::u16string out;
    bool ignore = (flags & 0x200) != 0, array = (flags & 0x2000) != 0;
    uint32_t vaBase = (args && !array) ? m.Read32(args) : args;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        char16_t ch = tmpl[i];
        if (ch != '%' || i + 1 >= tmpl.size()) { out.push_back(ch); continue; }
        char16_t n = tmpl[++i];
        if (n == '0') break;
        if (n == 'n') { out += u"\r\n"; continue; }
        if (n < '1' || n > '9') { out.push_back(n); continue; }
        int idx = n - '0';
        if (i + 1 < tmpl.size() && tmpl[i + 1] >= '0' && tmpl[i + 1] <= '9') idx = idx * 10 + (tmpl[++i] - '0');
        std::u16string fmt = u"s";
        if (i + 1 < tmpl.size() && tmpl[i + 1] == '!') {
            size_t e = tmpl.find(u'!', i + 2);
            if (e != std::u16string::npos) { fmt = tmpl.substr(i + 2, e - i - 2); i = e; }
        }
        if (ignore || !vaBase) { out += u"%" + AnsiToWide(std::to_string(idx)); continue; }
        uint32_t v = m.Read32(vaBase + static_cast<uint32_t>(idx - 1) * 4);
        char16_t conv = fmt.empty() ? 's' : fmt.back();
        if (conv == 's' || conv == 'S') out += (conv == 'S') != unicode ? Text(m, v, unicode) : Text(m, v, unicode);
        else if (conv == 'x' || conv == 'X') { char b[16]; std::snprintf(b, sizeof(b), conv == 'x' ? "%x" : "%X", v); out += AnsiToWide(b); }
        else if (conv == 'u') out += AnsiToWide(std::to_string(v));
        else out += AnsiToWide(std::to_string(static_cast<int32_t>(v)));
    }
    uint32_t chars = static_cast<uint32_t>(out.size());
    if (flags & 0x100) { // ALLOCATE_BUFFER: буфер через LocalAlloc
        uint32_t bytes = (chars + 1) * (unicode ? 2 : 1);
        uint32_t mem = rt::ProcessHeap().Alloc(std::max(bytes, size * (unicode ? 2u : 1u)), true);
        if (!mem) return Ret(0);
        Put(m, mem, out, chars + 1, unicode);
        m.Write32(buf, mem);
        return Ret(chars);
    }
    if (size <= chars) { SetLastError(m, ERROR_INSUFFICIENT_BUFFER); return Ret(0); }
    return Ret(Put(m, buf, out, size, unicode));
}
FN(FormatMessageA) { return FormatMessageCommon(c, m, false); }
FN(FormatMessageW) { return FormatMessageCommon(c, m, true); }

#define E(name, bytes) {"KERNEL32.dll", #name, bytes, CallConv::Stdcall, &name}
const HostFunctionEntry kEntries[] = {
    E(GetProcessVersion, 4), E(ResumeThread, 4), E(SuspendThread, 4), E(SetThreadPriority, 8), E(GetThreadPriority, 4),
    E(TerminateThread, 8), E(GetExitCodeThread, 8), E(TerminateProcess, 8), E(MulDiv, 12),
    E(GlobalAddAtomA, 4), E(GlobalAddAtomW, 4), E(GlobalFindAtomA, 4), E(GlobalFindAtomW, 4),
    E(GlobalGetAtomNameA, 12), E(GlobalGetAtomNameW, 12),
    {"KERNEL32.dll", "GlobalDeleteAtom", 4, CallConv::Stdcall, &DeleteAtom},
    {"KERNEL32.dll", "AddAtomA", 4, CallConv::Stdcall, &GlobalAddAtomA}, {"KERNEL32.dll", "AddAtomW", 4, CallConv::Stdcall, &GlobalAddAtomW},
    {"KERNEL32.dll", "FindAtomA", 4, CallConv::Stdcall, &GlobalFindAtomA}, {"KERNEL32.dll", "FindAtomW", 4, CallConv::Stdcall, &GlobalFindAtomW},
    {"KERNEL32.dll", "GetAtomNameA", 12, CallConv::Stdcall, &GlobalGetAtomNameA},
    {"KERNEL32.dll", "GetAtomNameW", 12, CallConv::Stdcall, &GlobalGetAtomNameW},
    {"KERNEL32.dll", "DeleteAtom", 4, CallConv::Stdcall, &DeleteAtom},
    E(GetTempFileNameW, 16), E(LockFile, 20), E(UnlockFile, 20),
    E(GetFileSecurityW, 20), {"KERNEL32.dll", "SetFileSecurityW", 12, CallConv::Stdcall, &GetFileSecurityW},
    {"ADVAPI32.dll", "GetFileSecurityW", 20, CallConv::Stdcall, &GetFileSecurityW},
    {"ADVAPI32.dll", "SetFileSecurityW", 12, CallConv::Stdcall, &GetFileSecurityW},
    E(DelayLoadFailureHook, 8), E(SearchPathA, 24), E(SearchPathW, 24), E(GetStringTypeExA, 20), E(GetStringTypeExW, 20),
    E(GetDateFormatA, 24), E(GetDateFormatW, 24), E(GetTimeFormatA, 24), E(GetTimeFormatW, 24),
    E(GetNumberFormatA, 24), E(GetNumberFormatW, 24), E(FormatMessageA, 28), E(FormatMessageW, 28),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32

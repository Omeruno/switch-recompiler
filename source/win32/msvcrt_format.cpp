// msvcrt_format.cpp — printf/scanf msvcrt.dll над памятью гостя.
//
// Особенности msvcrt (а не C99): порядок в %e/%g — минимум 3 цифры
// ("1.5e+003"), бесконечность и NaN — "1.#INF00", "-1.#IND00",
// "1.#QNAN0"; длины I64/I32/I; %S — строка «другой ширины»; %p — 8
// шестнадцатеричных цифр в верхнем регистре.

#include "msvcrt_format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace win32 {
namespace crt {

namespace {

std::string Narrow(const std::u16string& w) {
    std::string s;
    s.reserve(w.size());
    for (char16_t ch : w) s.push_back(ch < 256 ? static_cast<char>(ch) : '?');
    return s;
}
std::u16string Widen(const std::string& s) {
    std::u16string w;
    w.reserve(s.size());
    for (unsigned char ch : s) w.push_back(ch);
    return w;
}

struct Spec {
    bool left = false, plus = false, space = false, alt = false, zero = false;
    int width = -1, precision = -1;
    int len = 0; // 0 int, 1 h (short), 2 l (long), 3 I64/ll, 4 L (long double = double), 5 hh
    bool lenWide = false, lenNarrow = false; // l / h для строк и символов
    char conv = 0;
};

void Pad(std::u16string& out, const std::u16string& body, const Spec& s, bool numeric = false) {
    int w = s.width;
    if (w < 0 || static_cast<int>(body.size()) >= w) { out += body; return; }
    size_t pad = static_cast<size_t>(w) - body.size();
    if (s.left) { out += body; out.append(pad, u' '); return; }
    if (s.zero && numeric) {
        // нули — после знака/префикса
        size_t pre = 0;
        if (!body.empty() && (body[0] == u'-' || body[0] == u'+' || body[0] == u' ')) pre = 1;
        if (body.size() >= pre + 2 && body[pre] == u'0' && (body[pre + 1] == u'x' || body[pre + 1] == u'X')) pre += 2;
        out += body.substr(0, pre);
        out.append(pad, u'0');
        out += body.substr(pre);
        return;
    }
    out.append(pad, u' ');
    out += body;
}

std::string FixExponent(std::string s) {
    // "1.5e+03" -> "1.5e+003"
    size_t e = s.find_first_of("eE");
    if (e == std::string::npos || e + 1 >= s.size()) return s;
    size_t d = e + 1;
    if (s[d] == '+' || s[d] == '-') ++d;
    size_t digits = s.size() - d;
    if (digits < 3) s.insert(d, 3 - digits, '0');
    return s;
}

std::string FormatFloat(double v, const Spec& s) {
    char conv = s.conv;
    int prec = s.precision < 0 ? 6 : s.precision;
    if (std::isinf(v) || std::isnan(v)) {
        bool neg = std::signbit(v);
        const char* body;
        if (std::isinf(v)) body = "1.#INF";
        else {
            uint64_t b;
            std::memcpy(&b, &v, 8);
            bool indefinite = neg && (b & 0x000FFFFFFFFFFFFFull) == 0x0008000000000000ull;
            body = indefinite ? "1.#IND" : (b & 0x0008000000000000ull) ? "1.#QNAN" : "1.#SNAN";
        }
        std::string r = neg ? "-" : (s.plus ? "+" : (s.space ? " " : ""));
        r += body;
        if (conv == 'g' || conv == 'G') return r;
        // дополняем нулями до точности (как msvcrt)
        size_t frac = r.size() - r.find('.') - 1;
        while (static_cast<int>(frac) < prec) { r.push_back('0'); ++frac; }
        if (conv == 'e' || conv == 'E') r += conv == 'e' ? "e+000" : "E+000";
        return r;
    }
    char fmt[32];
    char* f = fmt;
    *f++ = '%';
    if (s.plus) *f++ = '+';
    if (s.space) *f++ = ' ';
    if (s.alt) *f++ = '#';
    *f++ = '.';
    f += std::snprintf(f, 8, "%d", prec);
    *f++ = conv == 'a' || conv == 'A' ? conv : conv;
    *f = 0;
    char buf[512];
    std::snprintf(buf, sizeof(buf), fmt, v);
    std::string r = buf;
    if (conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G') r = FixExponent(r);
    return r;
}

template <typename Ch>
struct FmtReader {
    cpu::IGuestMemory& m;
    uint32_t p;
    Ch Peek() const { return sizeof(Ch) == 1 ? static_cast<Ch>(m.Read8(p)) : static_cast<Ch>(m.Read16(p)); }
    Ch Get() { Ch c = Peek(); p += sizeof(Ch); return c; }
};

template <typename Ch>
std::u16string Format(cpu::IGuestMemory& m, uint32_t fmtAddr, GuestVaList& args) {
    const bool wide = sizeof(Ch) == 2;
    std::u16string out;
    FmtReader<Ch> r{m, fmtAddr};
    for (int guard = 0; guard < 1 << 20; ++guard) {
        uint32_t ch = static_cast<uint32_t>(r.Get()) & (wide ? 0xFFFFu : 0xFFu);
        if (!ch) break;
        if (ch != '%') { out.push_back(static_cast<char16_t>(ch)); continue; }
        Spec s;
        // флаги
        for (;;) {
            uint32_t c = r.Peek();
            if (c == '-') s.left = true;
            else if (c == '+') s.plus = true;
            else if (c == ' ') s.space = true;
            else if (c == '#') s.alt = true;
            else if (c == '0') s.zero = true;
            else break;
            r.Get();
        }
        // ширина
        if (r.Peek() == '*') { r.Get(); int w = static_cast<int32_t>(args.Next32()); if (w < 0) { s.left = true; w = -w; } s.width = w; }
        else { while (r.Peek() >= '0' && r.Peek() <= '9') { if (s.width < 0) s.width = 0; s.width = s.width * 10 + (r.Get() - '0'); } }
        // точность
        if (r.Peek() == '.') {
            r.Get();
            s.precision = 0;
            if (r.Peek() == '*') { r.Get(); int p = static_cast<int32_t>(args.Next32()); s.precision = p < 0 ? -1 : p; }
            else while (r.Peek() >= '0' && r.Peek() <= '9') s.precision = s.precision * 10 + (r.Get() - '0');
        }
        // длина
        for (;;) {
            uint32_t c = r.Peek();
            if (c == 'h') { r.Get(); if (r.Peek() == 'h') { r.Get(); s.len = 5; } else s.len = 1; s.lenNarrow = true; }
            else if (c == 'l') { r.Get(); if (r.Peek() == 'l') { r.Get(); s.len = 3; } else s.len = 2; s.lenWide = true; }
            else if (c == 'w') { r.Get(); s.lenWide = true; }
            else if (c == 'L') { r.Get(); s.len = 4; }
            else if (c == 'I') {
                r.Get();
                if (r.Peek() == '6') { r.Get(); r.Get(); s.len = 3; }
                else if (r.Peek() == '3') { r.Get(); r.Get(); s.len = 0; }
                else s.len = 0; // I: размер указателя (32 бита)
            }
            else if (c == 'z' || c == 't' || c == 'j') { r.Get(); s.len = c == 'j' ? 3 : 0; }
            else break;
        }
        uint32_t conv = r.Get();
        if (!conv) break;
        s.conv = static_cast<char>(conv);
        switch (conv) {
            case '%': out.push_back(u'%'); break;
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
                bool sgn = conv == 'd' || conv == 'i';
                uint64_t uv;
                bool neg = false;
                if (s.len == 3) {
                    uv = args.Next64();
                    if (sgn && static_cast<int64_t>(uv) < 0) { neg = true; uv = static_cast<uint64_t>(-static_cast<int64_t>(uv)); }
                } else {
                    uint32_t v32 = args.Next32();
                    if (s.len == 1) v32 = sgn ? static_cast<uint32_t>(static_cast<int16_t>(v32)) : (v32 & 0xFFFF);
                    if (s.len == 5) v32 = sgn ? static_cast<uint32_t>(static_cast<int8_t>(v32)) : (v32 & 0xFF);
                    if (sgn && static_cast<int32_t>(v32) < 0) { neg = true; uv = static_cast<uint64_t>(-static_cast<int64_t>(static_cast<int32_t>(v32))); }
                    else uv = v32;
                }
                std::string digits;
                unsigned base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X') ? 16 : 10;
                const char* dig = conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                if (uv == 0) digits = s.precision == 0 ? "" : "0";
                while (uv) { digits.insert(digits.begin(), dig[uv % base]); uv /= base; }
                if (s.precision >= 0 && static_cast<int>(digits.size()) < s.precision) digits.insert(0, s.precision - digits.size(), '0');
                std::string prefix;
                if (neg) prefix = "-";
                else if (sgn && s.plus) prefix = "+";
                else if (sgn && s.space) prefix = " ";
                if (s.alt && !digits.empty() && digits != "0") {
                    if (conv == 'x') prefix += "0x";
                    if (conv == 'X') prefix += "0X";
                    if (conv == 'o' && digits[0] != '0') prefix += "0";
                }
                Spec ps = s;
                if (s.precision >= 0) ps.zero = false;
                Pad(out, Widen(prefix + digits), ps, true);
                break;
            }
            case 'p': {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%08X", args.Next32());
                Pad(out, Widen(buf), s);
                break;
            }
            case 'c': case 'C': {
                bool w = s.lenWide ? true : s.lenNarrow ? false : (conv == 'C' ? !wide : wide);
                uint32_t v = args.Next32();
                std::u16string b(1, static_cast<char16_t>(w ? (v & 0xFFFF) : (v & 0xFF)));
                Pad(out, b, s);
                break;
            }
            case 's': case 'S': {
                // %s в printf — узкая строка, в wprintf — широкая; %S — наоборот; h/l — явно
                bool w = s.lenWide ? true : s.lenNarrow ? false : (conv == 'S' ? !wide : wide);
                uint32_t p = args.Next32();
                std::u16string str;
                if (!p) str = u"(null)";
                else {
                    size_t lim = s.precision >= 0 ? static_cast<size_t>(s.precision) : 1u << 20;
                    for (size_t i = 0; i < lim; ++i) {
                        uint32_t c = w ? m.Read16(p + static_cast<uint32_t>(i * 2)) : m.Read8(p + static_cast<uint32_t>(i));
                        if (!c) break;
                        str.push_back(static_cast<char16_t>(c));
                    }
                }
                Pad(out, str, s);
                break;
            }
            case 'n': {
                uint32_t p = args.Next32();
                if (p) {
                    if (s.len == 1) m.Write16(p, static_cast<uint16_t>(out.size()));
                    else m.Write32(p, static_cast<uint32_t>(out.size()));
                }
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
                double v = args.NextDouble();
                Spec fs = s;
                if (conv == 'F') fs.conv = 'f';
                std::string body = FormatFloat(v, fs);
                Pad(out, Widen(body), s, std::isfinite(v));
                break;
            }
            default:
                out.push_back(u'%');
                out.push_back(static_cast<char16_t>(conv));
                break;
        }
    }
    return out;
}

// --- scanf ---
template <typename Ch>
int Scan(cpu::IGuestMemory& m, const std::u16string& in, uint32_t fmtAddr, GuestVaList& args, size_t* consumed) {
    const bool wide = sizeof(Ch) == 2;
    FmtReader<Ch> r{m, fmtAddr};
    size_t pos = 0;
    int assigned = 0;
    bool anyInput = false;
    auto isSpace = [](uint32_t c) { return c == ' ' || (c >= 9 && c <= 13); };
    for (int guard = 0; guard < 1 << 16; ++guard) {
        uint32_t f = static_cast<uint32_t>(r.Get()) & (wide ? 0xFFFFu : 0xFFu);
        if (!f) break;
        if (isSpace(f)) { while (pos < in.size() && isSpace(in[pos])) ++pos; continue; }
        if (f != '%') {
            if (pos < in.size() && in[pos] == f) { ++pos; continue; }
            break;
        }
        bool suppress = false;
        int width = -1, len = 0;
        bool lenWide = false, lenNarrow = false;
        if (r.Peek() == '*') { r.Get(); suppress = true; }
        while (r.Peek() >= '0' && r.Peek() <= '9') { if (width < 0) width = 0; width = width * 10 + (r.Get() - '0'); }
        for (;;) {
            uint32_t c = r.Peek();
            if (c == 'h') { r.Get(); len = 1; lenNarrow = true; if (r.Peek() == 'h') { r.Get(); len = 5; } }
            else if (c == 'l') { r.Get(); len = 2; lenWide = true; if (r.Peek() == 'l') { r.Get(); len = 3; } }
            else if (c == 'L') { r.Get(); len = 4; }
            else if (c == 'I') { r.Get(); if (r.Peek() == '6') { r.Get(); r.Get(); len = 3; } else if (r.Peek() == '3') { r.Get(); r.Get(); } }
            else break;
        }
        uint32_t conv = r.Get();
        if (!conv) break;
        if (conv == '%') { while (pos < in.size() && isSpace(in[pos])) ++pos; if (pos < in.size() && in[pos] == '%') { ++pos; continue; } break; }
        if (conv == 'n') { if (!suppress) { uint32_t p = args.Next32(); m.Write32(p, static_cast<uint32_t>(pos)); } continue; }
        if (conv != 'c' && conv != '[' && conv != 'C') while (pos < in.size() && isSpace(in[pos])) ++pos;
        if (pos >= in.size()) { if (!anyInput) assigned = -1; break; }
        size_t lim = width < 0 ? in.size() - pos : static_cast<size_t>(width);
        auto takeNumber = [&](bool isFloat, int base) {
            std::string tok;
            size_t i = pos, end = std::min(in.size(), pos + lim);
            if (i < end && (in[i] == '+' || in[i] == '-')) tok.push_back(static_cast<char>(in[i++]));
            if (isFloat) {
                while (i < end && ((in[i] >= '0' && in[i] <= '9') || in[i] == '.' ||
                                   ((in[i] == 'e' || in[i] == 'E') && !tok.empty()) ||
                                   ((in[i] == '+' || in[i] == '-') && !tok.empty() && (tok.back() == 'e' || tok.back() == 'E')))) tok.push_back(static_cast<char>(in[i++]));
            } else {
                if (base == 16 || base == 0) {
                    if (i + 1 < end && in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) { tok += "0x"; i += 2; if (base == 0) base = 16; }
                }
                if (base == 0) base = (i < end && in[i] == '0') ? 8 : 10;
                while (i < end) {
                    uint32_t c = in[i];
                    int d = (c >= '0' && c <= '9') ? static_cast<int>(c - '0') : (c >= 'a' && c <= 'f') ? static_cast<int>(c - 'a' + 10) : (c >= 'A' && c <= 'F') ? static_cast<int>(c - 'A' + 10) : 99;
                    if (d >= base) break;
                    tok.push_back(static_cast<char>(c));
                    ++i;
                }
            }
            pos = i;
            return std::make_pair(tok, base);
        };
        switch (conv) {
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
                int base = conv == 'd' || conv == 'u' ? 10 : conv == 'i' ? 0 : conv == 'o' ? 8 : 16;
                auto t = takeNumber(false, base);
                if (t.first.empty() || t.first == "-" || t.first == "+") goto done;
                anyInput = true;
                long long v = std::strtoll(t.first.c_str(), nullptr, t.second);
                if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') v = static_cast<long long>(std::strtoull(t.first.c_str(), nullptr, t.second));
                if (!suppress) {
                    uint32_t p = args.Next32();
                    if (len == 1) m.Write16(p, static_cast<uint16_t>(v));
                    else if (len == 5) m.Write8(p, static_cast<uint8_t>(v));
                    else if (len == 3) { m.Write32(p, static_cast<uint32_t>(v)); m.Write32(p + 4, static_cast<uint32_t>(static_cast<uint64_t>(v) >> 32)); }
                    else m.Write32(p, static_cast<uint32_t>(v));
                    ++assigned;
                }
                break;
            }
            case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': {
                auto t = takeNumber(true, 10);
                if (t.first.empty()) goto done;
                anyInput = true;
                double v = std::strtod(t.first.c_str(), nullptr);
                if (!suppress) {
                    uint32_t p = args.Next32();
                    if (len == 2 || len == 4) { uint64_t b; std::memcpy(&b, &v, 8); m.Write32(p, static_cast<uint32_t>(b)); m.Write32(p + 4, static_cast<uint32_t>(b >> 32)); }
                    else { float fv = static_cast<float>(v); uint32_t b; std::memcpy(&b, &fv, 4); m.Write32(p, b); }
                    ++assigned;
                }
                break;
            }
            case 's': case 'S': case 'c': case 'C': case '[': {
                bool w = lenWide ? true : lenNarrow ? false : ((conv == 'S' || conv == 'C') ? !wide : wide);
                std::u16string set;
                bool negate = false;
                if (conv == '[') {
                    if (r.Peek() == '^') { r.Get(); negate = true; }
                    if (r.Peek() == ']') set.push_back(static_cast<char16_t>(r.Get()));
                    while (r.Peek() && r.Peek() != ']') set.push_back(static_cast<char16_t>(r.Get()));
                    if (r.Peek() == ']') r.Get();
                }
                size_t n = conv == 'c' || conv == 'C' ? (width < 0 ? 1 : static_cast<size_t>(width)) : lim;
                uint32_t p = suppress ? 0 : args.Next32();
                size_t i = 0;
                while (i < n && pos < in.size()) {
                    char16_t ch = in[pos];
                    if (conv == 's' || conv == 'S') { if (isSpace(ch)) break; }
                    if (conv == '[') {
                        bool inSet = false;
                        for (size_t k = 0; k < set.size(); ++k) {
                            if (k + 2 < set.size() && set[k + 1] == '-') { if (ch >= set[k] && ch <= set[k + 2]) inSet = true; k += 2; }
                            else if (set[k] == ch) inSet = true;
                        }
                        if (inSet == negate) break;
                    }
                    if (p) { if (w) m.Write16(p + static_cast<uint32_t>(i * 2), ch); else m.Write8(p + static_cast<uint32_t>(i), static_cast<uint8_t>(ch)); }
                    ++i;
                    ++pos;
                }
                if (i == 0) goto done;
                anyInput = true;
                if (p && conv != 'c' && conv != 'C') { if (w) m.Write16(p + static_cast<uint32_t>(i * 2), 0); else m.Write8(p + static_cast<uint32_t>(i), 0); }
                if (p) ++assigned;
                break;
            }
            default:
                goto done;
        }
    }
done:
    if (consumed) *consumed = pos;
    return assigned;
}

} // namespace

std::string FormatA(cpu::IGuestMemory& m, uint32_t fmt, GuestVaList& args) { return Narrow(Format<uint8_t>(m, fmt, args)); }
std::u16string FormatW(cpu::IGuestMemory& m, uint32_t fmt, GuestVaList& args) { return Format<uint16_t>(m, fmt, args); }
int ScanA(cpu::IGuestMemory& m, const std::string& src, uint32_t fmt, GuestVaList& args, size_t* consumed) {
    return Scan<uint8_t>(m, Widen(src), fmt, args, consumed);
}
int ScanW(cpu::IGuestMemory& m, const std::u16string& src, uint32_t fmt, GuestVaList& args, size_t* consumed) {
    return Scan<uint16_t>(m, src, fmt, args, consumed);
}

} // namespace crt
} // namespace win32

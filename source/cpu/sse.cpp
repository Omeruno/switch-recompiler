// sse.cpp — MMX, SSE, SSE2 (двухбайтовая карта 0F).
//
// Префикс 66/F3/F2 выбирает вариант инструкции: без префикса — MMX или
// «ps», 66 — SSE2 над XMM или «pd», F3 — «ss», F2 — «sd».
// Вещественная семантика (NaN, преобразования, флаги MXCSR) — fp_util.h.

#include <algorithm>
#include <cmath>

#include "exec_context.h"
#include "fp_util.h"

namespace cpu {
namespace detail {

namespace {

constexpr uint32_t MX_IE = 1, MX_DE = 2, MX_ZE = 4, MX_OE = 8, MX_UE = 16, MX_PE = 32;
constexpr uint32_t MX_DAZ = 1u << 6, MX_FTZ = 1u << 15;

enum class Pfx { None, P66, F3, F2 };

int8_t SatS8(int32_t v) { return static_cast<int8_t>(std::min(127, std::max(-128, v))); }
uint8_t SatU8(int32_t v) { return static_cast<uint8_t>(std::min(255, std::max(0, v))); }
int16_t SatS16(int32_t v) { return static_cast<int16_t>(std::min(32767, std::max(-32768, v))); }
uint16_t SatU16(int32_t v) { return static_cast<uint16_t>(std::min(65535, std::max(0, v))); }

class Sse {
public:
    Sse(ExecContext& x, uint8_t op) : x_(x), c_(x.c), op_(op) {
        if (x.rep == 0xF3) pfx_ = Pfx::F3;
        else if (x.rep == 0xF2) pfx_ = Pfx::F2;
        else if (x.opsizePrefix) pfx_ = Pfx::P66;
    }

    bool Execute();

private:
    // --- Операнды ---
    ModRM M() { r_ = x_.DecodeModRM(); return r_; }

    // Читает операнд-источник (регистр rm или память) размером bytes.
    // aligned — для упакованных инструкций память должна быть выровнена на 16.
    bool LoadSrcXmm(XmmReg& v, int bytes, bool aligned = true) {
        v = XmmReg{};
        if (r_.isReg) { v = c_.xmm[r_.rm]; return true; }
        return LoadMem(v, bytes, aligned && bytes == 16);
    }
    bool LoadSrcMm(uint64_t& v) {
        if (r_.isReg) { v = c_.mm[r_.rm]; return true; }
        v = x_.ReadMem64(r_.addr);
        return !x_.m.Fault().pending;
    }
    bool LoadMem(XmmReg& v, int bytes, bool aligned) {
        if (aligned && (r_.addr & 15)) { GpFault(); return false; }
        for (int i = 0; i < bytes / 4; ++i) v.d[i] = x_.m.Read32(r_.addr + i * 4);
        return !x_.m.Fault().pending;
    }
    bool StoreMem(const XmmReg& v, int bytes, bool aligned) {
        if (aligned && (r_.addr & 15)) { GpFault(); return false; }
        if (!x_.m.IsRangeMapped(r_.addr, bytes)) { x_.m.Write8(x_.m.IsMapped(r_.addr) ? ((r_.addr | 0xFFF) + 1) : r_.addr, 0); return false; }
        for (int i = 0; i < bytes / 4; ++i) x_.m.Write32(r_.addr + i * 4, v.d[i]);
        return true;
    }
    void GpFault() {
        // #GP при невыровненном операнде — для гостя это ACCESS_VIOLATION
        x_.Raise(EXC_ACCESS_VIOLATION);
        x_.exc.faultAddress = 0xFFFFFFFFu;
    }
    void MmxTouched() { c_.fpuTop = 0; c_.fpuTag = 0; }

    // --- Вещественное ---
    uint32_t RC() const { return (c_.mxcsr >> 13) & 3; }
    // DAZ: денормальный операнд читается как ноль. Иначе — флаг DE, но
    // только если среди операндов этой дорожки нет NaN (проверено на железе:
    // NaN подавляет DE).
    template <typename T> T Daz(T v, bool laneHasNaN = false) {
        if (fp::IsDenormal(v)) {
            if (c_.mxcsr & MX_DAZ) return std::signbit(v) ? -static_cast<T>(0) : static_cast<T>(0);
            if (!laneHasNaN) flags_ |= MX_DE;
        }
        return v;
    }
    template <typename T> void Daz2(T& a, T& b) {
        bool nan = fp::IsNaN(a) || fp::IsNaN(b);
        a = Daz(a, nan);
        b = Daz(b, nan);
    }
    template <typename T> T Ftz(T v) {
        if ((c_.mxcsr & MX_FTZ) && fp::IsDenormal(v)) { flags_ |= MX_UE | MX_PE; return std::signbit(v) ? -static_cast<T>(0) : static_cast<T>(0); }
        return v;
    }
    // Денормальный операнд (без DAZ): DE ставится, только если на этой
    // дорожке нет NaN, IE и ZE — у них приоритет (поведение железа).
    template <typename T> bool DenormalInput(T a, T b) const {
        if (c_.mxcsr & MX_DAZ) return false;
        if (fp::IsNaN(a) || fp::IsNaN(b)) return false;
        return fp::IsDenormal(a) || fp::IsDenormal(b);
    }
    template <typename T> T Op(int op, T a, T b) { // 0 add 1 sub 2 mul 3 div 4 min 5 max
        bool den = DenormalInput(a, b);
        uint32_t lane = 0;
        if (c_.mxcsr & MX_DAZ) { a = Daz(a, true); b = Daz(b, true); }
        T r;
        if (op == 4 || op == 5) {
            if (fp::IsNaN(a) || fp::IsNaN(b)) { lane |= MX_IE; r = b; }
            else if (op == 4) r = a < b ? a : b;
            else r = a > b ? a : b;
        } else {
            r = fp::Arith(op, a, b, RC(), lane);
            r = FtzLane(r, lane);
        }
        if (den && !(lane & (MX_IE | MX_ZE))) lane |= MX_DE;
        flags_ |= lane;
        return r;
    }
    template <typename T> T SqrtOp(T a) {
        bool den = DenormalInput(a, a);
        uint32_t lane = 0;
        if (c_.mxcsr & MX_DAZ) a = Daz(a, true);
        T r = FtzLane(fp::Sqrt(a, RC(), lane), lane);
        if (den && !(lane & (MX_IE | MX_ZE))) lane |= MX_DE;
        flags_ |= lane;
        return r;
    }
    template <typename T> T FtzLane(T v, uint32_t& lane) {
        if ((c_.mxcsr & MX_FTZ) && fp::IsDenormal(v)) { lane |= MX_UE | MX_PE; return std::signbit(v) ? -static_cast<T>(0) : static_cast<T>(0); }
        return v;
    }

    // CMPPS-предикаты
    template <typename T> bool Cmp(int pred, T a, T b) {
        Daz2(a, b);
        bool nan = fp::IsNaN(a) || fp::IsNaN(b);
        bool sn = fp::IsSNaN(a) || fp::IsSNaN(b);
        int p = pred & 7;
        bool signaling = p == 1 || p == 2 || p == 5 || p == 6;
        if (sn || (nan && signaling)) flags_ |= MX_IE;
        switch (p) {
            case 0: return !nan && a == b;
            case 1: return !nan && a < b;
            case 2: return !nan && a <= b;
            case 3: return nan;
            case 4: return nan || a != b;
            case 5: return nan || !(a < b);
            case 6: return nan || !(a <= b);
            default: return !nan;
        }
    }
    template <typename T> void Comis(T a, T b, bool ordered) {
        Daz2(a, b);
        bool nan = fp::IsNaN(a) || fp::IsNaN(b);
        if (fp::IsSNaN(a) || fp::IsSNaN(b) || (ordered && nan)) flags_ |= MX_IE;
        c_.MaterializeFlags();
        uint32_t f = c_.eflags & ~FLAGS_ARITH;
        if (nan) f |= FLAG_ZF | FLAG_PF | FLAG_CF;
        else if (a < b) f |= FLAG_CF;
        else if (a == b) f |= FLAG_ZF;
        c_.eflags = f;
    }
    float D2F(double d) {
        d = Daz(d);
        if (fp::IsNaN(d)) {
            if (fp::IsSNaN(d)) flags_ |= MX_IE;
            uint64_t b = fp::Bits(d);
            return fp::F32(static_cast<uint32_t>((b >> 32) & 0x80000000u) | 0x7FC00000u |
                           static_cast<uint32_t>((b >> 29) & 0x003FFFFFu));
        }
        fp::Scope s(RC());
        float f = static_cast<float>(d);
        flags_ |= s.Flags() & (MX_OE | MX_UE | MX_PE);
        return Ftz(f);
    }
    double F2D(float f) {
        f = Daz(f);
        if (fp::IsNaN(f)) {
            if (fp::IsSNaN(f)) flags_ |= MX_IE;
            uint32_t b = fp::Bits(f);
            return fp::F64((static_cast<uint64_t>(b & 0x80000000u) << 32) | 0x7FF8000000000000ull |
                           (static_cast<uint64_t>(b & 0x003FFFFFu) << 29));
        }
        return static_cast<double>(f);
    }
    float I2F(int32_t i) { fp::Scope s(RC()); float f = static_cast<float>(i); flags_ |= s.Flags() & MX_PE; return f; }
    int32_t F2I(float f, bool trunc) { return fp::ToInt32(f, trunc, RC(), flags_); }
    int32_t D2I(double d, bool trunc) { return fp::ToInt32(d, trunc, RC(), flags_); }

    // Завершение вещественной инструкции: флаги в MXCSR, проверка маски.
    bool Commit() {
        flags_ &= 0x3F;
        c_.mxcsr |= flags_;
        uint32_t unmasked = flags_ & ~(c_.mxcsr >> 7) & 0x3F;
        if (unmasked) { x_.Raise(0xC00002B5u); return false; } // STATUS_FLOAT_MULTIPLE_TRAPS (#XM)
        return true;
    }

    bool Float58();         // 51,58..5F арифметика
    bool IntegerSimd();     // 60..7F, D0..FF и прочие целочисленные
    template <typename Fn> bool IntBinary(Fn fn); // dst = fn(dst, src) по 64/128 бит

    ExecContext& x_;
    CpuState& c_;
    uint8_t op_;
    Pfx pfx_ = Pfx::None;
    ModRM r_;
    uint32_t flags_ = 0;
};

// Целочисленная бинарная операция над mm (без префикса) или xmm (66).
template <typename Fn>
bool Sse::IntBinary(Fn fn) {
    M();
    if (x_.fetchFault) return true;
    if (pfx_ == Pfx::P66) {
        XmmReg b;
        if (!LoadSrcXmm(b, 16)) return true;
        XmmReg a = c_.xmm[r_.reg];
        fn(a, b, 16);
        c_.xmm[r_.reg] = a;
    } else if (pfx_ == Pfx::None) {
        uint64_t bv;
        if (!LoadSrcMm(bv)) return true;
        XmmReg a{}, b{};
        a.q[0] = c_.mm[r_.reg];
        b.q[0] = bv;
        fn(a, b, 8);
        c_.mm[r_.reg] = a.q[0];
        MmxTouched();
    } else {
        x_.Raise(EXC_ILLEGAL_INSTRUCTION);
    }
    return true;
}

bool Sse::Float58() {
    M();
    if (x_.fetchFault) return true;
    XmmReg& d = c_.xmm[r_.reg];
    int kind = op_ == 0x51 ? 6 : (op_ == 0x58 ? 0 : op_ == 0x59 ? 2 : op_ == 0x5C ? 1 : op_ == 0x5D ? 4 : op_ == 0x5E ? 3 : 5);
    XmmReg s;
    XmmReg out = d;
    switch (pfx_) {
        case Pfx::None:
            if (!LoadSrcXmm(s, 16)) return true;
            for (int i = 0; i < 4; ++i) out.f[i] = kind == 6 ? SqrtOp(s.f[i]) : Op(kind, d.f[i], s.f[i]);
            break;
        case Pfx::P66:
            if (!LoadSrcXmm(s, 16)) return true;
            for (int i = 0; i < 2; ++i) out.fd[i] = kind == 6 ? SqrtOp(s.fd[i]) : Op(kind, d.fd[i], s.fd[i]);
            break;
        case Pfx::F3:
            if (!LoadSrcXmm(s, 4, false)) return true;
            out.f[0] = kind == 6 ? SqrtOp(s.f[0]) : Op(kind, d.f[0], s.f[0]);
            break;
        case Pfx::F2:
            if (!LoadSrcXmm(s, 8, false)) return true;
            out.fd[0] = kind == 6 ? SqrtOp(s.fd[0]) : Op(kind, d.fd[0], s.fd[0]);
            break;
    }
    if (Commit()) d = out;
    return true;
}

bool Sse::IntegerSimd() {
    switch (op_) {
        // --- распаковка/упаковка ---
        case 0x60: case 0x61: case 0x62: case 0x68: case 0x69: case 0x6A: {
            bool high = op_ >= 0x68;
            int lane = (op_ & 3) == 0 ? 1 : (op_ & 3) == 1 ? 2 : 4;
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                XmmReg r{};
                int n = bytes / lane / 2; // пар
                int base = high ? n : 0;
                for (int i = 0; i < n; ++i) {
                    std::memcpy(r.b + (2 * i) * lane, a.b + (base + i) * lane, lane);
                    std::memcpy(r.b + (2 * i + 1) * lane, b.b + (base + i) * lane, lane);
                }
                a = r;
            });
        }
        case 0x6C: case 0x6D: {
            if (pfx_ != Pfx::P66) return false;
            bool high = op_ == 0x6D;
            return IntBinary([&](XmmReg& a, const XmmReg& b, int) {
                XmmReg r;
                r.q[0] = high ? a.q[1] : a.q[0];
                r.q[1] = high ? b.q[1] : b.q[0];
                a = r;
            });
        }
        case 0x63: case 0x67: case 0x6B: {
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                XmmReg r{};
                if (op_ == 0x6B) { // PACKSSDW
                    int n = bytes / 4;
                    for (int i = 0; i < n; ++i) r.sw[i] = SatS16(a.sd[i]);
                    for (int i = 0; i < n; ++i) r.sw[n + i] = SatS16(b.sd[i]);
                } else {
                    int n = bytes / 2;
                    for (int i = 0; i < n; ++i) {
                        if (op_ == 0x63) r.sb[i] = SatS8(a.sw[i]); else r.b[i] = SatU8(a.sw[i]);
                    }
                    for (int i = 0; i < n; ++i) {
                        if (op_ == 0x63) r.sb[n + i] = SatS8(b.sw[i]); else r.b[n + i] = SatU8(b.sw[i]);
                    }
                }
                a = r;
            });
        }
        // --- сравнения ---
        case 0x64: case 0x65: case 0x66: case 0x74: case 0x75: case 0x76: {
            bool gt = op_ < 0x70;
            int lane = 1 << (op_ & 3);
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                for (int i = 0; i < bytes / lane; ++i) {
                    bool t;
                    if (lane == 1) t = gt ? a.sb[i] > b.sb[i] : a.b[i] == b.b[i];
                    else if (lane == 2) t = gt ? a.sw[i] > b.sw[i] : a.w[i] == b.w[i];
                    else t = gt ? a.sd[i] > b.sd[i] : a.d[i] == b.d[i];
                    if (lane == 1) a.b[i] = t ? 0xFF : 0;
                    else if (lane == 2) a.w[i] = t ? 0xFFFF : 0;
                    else a.d[i] = t ? 0xFFFFFFFFu : 0;
                }
            });
        }
        // --- логика ---
        case 0xDB: case 0xDF: case 0xEB: case 0xEF:
            return IntBinary([&](XmmReg& a, const XmmReg& b, int) {
                for (int i = 0; i < 2; ++i) {
                    if (op_ == 0xDB) a.q[i] &= b.q[i];
                    else if (op_ == 0xDF) a.q[i] = ~a.q[i] & b.q[i];
                    else if (op_ == 0xEB) a.q[i] |= b.q[i];
                    else a.q[i] ^= b.q[i];
                }
            });
        // --- сложение/вычитание ---
        case 0xFC: case 0xFD: case 0xFE: case 0xD4: case 0xF8: case 0xF9: case 0xFA: case 0xFB: {
            bool sub = op_ >= 0xF8 && op_ <= 0xFB;
            int lane = op_ == 0xD4 || op_ == 0xFB ? 8 : (op_ & 3) == 0 ? 1 : (op_ & 3) == 1 ? 2 : 4;
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                for (int i = 0; i < bytes / lane; ++i) {
                    if (lane == 1) a.b[i] = static_cast<uint8_t>(sub ? a.b[i] - b.b[i] : a.b[i] + b.b[i]);
                    else if (lane == 2) a.w[i] = static_cast<uint16_t>(sub ? a.w[i] - b.w[i] : a.w[i] + b.w[i]);
                    else if (lane == 4) a.d[i] = sub ? a.d[i] - b.d[i] : a.d[i] + b.d[i];
                    else a.q[i] = sub ? a.q[i] - b.q[i] : a.q[i] + b.q[i];
                }
            });
        }
        case 0xEC: case 0xED: case 0xE8: case 0xE9: case 0xDC: case 0xDD: case 0xD8: case 0xD9: {
            bool sub = op_ == 0xE8 || op_ == 0xE9 || op_ == 0xD8 || op_ == 0xD9;
            bool sgn = op_ >= 0xE8;
            bool word = op_ & 1;
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                int n = bytes / (word ? 2 : 1);
                for (int i = 0; i < n; ++i) {
                    if (!word) {
                        int32_t v = sgn ? (sub ? a.sb[i] - b.sb[i] : a.sb[i] + b.sb[i]) : (sub ? a.b[i] - b.b[i] : a.b[i] + b.b[i]);
                        if (sgn) a.sb[i] = SatS8(v); else a.b[i] = SatU8(v);
                    } else {
                        int32_t v = sgn ? (sub ? a.sw[i] - b.sw[i] : a.sw[i] + b.sw[i]) : (sub ? a.w[i] - b.w[i] : a.w[i] + b.w[i]);
                        if (sgn) a.sw[i] = SatS16(v); else a.w[i] = SatU16(v);
                    }
                }
            });
        }
        // --- умножение ---
        case 0xD5: case 0xE5: case 0xE4:
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                for (int i = 0; i < bytes / 2; ++i) {
                    if (op_ == 0xD5) a.w[i] = static_cast<uint16_t>(a.sw[i] * b.sw[i]);
                    else if (op_ == 0xE5) a.w[i] = static_cast<uint16_t>((a.sw[i] * b.sw[i]) >> 16);
                    else a.w[i] = static_cast<uint16_t>((static_cast<uint32_t>(a.w[i]) * b.w[i]) >> 16);
                }
            });
        case 0xF4: // PMULUDQ
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                for (int i = 0; i < bytes / 8; ++i) a.q[i] = static_cast<uint64_t>(a.d[i * 2]) * b.d[i * 2];
            });
        case 0xF5: // PMADDWD
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                XmmReg r{};
                for (int i = 0; i < bytes / 4; ++i)
                    r.sd[i] = a.sw[2 * i] * b.sw[2 * i] + a.sw[2 * i + 1] * b.sw[2 * i + 1];
                a = r;
            });
        case 0xF6: // PSADBW
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                XmmReg r{};
                for (int h = 0; h < bytes / 8; ++h) {
                    uint32_t sum = 0;
                    for (int i = 0; i < 8; ++i) sum += static_cast<uint32_t>(std::abs(a.b[h * 8 + i] - b.b[h * 8 + i]));
                    r.q[h] = sum;
                }
                a = r;
            });
        case 0xE0: case 0xE3: // PAVGB/PAVGW
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                if (op_ == 0xE0) for (int i = 0; i < bytes; ++i) a.b[i] = static_cast<uint8_t>((a.b[i] + b.b[i] + 1) >> 1);
                else for (int i = 0; i < bytes / 2; ++i) a.w[i] = static_cast<uint16_t>((a.w[i] + b.w[i] + 1) >> 1);
            });
        case 0xDA: case 0xDE: case 0xEA: case 0xEE: // PMINUB PMAXUB PMINSW PMAXSW
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                if (op_ == 0xDA || op_ == 0xDE)
                    for (int i = 0; i < bytes; ++i) a.b[i] = op_ == 0xDA ? std::min(a.b[i], b.b[i]) : std::max(a.b[i], b.b[i]);
                else
                    for (int i = 0; i < bytes / 2; ++i) a.sw[i] = op_ == 0xEA ? std::min(a.sw[i], b.sw[i]) : std::max(a.sw[i], b.sw[i]);
            });
        // --- сдвиги на регистр ---
        case 0xD1: case 0xD2: case 0xD3: case 0xE1: case 0xE2: case 0xF1: case 0xF2: case 0xF3: {
            int lane = (op_ & 3) == 1 ? 2 : (op_ & 3) == 2 ? 4 : 8;
            int kind = op_ >= 0xF0 ? 0 : (op_ >= 0xE0 ? 2 : 1); // 0 sll, 1 srl, 2 sra
            return IntBinary([&](XmmReg& a, const XmmReg& b, int bytes) {
                uint64_t cnt = b.q[0];
                int bits = lane * 8;
                for (int i = 0; i < bytes / lane; ++i) {
                    if (lane == 2) {
                        if (kind == 2) a.sw[i] = static_cast<int16_t>(a.sw[i] >> (cnt > 15 ? 15 : cnt));
                        else a.w[i] = cnt >= static_cast<uint64_t>(bits) ? 0 : static_cast<uint16_t>(kind == 0 ? a.w[i] << cnt : a.w[i] >> cnt);
                    } else if (lane == 4) {
                        if (kind == 2) a.sd[i] = a.sd[i] >> (cnt > 31 ? 31 : cnt);
                        else a.d[i] = cnt >= static_cast<uint64_t>(bits) ? 0 : (kind == 0 ? a.d[i] << cnt : a.d[i] >> cnt);
                    } else {
                        a.q[i] = cnt >= 64 ? 0 : (kind == 0 ? a.q[i] << cnt : a.q[i] >> cnt);
                    }
                }
            });
        }
        default:
            return false;
    }
}

bool Sse::Execute() {
    switch (op_) {
        // ===== перемещения =====
        case 0x10: case 0x11: {
            M();
            if (x_.fetchFault) return true;
            bool store = op_ == 0x11;
            int bytes = pfx_ == Pfx::F3 ? 4 : pfx_ == Pfx::F2 ? 8 : 16;
            if (store) {
                if (r_.isReg) {
                    XmmReg& d = c_.xmm[r_.rm];
                    if (bytes == 16) d = c_.xmm[r_.reg];
                    else std::memcpy(d.b, c_.xmm[r_.reg].b, bytes);
                } else {
                    StoreMem(c_.xmm[r_.reg], bytes, false);
                }
            } else {
                XmmReg& d = c_.xmm[r_.reg];
                if (r_.isReg) {
                    if (bytes == 16) d = c_.xmm[r_.rm];
                    else std::memcpy(d.b, c_.xmm[r_.rm].b, bytes);
                } else {
                    XmmReg v;
                    if (!LoadMem(v, bytes, false)) return true;
                    if (bytes == 16) d = v;
                    else { d = XmmReg{}; std::memcpy(d.b, v.b, bytes); }
                }
            }
            return true;
        }
        case 0x12: case 0x13: case 0x16: case 0x17: {
            M();
            if (x_.fetchFault) return true;
            bool high = op_ >= 0x16;
            bool store = op_ & 1;
            XmmReg& d = c_.xmm[r_.reg];
            if (pfx_ == Pfx::F2 && op_ == 0x12) { // MOVDDUP
                XmmReg s;
                if (!LoadSrcXmm(s, 8, false)) return true;
                d.q[0] = d.q[1] = s.q[0];
                return true;
            }
            if (pfx_ == Pfx::F3 && (op_ == 0x12 || op_ == 0x16)) { // MOVSLDUP / MOVSHDUP
                XmmReg s;
                if (!LoadSrcXmm(s, 16)) return true;
                int o = op_ == 0x16 ? 1 : 0;
                d.d[0] = d.d[1] = s.d[o];
                d.d[2] = d.d[3] = s.d[2 + o];
                return true;
            }
            if (store) {
                if (r_.isReg) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
                x_.WriteMem64(r_.addr, high ? d.q[1] : d.q[0]);
                return true;
            }
            if (r_.isReg) {
                if (pfx_ == Pfx::P66) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
                // MOVHLPS / MOVLHPS
                if (high) d.q[1] = c_.xmm[r_.rm].q[0]; else d.q[0] = c_.xmm[r_.rm].q[1];
                return true;
            }
            uint64_t v = x_.ReadMem64(r_.addr);
            if (x_.m.Fault().pending) return true;
            if (high) d.q[1] = v; else d.q[0] = v;
            return true;
        }
        case 0x14: case 0x15: { // UNPCKL/HPS/PD
            M();
            if (x_.fetchFault) return true;
            XmmReg s;
            if (!LoadSrcXmm(s, 16)) return true;
            XmmReg& d = c_.xmm[r_.reg];
            XmmReg r;
            bool high = op_ == 0x15;
            if (pfx_ == Pfx::P66) { r.q[0] = high ? d.q[1] : d.q[0]; r.q[1] = high ? s.q[1] : s.q[0]; }
            else {
                int b = high ? 2 : 0;
                r.d[0] = d.d[b]; r.d[1] = s.d[b]; r.d[2] = d.d[b + 1]; r.d[3] = s.d[b + 1];
            }
            d = r;
            return true;
        }
        case 0x28: case 0x29: case 0x2B: { // MOVAPS/MOVAPD, MOVNTPS/PD
            M();
            if (x_.fetchFault) return true;
            if (op_ == 0x28) {
                XmmReg s;
                if (!LoadSrcXmm(s, 16)) return true;
                c_.xmm[r_.reg] = s;
            } else if (r_.isReg) {
                if (op_ == 0x2B) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
                c_.xmm[r_.rm] = c_.xmm[r_.reg];
            } else {
                StoreMem(c_.xmm[r_.reg], 16, true);
            }
            return true;
        }
        case 0x6E: { // MOVD mm/xmm, r/m32
            M();
            if (x_.fetchFault) return true;
            uint32_t v = x_.ReadRM(r_, 32);
            if (x_.m.Fault().pending) return true;
            if (pfx_ == Pfx::P66) { c_.xmm[r_.reg] = XmmReg{}; c_.xmm[r_.reg].d[0] = v; }
            else if (pfx_ == Pfx::None) { c_.mm[r_.reg] = v; MmxTouched(); }
            else return false;
            return true;
        }
        case 0x7E: {
            M();
            if (x_.fetchFault) return true;
            if (pfx_ == Pfx::F3) { // MOVQ xmm, xmm/m64
                XmmReg s;
                if (!LoadSrcXmm(s, 8, false)) return true;
                XmmReg& d = c_.xmm[r_.reg];
                d.q[0] = s.q[0];
                d.q[1] = 0;
                return true;
            }
            uint32_t v;
            if (pfx_ == Pfx::P66) v = c_.xmm[r_.reg].d[0];
            else if (pfx_ == Pfx::None) { v = static_cast<uint32_t>(c_.mm[r_.reg]); MmxTouched(); }
            else return false;
            x_.WriteRM(r_, 32, v);
            return true;
        }
        case 0x6F: case 0x7F: {
            M();
            if (x_.fetchFault) return true;
            bool store = op_ == 0x7F;
            if (pfx_ == Pfx::None) { // MOVQ mm
                MmxTouched();
                if (store) {
                    if (r_.isReg) c_.mm[r_.rm] = c_.mm[r_.reg];
                    else x_.WriteMem64(r_.addr, c_.mm[r_.reg]);
                } else {
                    uint64_t v;
                    if (LoadSrcMm(v)) c_.mm[r_.reg] = v;
                }
                return true;
            }
            if (pfx_ == Pfx::F2) return false;
            bool aligned = pfx_ == Pfx::P66; // MOVDQA / MOVDQU
            if (store) {
                if (r_.isReg) c_.xmm[r_.rm] = c_.xmm[r_.reg];
                else StoreMem(c_.xmm[r_.reg], 16, aligned);
            } else {
                XmmReg s;
                if (LoadSrcXmm(s, 16, aligned)) c_.xmm[r_.reg] = s;
            }
            return true;
        }
        case 0xD6: {
            M();
            if (x_.fetchFault) return true;
            if (pfx_ == Pfx::P66) { // MOVQ xmm/m64, xmm
                if (r_.isReg) { c_.xmm[r_.rm].q[0] = c_.xmm[r_.reg].q[0]; c_.xmm[r_.rm].q[1] = 0; }
                else x_.WriteMem64(r_.addr, c_.xmm[r_.reg].q[0]);
                return true;
            }
            if (!r_.isReg) return false;
            if (pfx_ == Pfx::F3) { c_.xmm[r_.reg].q[0] = c_.mm[r_.rm & 7]; c_.xmm[r_.reg].q[1] = 0; MmxTouched(); return true; } // MOVQ2DQ
            if (pfx_ == Pfx::F2) { c_.mm[r_.reg] = c_.xmm[r_.rm].q[0]; MmxTouched(); return true; }                            // MOVDQ2Q
            return false;
        }
        case 0xE7: { // MOVNTQ / MOVNTDQ
            M();
            if (x_.fetchFault) return true;
            if (r_.isReg) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            if (pfx_ == Pfx::P66) StoreMem(c_.xmm[r_.reg], 16, true);
            else if (pfx_ == Pfx::None) { x_.WriteMem64(r_.addr, c_.mm[r_.reg]); MmxTouched(); }
            else return false;
            return true;
        }
        case 0xC3: { // MOVNTI
            M();
            if (x_.fetchFault) return true;
            if (r_.isReg || pfx_ != Pfx::None) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            x_.WriteMem(r_.addr, 32, c_.regs[r_.reg]);
            return true;
        }
        case 0xF7: { // MASKMOVQ / MASKMOVDQU: запись по [EDI] байтов с маской
            M();
            if (x_.fetchFault) return true;
            if (!r_.isReg) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            uint32_t base = (x_.asz == 16 ? (c_.regs[7] & 0xFFFF) : c_.regs[7]) + x_.DataSegBase();
            if (pfx_ == Pfx::P66) {
                for (int i = 0; i < 16; ++i) if (c_.xmm[r_.rm].b[i] & 0x80) x_.m.Write8(base + i, c_.xmm[r_.reg].b[i]);
            } else {
                MmxTouched();
                for (int i = 0; i < 8; ++i)
                    if ((c_.mm[r_.rm] >> (i * 8 + 7)) & 1) x_.m.Write8(base + i, static_cast<uint8_t>(c_.mm[r_.reg] >> (i * 8)));
            }
            return true;
        }
        case 0x77: // EMMS
            c_.fpuTag = 0xFFFF;
            return true;

        // ===== перестановки =====
        case 0x70: {
            M();
            uint8_t imm = x_.Fetch8();
            if (x_.fetchFault) return true;
            if (pfx_ == Pfx::None) { // PSHUFW
                uint64_t s;
                if (!LoadSrcMm(s)) return true;
                uint64_t r = 0;
                for (int i = 0; i < 4; ++i) r |= ((s >> (((imm >> (2 * i)) & 3) * 16)) & 0xFFFF) << (16 * i);
                c_.mm[r_.reg] = r;
                MmxTouched();
                return true;
            }
            XmmReg s;
            if (!LoadSrcXmm(s, 16)) return true;
            XmmReg r = s;
            if (pfx_ == Pfx::P66) for (int i = 0; i < 4; ++i) r.d[i] = s.d[(imm >> (2 * i)) & 3];
            else if (pfx_ == Pfx::F3) for (int i = 0; i < 4; ++i) r.w[4 + i] = s.w[4 + ((imm >> (2 * i)) & 3)];
            else for (int i = 0; i < 4; ++i) r.w[i] = s.w[(imm >> (2 * i)) & 3];
            c_.xmm[r_.reg] = r;
            return true;
        }
        case 0xC6: { // SHUFPS / SHUFPD
            M();
            uint8_t imm = x_.Fetch8();
            if (x_.fetchFault) return true;
            XmmReg s;
            if (!LoadSrcXmm(s, 16)) return true;
            XmmReg& d = c_.xmm[r_.reg];
            XmmReg r;
            if (pfx_ == Pfx::P66) { r.q[0] = d.q[imm & 1]; r.q[1] = s.q[(imm >> 1) & 1]; }
            else {
                r.d[0] = d.d[imm & 3]; r.d[1] = d.d[(imm >> 2) & 3];
                r.d[2] = s.d[(imm >> 4) & 3]; r.d[3] = s.d[(imm >> 6) & 3];
            }
            d = r;
            return true;
        }
        case 0xC4: { // PINSRW
            M();
            uint8_t imm = x_.Fetch8();
            if (x_.fetchFault) return true;
            uint32_t v = r_.isReg ? c_.regs[r_.rm] : x_.ReadMem(r_.addr, 16);
            if (x_.m.Fault().pending) return true;
            if (pfx_ == Pfx::P66) c_.xmm[r_.reg].w[imm & 7] = static_cast<uint16_t>(v);
            else {
                int sh = (imm & 3) * 16;
                c_.mm[r_.reg] = (c_.mm[r_.reg] & ~(0xFFFFull << sh)) | (static_cast<uint64_t>(v & 0xFFFF) << sh);
                MmxTouched();
            }
            return true;
        }
        case 0xC5: { // PEXTRW
            M();
            uint8_t imm = x_.Fetch8();
            if (x_.fetchFault) return true;
            if (!r_.isReg) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            if (pfx_ == Pfx::P66) c_.regs[r_.reg] = c_.xmm[r_.rm].w[imm & 7];
            else { c_.regs[r_.reg] = static_cast<uint32_t>((c_.mm[r_.rm] >> ((imm & 3) * 16)) & 0xFFFF); MmxTouched(); }
            return true;
        }
        case 0xD7: { // PMOVMSKB
            M();
            if (x_.fetchFault) return true;
            if (!r_.isReg) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            uint32_t m = 0;
            if (pfx_ == Pfx::P66) for (int i = 0; i < 16; ++i) m |= static_cast<uint32_t>(c_.xmm[r_.rm].b[i] >> 7) << i;
            else { for (int i = 0; i < 8; ++i) m |= static_cast<uint32_t>((c_.mm[r_.rm] >> (i * 8 + 7)) & 1) << i; MmxTouched(); }
            c_.regs[r_.reg] = m;
            return true;
        }
        case 0x50: { // MOVMSKPS/PD
            M();
            if (x_.fetchFault) return true;
            if (!r_.isReg) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            const XmmReg& s = c_.xmm[r_.rm];
            uint32_t m = 0;
            if (pfx_ == Pfx::P66) m = static_cast<uint32_t>((s.q[0] >> 63) | ((s.q[1] >> 63) << 1));
            else for (int i = 0; i < 4; ++i) m |= (s.d[i] >> 31) << i;
            c_.regs[r_.reg] = m;
            return true;
        }

        // ===== сдвиги на непосредственное =====
        case 0x71: case 0x72: case 0x73: {
            M();
            uint8_t imm = x_.Fetch8();
            if (x_.fetchFault) return true;
            if (!r_.isReg) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            int lane = op_ == 0x71 ? 2 : op_ == 0x72 ? 4 : 8;
            int sub = r_.reg;
            bool xmm = pfx_ == Pfx::P66;
            XmmReg v{};
            if (xmm) v = c_.xmm[r_.rm]; else v.q[0] = c_.mm[r_.rm];
            int bytes = xmm ? 16 : 8;
            if (op_ == 0x73 && (sub == 3 || sub == 7)) { // PSRLDQ / PSLLDQ (только XMM)
                if (!xmm) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
                XmmReg r{};
                int n = imm > 16 ? 16 : imm;
                for (int i = 0; i < 16; ++i) {
                    int src = sub == 3 ? i + n : i - n;
                    r.b[i] = (src >= 0 && src < 16) ? v.b[src] : 0;
                }
                c_.xmm[r_.rm] = r;
                return true;
            }
            if (!(sub == 2 || sub == 6 || (sub == 4 && lane != 8))) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return true; }
            uint32_t cnt = imm;
            int bits = lane * 8;
            for (int i = 0; i < bytes / lane; ++i) {
                if (lane == 2) {
                    if (sub == 4) v.sw[i] = static_cast<int16_t>(v.sw[i] >> (cnt > 15 ? 15 : cnt));
                    else v.w[i] = cnt >= static_cast<uint32_t>(bits) ? 0 : static_cast<uint16_t>(sub == 6 ? v.w[i] << cnt : v.w[i] >> cnt);
                } else if (lane == 4) {
                    if (sub == 4) v.sd[i] = v.sd[i] >> (cnt > 31 ? 31 : cnt);
                    else v.d[i] = cnt >= static_cast<uint32_t>(bits) ? 0 : (sub == 6 ? v.d[i] << cnt : v.d[i] >> cnt);
                } else {
                    v.q[i] = cnt >= 64 ? 0 : (sub == 6 ? v.q[i] << cnt : v.q[i] >> cnt);
                }
            }
            if (xmm) c_.xmm[r_.rm] = v; else { c_.mm[r_.rm] = v.q[0]; MmxTouched(); }
            return true;
        }

        // ===== вещественная логика =====
        case 0x54: case 0x55: case 0x56: case 0x57: {
            if (pfx_ == Pfx::F2 || pfx_ == Pfx::F3) return false;
            M();
            if (x_.fetchFault) return true;
            XmmReg s;
            if (!LoadSrcXmm(s, 16)) return true;
            XmmReg& d = c_.xmm[r_.reg];
            for (int i = 0; i < 2; ++i) {
                if (op_ == 0x54) d.q[i] &= s.q[i];
                else if (op_ == 0x55) d.q[i] = ~d.q[i] & s.q[i];
                else if (op_ == 0x56) d.q[i] |= s.q[i];
                else d.q[i] ^= s.q[i];
            }
            return true;
        }

        // ===== вещественная арифметика =====
        case 0x51: case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            return Float58();
        case 0x52: case 0x53: { // RSQRT / RCP (приближения; у нас точнее железа)
            if (pfx_ == Pfx::P66 || pfx_ == Pfx::F2) return false;
            M();
            if (x_.fetchFault) return true;
            XmmReg s;
            if (!LoadSrcXmm(s, pfx_ == Pfx::F3 ? 4 : 16, pfx_ != Pfx::F3)) return true;
            XmmReg& d = c_.xmm[r_.reg];
            int n = pfx_ == Pfx::F3 ? 1 : 4;
            for (int i = 0; i < n; ++i) {
                float v = s.f[i];
                if (fp::IsNaN(v)) d.f[i] = fp::Quiet(v);
                else d.f[i] = op_ == 0x52 ? (v < 0 ? fp::F32(fp::kIndefinite32) : 1.0f / std::sqrt(v)) : 1.0f / v;
            }
            return true;
        }
        case 0xC2: { // CMPPS/PD/SS/SD
            M();
            uint8_t imm = x_.Fetch8();
            if (x_.fetchFault) return true;
            XmmReg& d = c_.xmm[r_.reg];
            XmmReg s, out = d;
            switch (pfx_) {
                case Pfx::None:
                    if (!LoadSrcXmm(s, 16)) return true;
                    for (int i = 0; i < 4; ++i) out.d[i] = Cmp(imm, d.f[i], s.f[i]) ? 0xFFFFFFFFu : 0;
                    break;
                case Pfx::P66:
                    if (!LoadSrcXmm(s, 16)) return true;
                    for (int i = 0; i < 2; ++i) out.q[i] = Cmp(imm, d.fd[i], s.fd[i]) ? ~0ull : 0;
                    break;
                case Pfx::F3: if (!LoadSrcXmm(s, 4, false)) return true;
                    out.d[0] = Cmp(imm, d.f[0], s.f[0]) ? 0xFFFFFFFFu : 0; break;
                case Pfx::F2: if (!LoadSrcXmm(s, 8, false)) return true;
                    out.q[0] = Cmp(imm, d.fd[0], s.fd[0]) ? ~0ull : 0; break;
            }
            if (Commit()) d = out;
            return true;
        }
        case 0x2E: case 0x2F: { // UCOMISS/COMISS (SD с 66)
            if (pfx_ == Pfx::F2 || pfx_ == Pfx::F3) return false;
            M();
            if (x_.fetchFault) return true;
            XmmReg s;
            bool dbl = pfx_ == Pfx::P66;
            if (!LoadSrcXmm(s, dbl ? 8 : 4, false)) return true;
            if (dbl) Comis(c_.xmm[r_.reg].fd[0], s.fd[0], op_ == 0x2F);
            else Comis(c_.xmm[r_.reg].f[0], s.f[0], op_ == 0x2F);
            Commit();
            return true;
        }

        // ===== преобразования =====
        case 0x2A: {
            M();
            if (x_.fetchFault) return true;
            XmmReg& d = c_.xmm[r_.reg];
            XmmReg out = d;
            if (pfx_ == Pfx::F3 || pfx_ == Pfx::F2) { // CVTSI2SS/SD r/m32
                int32_t i = static_cast<int32_t>(x_.ReadRM(r_, 32));
                if (x_.m.Fault().pending) return true;
                if (pfx_ == Pfx::F3) out.f[0] = I2F(i); else out.fd[0] = static_cast<double>(i);
            } else { // CVTPI2PS/PD mm/m64
                uint64_t v;
                if (!LoadSrcMm(v)) return true;
                MmxTouched();
                int32_t lo = static_cast<int32_t>(v), hi = static_cast<int32_t>(v >> 32);
                if (pfx_ == Pfx::None) { out.f[0] = I2F(lo); out.f[1] = I2F(hi); }
                else { out.fd[0] = lo; out.fd[1] = hi; }
            }
            if (Commit()) d = out;
            return true;
        }
        case 0x2C: case 0x2D: {
            bool trunc = op_ == 0x2C;
            M();
            if (x_.fetchFault) return true;
            if (pfx_ == Pfx::F3 || pfx_ == Pfx::F2) { // CVT(T)SS2SI / SD2SI
                XmmReg s;
                if (!LoadSrcXmm(s, pfx_ == Pfx::F3 ? 4 : 8, false)) return true;
                int32_t v = pfx_ == Pfx::F3 ? F2I(Daz(s.f[0]), trunc) : D2I(Daz(s.fd[0]), trunc);
                flags_ &= ~MX_DE; // эти преобразования не сообщают о денормалах
                if (Commit()) c_.regs[r_.reg] = static_cast<uint32_t>(v);
                return true;
            }
            XmmReg s;
            if (!LoadSrcXmm(s, pfx_ == Pfx::None ? 8 : 16, pfx_ == Pfx::P66)) return true;
            int32_t a, b;
            if (pfx_ == Pfx::None) { a = F2I(Daz(s.f[0]), trunc); b = F2I(Daz(s.f[1]), trunc); }
            else { a = D2I(Daz(s.fd[0]), trunc); b = D2I(Daz(s.fd[1]), trunc); }
            flags_ &= ~MX_DE;
            if (Commit()) {
                c_.mm[r_.reg] = static_cast<uint32_t>(a) | (static_cast<uint64_t>(static_cast<uint32_t>(b)) << 32);
                MmxTouched();
            }
            return true;
        }
        case 0x5A: {
            M();
            if (x_.fetchFault) return true;
            XmmReg& d = c_.xmm[r_.reg];
            XmmReg s, out = d;
            switch (pfx_) {
                case Pfx::None: // CVTPS2PD (m64)
                    if (!LoadSrcXmm(s, 8, false)) return true;
                    out.fd[0] = F2D(s.f[0]); out.fd[1] = F2D(s.f[1]);
                    break;
                case Pfx::P66: // CVTPD2PS
                    if (!LoadSrcXmm(s, 16)) return true;
                    out = XmmReg{};
                    out.f[0] = D2F(s.fd[0]); out.f[1] = D2F(s.fd[1]);
                    break;
                case Pfx::F3: // CVTSS2SD
                    if (!LoadSrcXmm(s, 4, false)) return true;
                    out.fd[0] = F2D(s.f[0]);
                    break;
                case Pfx::F2: // CVTSD2SS
                    if (!LoadSrcXmm(s, 8, false)) return true;
                    out.f[0] = D2F(s.fd[0]);
                    break;
            }
            if (Commit()) d = out;
            return true;
        }
        case 0x5B: {
            if (pfx_ == Pfx::F2) return false;
            M();
            if (x_.fetchFault) return true;
            XmmReg s;
            if (!LoadSrcXmm(s, 16)) return true;
            XmmReg out;
            if (pfx_ == Pfx::None) for (int i = 0; i < 4; ++i) out.f[i] = I2F(s.sd[i]);       // CVTDQ2PS
            else {
                bool trunc = pfx_ == Pfx::F3;                                                  // CVT(T)PS2DQ
                for (int i = 0; i < 4; ++i) out.sd[i] = F2I(Daz(s.f[i]), trunc);
                flags_ &= ~MX_DE;
            }
            if (Commit()) c_.xmm[r_.reg] = out;
            return true;
        }
        case 0xE6: {
            if (pfx_ == Pfx::None) return false;
            M();
            if (x_.fetchFault) return true;
            XmmReg s;
            XmmReg out{};
            if (pfx_ == Pfx::F3) { // CVTDQ2PD (m64)
                if (!LoadSrcXmm(s, 8, false)) return true;
                out.fd[0] = s.sd[0];
                out.fd[1] = s.sd[1];
            } else { // 66: CVTTPD2DQ, F2: CVTPD2DQ
                if (!LoadSrcXmm(s, 16)) return true;
                bool trunc = pfx_ == Pfx::P66;
                out.sd[0] = D2I(Daz(s.fd[0]), trunc);
                out.sd[1] = D2I(Daz(s.fd[1]), trunc);
                flags_ &= ~MX_DE;
            }
            if (Commit()) c_.xmm[r_.reg] = out;
            return true;
        }

        default:
            return IntegerSimd();
    }
}

} // namespace

bool ExecuteSse0F(ExecContext& x, uint8_t op) {
    Sse s(x, op);
    return s.Execute();
}

} // namespace detail
} // namespace cpu

// fpu.cpp — x87 FPU (опкоды D8..DF), FNSAVE/FRSTOR, FXSAVE/FXRSTOR.
//
// Регистры хранятся как double (см. cpu_state.h): Windows-программы по
// умолчанию работают с точностью 53 бита, и тогда результаты совпадают с
// железом. Режим 64-битной точности (PC=11) эмулируется приближённо.

#include <cmath>

#include "exec_context.h"
#include "fp_util.h"

namespace cpu {
namespace detail {

namespace {

// Биты статуса x87
constexpr uint16_t SW_IE = 1 << 0, SW_DE = 1 << 1, SW_ZE = 1 << 2, SW_OE = 1 << 3, SW_UE = 1 << 4,
                   SW_PE = 1 << 5, SW_SF = 1 << 6, SW_ES = 1 << 7, SW_C0 = 1 << 8, SW_C1 = 1 << 9,
                   SW_C2 = 1 << 10, SW_C3 = 1 << 14, SW_B = 1 << 15;
constexpr uint16_t SW_CC = SW_C0 | SW_C1 | SW_C2 | SW_C3;

enum ArithOp { kAdd, kMul, kSub, kSubR, kDiv, kDivR };

class Fpu {
public:
    explicit Fpu(ExecContext& x) : x_(x), c_(x.c) {}

    int Phys(int i) const { return (c_.fpuTop + i) & 7; }
    int Tag(int phys) const { return (c_.fpuTag >> (phys * 2)) & 3; }
    void SetTag(int phys, int t) {
        c_.fpuTag = static_cast<uint16_t>((c_.fpuTag & ~(3u << (phys * 2))) | (static_cast<unsigned>(t) << (phys * 2)));
    }
    static int Classify(double v) {
        if (v == 0.0) return 1;
        if (std::isnan(v) || std::isinf(v)) return 2;
        return 0;
    }
    bool Empty(int i) const { return Tag(Phys(i)) == 3; }
    double ST(int i) const { return c_.st[Phys(i)]; }
    void SetST(int i, double v) {
        int p = Phys(i);
        c_.st[p] = v;
        SetTag(p, Classify(v));
    }
    uint32_t RC() const { return (c_.fpuControl >> 10) & 3; }
    uint32_t PC() const { return (c_.fpuControl >> 8) & 3; }

    void SetCC(bool c0, bool c1, bool c2, bool c3) {
        c_.fpuStatus = static_cast<uint16_t>((c_.fpuStatus & ~SW_CC) | (c0 ? SW_C0 : 0) | (c1 ? SW_C1 : 0) |
                                             (c2 ? SW_C2 : 0) | (c3 ? SW_C3 : 0));
    }
    void SetC1(bool v) { c_.fpuStatus = static_cast<uint16_t>(v ? (c_.fpuStatus | SW_C1) : (c_.fpuStatus & ~SW_C1)); }

    // Регистрирует исключения; true — если хоть одно из них не замаскировано
    // (тогда результат не записывается, а при следующей инструкции FPU гость
    // получит исключение).
    bool Signal(uint32_t flags) {
        flags &= 0x3F;
        if (!flags) return false;
        c_.fpuStatus |= static_cast<uint16_t>(flags);
        uint32_t unmasked = flags & ~c_.fpuControl & 0x3F;
        if (unmasked) {
            c_.fpuStatus |= SW_ES | SW_B;
            return true;
        }
        return false;
    }

    // Чтение ST(i) с проверкой «стек пуст». false — регистр пуст (IE|SF выставлены).
    bool Read(int i, double& v) {
        if (Empty(i)) {
            SetC1(false);
            Signal(SW_IE);
            c_.fpuStatus |= SW_SF;
            v = fp::F64(fp::kIndefinite64);
            return false;
        }
        v = ST(i);
        return true;
    }

    void Push(double v) {
        int p = (c_.fpuTop - 1) & 7;
        if (Tag(p) != 3) {
            SetC1(true);
            c_.fpuStatus |= SW_SF;
            if (Signal(SW_IE)) return;
            v = fp::F64(fp::kIndefinite64);
        }
        c_.fpuTop = static_cast<uint8_t>(p);
        c_.st[p] = v;
        SetTag(p, Classify(v));
    }
    void Pop() {
        SetTag(Phys(0), 3);
        c_.fpuTop = static_cast<uint8_t>((c_.fpuTop + 1) & 7);
    }

    // Округление результата до точности PC.
    double Round(double v) const {
        if (PC() == 0) {
            fp::Scope s(RC());
            return static_cast<double>(static_cast<float>(v));
        }
        return v;
    }

    // Правило NaN x87 (таблица 4-7 руководства Intel): SNaN -> IE;
    // SNaN и QNaN -> QNaN-операнд; два NaN одного рода -> с большей мантиссой.
    static bool NaNResult(double a, double b, double& out, uint32_t& flags) {
        bool na = std::isnan(a), nb = std::isnan(b);
        if (!na && !nb) return false;
        bool sa = fp::IsSNaN(a), sb = fp::IsSNaN(b);
        if (sa || sb) flags |= SW_IE;
        if (na && nb) {
            if (sa != sb) out = fp::Quiet(sa ? b : a);
            else {
                uint64_t ma = fp::Bits(a) & 0x000FFFFFFFFFFFFFull;
                uint64_t mb = fp::Bits(b) & 0x000FFFFFFFFFFFFFull;
                // при равных мантиссах железо выбирает положительный
                bool pickB = mb > ma || (mb == ma && std::signbit(a) && !std::signbit(b));
                out = fp::Quiet(pickB ? b : a);
            }
        } else {
            out = fp::Quiet(na ? a : b);
        }
        return true;
    }

    double Arith(int op, double dst, double other, uint32_t& flags) {
        double r;
        if (NaNResult(dst, other, r, flags)) return r;
        {
            fp::Scope s(RC());
            switch (op) {
                case kAdd: r = dst + other; break;
                case kMul: r = dst * other; break;
                case kSub: r = dst - other; break;
                case kSubR: r = other - dst; break;
                case kDiv: r = dst / other; break;
                default: r = other / dst; break;
            }
            uint32_t f = s.Flags();
            flags |= (f & (fp::kIE | fp::kZE | fp::kOE | fp::kUE | fp::kPE));
            // Крошечный результат: x87 округляет мантиссу до 53 бит, сохраняя
            // широкий порядок, и только при записи в память — до денормала.
            // Повторяем это двумя округлениями через масштаб 2^600.
            if (PC() == 2 && std::isfinite(r) && std::fabs(r) < 2.2250738585072014e-308 &&
                std::isfinite(dst) && std::isfinite(other)) {
                const double up = 0x1p600, down = 0x1p-600;
                double t = r;
                switch (op) {
                    case kAdd: t = (dst * up) + (other * up); break;
                    case kSub: t = (dst * up) - (other * up); break;
                    case kSubR: t = (other * up) - (dst * up); break;
                    case kMul: t = std::fabs(dst) < std::fabs(other) ? (dst * up) * other : dst * (other * up); break;
                    case kDiv: t = (dst * up) / other; break;
                    default: t = (other * up) / dst; break;
                }
                if (std::isfinite(t)) r = t * down;
            }
        }
        if (std::isnan(r)) { r = fp::F64(fp::kIndefinite64); flags |= SW_IE; }
        return Round(r);
    }

    // Сравнение: 0 больше, 1 меньше, 2 равно, 3 неупорядочено
    static int Compare(double a, double b) {
        if (std::isnan(a) || std::isnan(b)) return 3;
        if (a > b) return 0;
        if (a < b) return 1;
        return 2;
    }
    void SetCompareCC(int r) {
        switch (r) {
            case 0: SetCC(false, false, false, false); break;
            case 1: SetCC(true, false, false, false); break;
            case 2: SetCC(false, false, false, true); break;
            default: SetCC(true, false, true, true); break;
        }
    }
    void SetCompareEflags(int r) {
        c_.MaterializeFlags();
        uint32_t f = c_.eflags & ~FLAGS_ARITH;
        if (r == 1) f |= FLAG_CF;
        else if (r == 2) f |= FLAG_ZF;
        else if (r == 3) f |= FLAG_ZF | FLAG_PF | FLAG_CF;
        c_.eflags = f;
    }
    // ordered: FCOM (IE на любой NaN), иначе FUCOM (IE только на SNaN)
    void CompareFlagsIE(double a, double b, bool ordered) {
        if (std::isnan(a) || std::isnan(b)) {
            if (ordered || fp::IsSNaN(a) || fp::IsSNaN(b)) Signal(SW_IE);
        }
    }

    // Операнд арифметики из памяти: SNaN не гасится (это делает NaNResult).
    double LoadMemRaw(uint32_t addr, int kind) {
        double v = kind == 0 ? fp::FloatToDoubleExact(fp::F32(x_.m.Read32(addr))) : fp::F64(x_.ReadMem64(addr));
        if (fp::IsDenormal(v) || (kind == 0 && fp::IsDenormal(fp::F32(x_.m.Read32(addr))))) Signal(SW_DE);
        return v;
    }

    double LoadMem(uint32_t addr, int kind) { // 0 m32, 1 m64, 2 m80
        uint32_t flags = 0;
        double v;
        if (kind == 0) {
            float f = fp::F32(x_.m.Read32(addr));
            if (fp::IsSNaN(f)) { flags |= SW_IE; f = fp::Quiet(f); }
            if (fp::IsDenormal(f)) flags |= SW_DE;
            v = static_cast<double>(f);
        } else if (kind == 1) {
            v = fp::F64(x_.ReadMem64(addr));
            if (fp::IsSNaN(v)) { flags |= SW_IE; v = fp::Quiet(v); }
            if (fp::IsDenormal(v)) flags |= SW_DE;
        } else {
            uint8_t b[10];
            for (int i = 0; i < 10; ++i) b[i] = x_.m.Read8(addr + i);
            v = fp::Ext80ToDouble(b);
        }
        Signal(flags);
        return v;
    }

    void StoreMem(uint32_t addr, int kind, double v) {
        if (kind == 0) {
            uint32_t flags = 0;
            float f;
            if (std::isnan(v)) {
                if (fp::IsSNaN(v)) flags |= SW_IE;
                uint64_t b = fp::Bits(v);
                uint32_t u = static_cast<uint32_t>((b >> 32) & 0x80000000u) | 0x7FC00000u |
                             static_cast<uint32_t>((b >> 29) & 0x003FFFFFu);
                f = fp::F32(u);
            } else {
                fp::Scope s(RC());
                f = static_cast<float>(v);
                flags |= s.Flags() & (fp::kOE | fp::kUE | fp::kPE);
            }
            if (Signal(flags)) return;
            x_.m.Write32(addr, fp::Bits(f));
        } else if (kind == 1) {
            x_.WriteMem64(addr, fp::Bits(v));
        } else {
            uint8_t b[10];
            fp::DoubleToExt80(v, b);
            if (!x_.m.IsRangeMapped(addr, 10)) { x_.m.Write8(addr, 0); return; }
            for (int i = 0; i < 10; ++i) x_.m.Write8(addr + i, b[i]);
        }
    }

    // FIST*: kind 0 m16, 1 m32, 2 m64. truncate — FISTTP.
    void StoreInt(uint32_t addr, int kind, double v, bool truncate) {
        uint32_t flags = 0;
        if (kind == 0) {
            int16_t i = fp::ToInt16(v, RC(), flags, truncate);
            if (Signal(flags & (SW_IE | SW_PE))) return;
            x_.m.Write16(addr, static_cast<uint16_t>(i));
        } else if (kind == 1) {
            int32_t i = fp::ToInt32(v, truncate, RC(), flags);
            if (Signal(flags & (SW_IE | SW_PE))) return;
            x_.m.Write32(addr, static_cast<uint32_t>(i));
        } else {
            int64_t i = fp::ToInt64(v, truncate, RC(), flags);
            if (Signal(flags & (SW_IE | SW_PE))) return;
            x_.WriteMem64(addr, static_cast<uint64_t>(i));
        }
    }

    double LoadInt(uint32_t addr, int kind) {
        if (kind == 0) return static_cast<double>(static_cast<int16_t>(x_.m.Read16(addr)));
        if (kind == 1) return static_cast<double>(static_cast<int32_t>(x_.m.Read32(addr)));
        // 64-битное целое x87 хранит точно, а мы — в double: округляем сразу
        // по режиму RC, это совпадает с последующей записью в память.
        int64_t i = static_cast<int64_t>(x_.ReadMem64(addr));
        fp::Scope s(RC());
        return static_cast<double>(i);
    }

    // --- Арифметика с операндом из памяти или ST(i) ---
    // dstIndex — куда пишется результат (0 для D8/DA/DC-mem/DE-mem, i для DC/DE-reg)
    void ArithTo(int op, int dstIndex, double other, bool otherValid, bool pop) {
        if (Empty(dstIndex) || !otherValid) { // стек пуст
            SetC1(false);
            c_.fpuStatus |= SW_SF;
            if (Signal(SW_IE)) return;
            SetST(dstIndex, fp::F64(fp::kIndefinite64));
            if (pop) Pop();
            return;
        }
        uint32_t flags = 0;
        double r = Arith(op, ST(dstIndex), other, flags);
        SetC1(false);
        if (Signal(flags)) return; // незамаскированное: результат не пишется
        SetST(dstIndex, r);
        if (pop) Pop();
    }

    void Compare(double other, bool otherValid, bool ordered, int pops) {
        double a;
        bool ok = Read(0, a);
        if (!otherValid) { SetC1(false); c_.fpuStatus |= SW_SF; Signal(SW_IE); ok = false; }
        if (!ok) SetCC(true, false, true, true);
        else {
            CompareFlagsIE(a, other, ordered);
            SetCompareCC(Compare(a, other));
        }
        for (int i = 0; i < pops; ++i) Pop();
    }

    void Execute();

private:
    void ExecuteMem(uint8_t op, const ModRM& r);
    void ExecuteReg(uint8_t op, const ModRM& r);
    void Transcendental(uint8_t rm);

    ExecContext& x_;
    CpuState& c_;
};

int ArithFromReg(uint8_t reg) {
    static const int ops[8] = {kAdd, kMul, -1, -1, kSub, kSubR, kDiv, kDivR};
    return ops[reg];
}
// DC/DE с регистром: SUB/SUBR и DIV/DIVR поменяны местами
int ArithFromRegReversed(uint8_t reg) {
    static const int ops[8] = {kAdd, kMul, -1, -1, kSubR, kSub, kDivR, kDiv};
    return ops[reg];
}

void Fpu::Execute() {
    uint8_t op = x_.opcode;
    ModRM r = x_.DecodeModRM();
    if (x_.fetchFault) return;

    // Отложенное незамаскированное исключение: «ожидающие» инструкции
    // FPU сообщают о нём (FNINIT/FNCLEX/FNSTSW/FNSTCW/FNSTENV/FNSAVE — нет).
    bool noWait = (op == 0xDB && r.isReg && (r.rm == 2 || r.rm == 3) && r.reg == 4) ||
                  (op == 0xD9 && !r.isReg && (r.reg == 6 || r.reg == 7)) ||
                  (op == 0xDD && !r.isReg && (r.reg == 6 || r.reg == 7)) ||
                  (op == 0xDF && r.isReg && r.reg == 4 && r.rm == 0);
    if (!noWait && (c_.fpuStatus & SW_ES)) {
        uint16_t pend = c_.fpuStatus & ~c_.fpuControl & 0x3F;
        if (pend & SW_ZE) x_.Raise(EXC_FLT_DIVIDE_BY_ZERO);
        else if (pend & (SW_IE)) x_.Raise((c_.fpuStatus & SW_SF) ? EXC_FLT_STACK_CHECK : EXC_FLT_INVALID_OPERATION);
        else if (pend & SW_OE) x_.Raise(0xC0000091u); // FLOAT_OVERFLOW
        else if (pend & SW_UE) x_.Raise(0xC0000093u); // FLOAT_UNDERFLOW
        else if (pend & SW_PE) x_.Raise(0xC000008Fu); // FLOAT_INEXACT_RESULT
        else x_.Raise(0xC000008Du);                   // FLOAT_DENORMAL_OPERAND
        return;
    }

    if (!(op == 0xD9 && !r.isReg && r.reg >= 4) && !(op == 0xDD && !r.isReg && r.reg >= 4) &&
        !(op == 0xDF && r.isReg && r.reg == 4)) {
        c_.fpuLastIp = x_.start;
        c_.fpuLastOpcode = static_cast<uint16_t>(((op & 7) << 8) | (r.mod << 6) | (r.reg << 3) | r.rm);
        if (!r.isReg) c_.fpuLastDp = r.ea;
    }
    if (r.isReg) ExecuteReg(op, r); else ExecuteMem(op, r);
}

void Fpu::ExecuteMem(uint8_t op, const ModRM& r) {
    uint32_t a = r.addr;
    switch (op) {
        case 0xD8: case 0xDA: case 0xDC: case 0xDE: {
            double v;
            if (op == 0xD8) v = LoadMemRaw(a, 0);
            else if (op == 0xDC) v = LoadMemRaw(a, 1);
            else v = LoadInt(a, op == 0xDA ? 1 : 0);
            if (x_.m.Fault().pending) return;
            if (r.reg == 2 || r.reg == 3) Compare(v, true, true, r.reg == 3 ? 1 : 0);
            else ArithTo(ArithFromReg(r.reg), 0, v, true, false);
            return;
        }
        case 0xD9:
            switch (r.reg) {
                case 0: { double v = LoadMem(a, 0); if (!x_.m.Fault().pending) { SetC1(false); Push(v); } return; }
                case 2: case 3: {
                    double v;
                    if (!Read(0, v) && Signal(0)) return;
                    StoreMem(a, 0, v);
                    if (r.reg == 3 && !x_.m.Fault().pending) Pop();
                    return;
                }
                case 4: FpuRestore(x_, a, false); return; // FLDENV
                case 5: c_.fpuControl = static_cast<uint16_t>(x_.m.Read16(a) | 0x40); return;
                case 6: { // FNSTENV
                    FpuSave(x_, a, false);
                    c_.fpuControl |= 0x3F; // маскирует все исключения, как железо
                    return;
                }
                case 7: x_.m.Write16(a, c_.fpuControl); return;
                default: x_.Raise(EXC_ILLEGAL_INSTRUCTION); return;
            }
        case 0xDB:
            switch (r.reg) {
                case 0: { double v = LoadInt(a, 1); if (!x_.m.Fault().pending) { SetC1(false); Push(v); } return; }
                case 1: case 2: case 3: {
                    double v;
                    Read(0, v);
                    StoreInt(a, 1, v, r.reg == 1);
                    if (r.reg != 2 && !x_.m.Fault().pending) Pop();
                    return;
                }
                case 5: { double v = LoadMem(a, 2); if (!x_.m.Fault().pending) { SetC1(false); Push(v); } return; }
                case 7: {
                    double v;
                    Read(0, v);
                    StoreMem(a, 2, v);
                    if (!x_.m.Fault().pending) Pop();
                    return;
                }
                default: x_.Raise(EXC_ILLEGAL_INSTRUCTION); return;
            }
        case 0xDD:
            switch (r.reg) {
                case 0: { double v = LoadMem(a, 1); if (!x_.m.Fault().pending) { SetC1(false); Push(v); } return; }
                case 1: { double v; Read(0, v); StoreInt(a, 2, v, true); if (!x_.m.Fault().pending) Pop(); return; }
                case 2: case 3: {
                    double v;
                    Read(0, v);
                    StoreMem(a, 1, v);
                    if (r.reg == 3 && !x_.m.Fault().pending) Pop();
                    return;
                }
                case 4: FpuRestore(x_, a, false); { // FRSTOR: окружение + 8 регистров
                    for (int i = 0; i < 8; ++i) {
                        uint8_t b[10];
                        for (int k = 0; k < 10; ++k) b[k] = x_.m.Read8(a + 28 + i * 10 + k);
                        c_.st[Phys(i)] = fp::Ext80ToDouble(b);
                    }
                    return;
                }
                case 6: { // FNSAVE
                    FpuSave(x_, a, false);
                    for (int i = 0; i < 8; ++i) {
                        uint8_t b[10];
                        fp::DoubleToExt80(c_.st[Phys(i)], b);
                        for (int k = 0; k < 10; ++k) x_.m.Write8(a + 28 + i * 10 + k, b[k]);
                    }
                    if (x_.m.Fault().pending) return;
                    c_.fpuControl = 0x037F; c_.fpuStatus = 0; c_.fpuTag = 0xFFFF; c_.fpuTop = 0; // FNINIT
                    return;
                }
                case 7: x_.m.Write16(a, c_.GetFpuStatusWord()); return;
                default: x_.Raise(EXC_ILLEGAL_INSTRUCTION); return;
            }
        case 0xDF:
            switch (r.reg) {
                case 0: { double v = LoadInt(a, 0); if (!x_.m.Fault().pending) { SetC1(false); Push(v); } return; }
                case 1: case 2: case 3: {
                    double v;
                    Read(0, v);
                    StoreInt(a, 0, v, r.reg == 1);
                    if (r.reg != 2 && !x_.m.Fault().pending) Pop();
                    return;
                }
                case 4: { // FBLD: 18 BCD-цифр + знак
                    double v = 0;
                    for (int i = 8; i >= 0; --i) {
                        uint8_t b = x_.m.Read8(a + i);
                        v = v * 100 + (b >> 4) * 10 + (b & 0xF);
                    }
                    if (x_.m.Read8(a + 9) & 0x80) v = -v;
                    if (!x_.m.Fault().pending) Push(v);
                    return;
                }
                case 5: { double v = LoadInt(a, 2); if (!x_.m.Fault().pending) { SetC1(false); Push(v); } return; }
                case 6: { // FBSTP
                    double v;
                    Read(0, v);
                    double t;
                    {
                        fp::Scope s(RC());
                        t = std::nearbyint(std::fabs(v));
                    }
                    if (std::isnan(v) || t >= 1e18) {
                        if (Signal(SW_IE)) return;
                        uint8_t ind[10] = {0, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF, 0xFF};
                        for (int k = 0; k < 10; ++k) x_.m.Write8(a + k, ind[k]);
                    } else {
                        uint64_t n = static_cast<uint64_t>(t);
                        for (int i = 0; i < 9; ++i) {
                            uint8_t lo = n % 10; n /= 10;
                            uint8_t hi = n % 10; n /= 10;
                            x_.m.Write8(a + i, static_cast<uint8_t>((hi << 4) | lo));
                        }
                        x_.m.Write8(a + 9, std::signbit(v) ? 0x80 : 0);
                    }
                    if (!x_.m.Fault().pending) Pop();
                    return;
                }
                case 7: { double v; Read(0, v); StoreInt(a, 2, v, false); if (!x_.m.Fault().pending) Pop(); return; }
            }
            return;
    }
}

void Fpu::ExecuteReg(uint8_t op, const ModRM& r) {
    int i = r.rm;
    switch (op) {
        case 0xD8: {
            double v;
            bool ok = !Empty(i);
            v = ok ? ST(i) : 0.0;
            if (r.reg == 2 || r.reg == 3) Compare(v, ok, true, r.reg == 3 ? 1 : 0);
            else ArithTo(ArithFromReg(r.reg), 0, v, ok, false);
            return;
        }
        case 0xDC: case 0xDE: {
            bool pop = op == 0xDE;
            if (r.reg == 2 || r.reg == 3) { // FCOM2/FCOMP3/FCOMP5 (псевдонимы) и FCOMPP
                if (op == 0xDE && r.reg == 3) {
                    if (i != 1) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return; }
                    bool ok = !Empty(1);
                    Compare(ok ? ST(1) : 0.0, ok, true, 2);
                    return;
                }
                bool ok = !Empty(i);
                Compare(ok ? ST(i) : 0.0, ok, true, (r.reg == 3 || op == 0xDE) ? 1 : 0);
                return;
            }
            bool ok = !Empty(0);
            ArithTo(ArithFromRegReversed(r.reg), i, ok ? ST(0) : 0.0, ok, pop);
            return;
        }
        case 0xD9:
            switch (r.reg) {
                case 0: { // FLD ST(i)
                    double v;
                    Read(i, v);
                    SetC1(false);
                    Push(v);
                    return;
                }
                case 1: { // FXCH
                    double a, b;
                    bool oka = Read(0, a), okb = Read(i, b);
                    SetC1(false);
                    if (!oka || !okb) { if (c_.fpuControl & SW_IE) { SetST(0, b); SetST(i, a); } return; }
                    SetST(0, b);
                    SetST(i, a);
                    return;
                }
                case 2: if (i == 0) return; x_.Raise(EXC_ILLEGAL_INSTRUCTION); return; // FNOP
                case 3: { double v; Read(0, v); SetST(i, v); Pop(); return; } // FSTP1
                case 4:
                    switch (i) {
                        case 0: { double v; if (Read(0, v)) SetST(0, -v); else SetST(0, v); SetC1(false); return; } // FCHS
                        case 1: { double v; if (Read(0, v)) SetST(0, std::fabs(v)); else SetST(0, v); SetC1(false); return; } // FABS
                        case 4: { // FTST
                            double v;
                            if (!Read(0, v)) { SetCC(true, false, true, true); return; }
                            CompareFlagsIE(v, 0.0, true);
                            SetCompareCC(Compare(v, 0.0));
                            return;
                        }
                        case 5: { // FXAM
                            double v = ST(0);
                            bool sign = std::signbit(v);
                            if (Empty(0)) SetCC(true, sign, false, true);
                            else if (std::isnan(v)) SetCC(true, sign, false, false);
                            else if (std::isinf(v)) SetCC(true, sign, true, false);
                            else if (v == 0.0) SetCC(false, sign, false, true);
                            else SetCC(false, sign, true, false);
                            return;
                        }
                        default: x_.Raise(EXC_ILLEGAL_INSTRUCTION); return;
                    }
                case 5: { // константы: точное значение, округлённое по RC
                    // {вниз, вверх} — соседние double вокруг точного значения
                    static const uint64_t k[7][2] = {
                        {0x3FF0000000000000ull, 0x3FF0000000000000ull}, // 1
                        {0x400A934F0979A371ull, 0x400A934F0979A372ull}, // log2(10)
                        {0x3FF71547652B82FEull, 0x3FF71547652B82FFull}, // log2(e)
                        {0x400921FB54442D18ull, 0x400921FB54442D19ull}, // pi
                        {0x3FD34413509F79FEull, 0x3FD34413509F79FFull}, // log10(2)
                        {0x3FE62E42FEFA39EFull, 0x3FE62E42FEFA39F0ull}, // ln(2)
                        {0, 0},                                         // 0
                    };
                    // ближайший double: pi, log2(10), log2(e), ln2 — нижний, log10(2) — верхний
                    static const int nearestIsHigh[7] = {0, 0, 0, 0, 1, 0, 0};
                    if (i == 7) { x_.Raise(EXC_ILLEGAL_INSTRUCTION); return; }
                    uint32_t rc = RC();
                    int pick = rc == 0 ? nearestIsHigh[i] : (rc == 2 ? 1 : 0); // константы положительные: вниз == к нулю
                    SetC1(false);
                    Push(fp::F64(k[i][pick]));
                    return;
                }
                default: // 6, 7: F2XM1..FCOS
                    Transcendental(static_cast<uint8_t>(((r.reg - 6) << 3) | i));
                    return;
            }
        case 0xDA:
            if (r.reg < 4) { // FCMOVB/E/BE/U
                bool cond;
                switch (r.reg) {
                    case 0: cond = c_.CF(); break;
                    case 1: cond = c_.ZF(); break;
                    case 2: cond = c_.CF() || c_.ZF(); break;
                    default: cond = c_.PF(); break;
                }
                double v;
                if (Read(i, v) && cond) SetST(0, v);
                SetC1(false);
                return;
            }
            if (r.reg == 5 && i == 1) { // FUCOMPP
                bool ok = !Empty(1);
                double a;
                bool oka = Read(0, a);
                if (!ok) { SetC1(false); c_.fpuStatus |= SW_SF; Signal(SW_IE); }
                if (!ok || !oka) SetCC(true, false, true, true);
                else { CompareFlagsIE(a, ST(1), false); SetCompareCC(Compare(a, ST(1))); }
                Pop(); Pop();
                return;
            }
            x_.Raise(EXC_ILLEGAL_INSTRUCTION);
            return;
        case 0xDB:
            if (r.reg < 4) { // FCMOVNB/NE/NBE/NU
                bool cond;
                switch (r.reg) {
                    case 0: cond = !c_.CF(); break;
                    case 1: cond = !c_.ZF(); break;
                    case 2: cond = !c_.CF() && !c_.ZF(); break;
                    default: cond = !c_.PF(); break;
                }
                double v;
                if (Read(i, v) && cond) SetST(0, v);
                SetC1(false);
                return;
            }
            if (r.reg == 4) {
                if (i == 2) { c_.fpuStatus &= static_cast<uint16_t>(~(0x3F | SW_SF | SW_ES | SW_B)); return; } // FNCLEX
                if (i == 3) { c_.fpuControl = 0x037F; c_.fpuStatus = 0; c_.fpuTag = 0xFFFF; c_.fpuTop = 0;
                              c_.fpuLastIp = c_.fpuLastDp = 0; c_.fpuLastOpcode = 0; return; } // FNINIT
                if (i == 0 || i == 1 || i == 4) return; // FENI/FDISI/FSETPM — ничего
                x_.Raise(EXC_ILLEGAL_INSTRUCTION);
                return;
            }
            if (r.reg == 5 || r.reg == 6) { // FUCOMI / FCOMI
                double a, b;
                bool oka = Read(0, a), okb = Read(i, b);
                SetC1(false);
                if (!oka || !okb) { SetCompareEflags(3); return; }
                CompareFlagsIE(a, b, r.reg == 6);
                SetCompareEflags(Compare(a, b));
                return;
            }
            x_.Raise(EXC_ILLEGAL_INSTRUCTION);
            return;
        case 0xDD:
            switch (r.reg) {
                case 0: SetTag(Phys(i), 3); return;                     // FFREE
                case 1: { double a, b; Read(0, a); Read(i, b); SetST(0, b); SetST(i, a); SetC1(false); return; } // FXCH4
                case 2: case 3: { // FST/FSTP ST(i)
                    double v;
                    Read(0, v);
                    SetST(i, v);
                    if (r.reg == 3) Pop();
                    return;
                }
                case 4: case 5: { // FUCOM/FUCOMP
                    bool ok = !Empty(i);
                    double a;
                    bool oka = Read(0, a);
                    if (!ok) { SetC1(false); c_.fpuStatus |= SW_SF; Signal(SW_IE); }
                    if (!ok || !oka) SetCC(true, false, true, true);
                    else { CompareFlagsIE(a, ST(i), false); SetCompareCC(Compare(a, ST(i))); }
                    if (r.reg == 5) Pop();
                    return;
                }
                default: x_.Raise(EXC_ILLEGAL_INSTRUCTION); return;
            }
        case 0xDF:
            switch (r.reg) {
                case 0: SetTag(Phys(i), 3); Pop(); return;                         // FFREEP
                case 4: if (i == 0) { x_.SetReg(0, 16, c_.GetFpuStatusWord()); return; } // FNSTSW AX
                        x_.Raise(EXC_ILLEGAL_INSTRUCTION); return;
                case 5: case 6: { // FUCOMIP / FCOMIP
                    double a, b;
                    bool oka = Read(0, a), okb = Read(i, b);
                    SetC1(false);
                    if (!oka || !okb) SetCompareEflags(3);
                    else { CompareFlagsIE(a, b, r.reg == 6); SetCompareEflags(Compare(a, b)); }
                    Pop();
                    return;
                }
                case 1: { double a, b; Read(0, a); Read(i, b); SetST(0, b); SetST(i, a); return; } // FXCH7
                case 2: case 3: { double v; Read(0, v); SetST(i, v); Pop(); return; }               // FSTP8/9
                default: x_.Raise(EXC_ILLEGAL_INSTRUCTION); return;
            }
    }
}

void Fpu::Transcendental(uint8_t code) {
    // code = (reg-6)*8 + rm: 0 F2XM1 1 FYL2X 2 FPTAN 3 FPATAN 4 FXTRACT 5 FPREM1 6 FDECSTP 7 FINCSTP
    //                        8 FPREM 9 FYL2XP1 10 FSQRT 11 FSINCOS 12 FRNDINT 13 FSCALE 14 FSIN 15 FCOS
    if (code == 6) { c_.fpuTop = static_cast<uint8_t>((c_.fpuTop - 1) & 7); SetC1(false); return; }
    if (code == 7) { c_.fpuTop = static_cast<uint8_t>((c_.fpuTop + 1) & 7); SetC1(false); return; }

    double a;
    if (!Read(0, a)) return;
    uint32_t flags = 0;
    SetC1(false);
    auto finish = [&](double v) {
        if (std::isnan(v) && !std::isnan(a)) { v = fp::F64(fp::kIndefinite64); flags |= SW_IE; }
        if (std::isnan(a)) v = fp::Quiet(a);
        if (Signal(flags)) return false;
        SetST(0, Round(v));
        return true;
    };
    switch (code) {
        case 0: finish(std::isnan(a) ? fp::Quiet(a) : std::exp2(a) - 1.0); return;
        case 1: case 9: { // FYL2X / FYL2XP1: ST1 = ST1 * log2(ST0 [+1]); pop
            double b;
            if (!Read(1, b)) return;
            double r;
            if (std::isnan(a) || std::isnan(b)) r = fp::Quiet(std::isnan(a) ? a : b);
            else {
                double l = code == 1 ? std::log2(a) : std::log1p(a) / std::log(2.0);
                if (code == 1 && a == 0.0) flags |= SW_ZE;
                r = b * l;
                if (std::isnan(r)) { r = fp::F64(fp::kIndefinite64); flags |= SW_IE; }
            }
            if (Signal(flags)) return;
            SetST(1, Round(r));
            Pop();
            return;
        }
        case 2: { // FPTAN
            if (std::fabs(a) >= 9223372036854775808.0) { c_.fpuStatus |= SW_C2; return; }
            c_.fpuStatus &= static_cast<uint16_t>(~SW_C2);
            if (!finish(std::tan(a))) return;
            Push(1.0);
            return;
        }
        case 3: { // FPATAN: ST1 = atan2(ST1, ST0); pop
            double b;
            if (!Read(1, b)) return;
            SetST(1, Round(std::atan2(b, a)));
            Pop();
            return;
        }
        case 4: { // FXTRACT: ST0 = мантисса, push порядок
            if (a == 0.0) {
                flags |= SW_ZE;
                if (Signal(flags)) return;
                SetST(0, -INFINITY);
                Push(a);
                return;
            }
            int e;
            double m = std::frexp(a, &e); // m в [0.5, 1)
            if (std::isinf(a)) { SetST(0, INFINITY); Push(a); return; }
            SetST(0, static_cast<double>(e - 1));
            Push(m * 2.0);
            return;
        }
        case 5: case 8: { // FPREM1 / FPREM
            double b;
            if (!Read(1, b)) return;
            double r;
            if (NaNResult(a, b, r, flags)) {
                if (Signal(flags)) return;
                SetST(0, r);
                SetCC(false, false, false, false);
                return;
            }
            if (std::isinf(a) || b == 0.0) {
                if (Signal(SW_IE)) return;
                SetST(0, fp::F64(fp::kIndefinite64));
                SetCC(false, false, false, false);
                return;
            }
            if (std::isinf(b) || a == 0.0) { SetCC(false, false, false, false); return; } // ST0 не меняется
            int ea, eb;
            std::frexp(a, &ea);
            std::frexp(b, &eb);
            int d = ea - eb;
            if (d >= 64) {
                // Частичный остаток. Число N у Intel — 32 + D mod 32 (проверено на железе).
                int n = 32 + d % 32;
                SetST(0, std::fmod(a, std::ldexp(b, d - n)));
                SetCC(false, false, true, false);
                return;
            }
            double A = std::fabs(a), B = std::fabs(b);
            double rem = code == 8 ? std::fmod(a, b) : std::remainder(a, b);
            // Младшие 3 бита частного точно: через остаток от деления на 8*B
            double remA = code == 8 ? std::fmod(A, B) : std::remainder(A, B);
            double r8 = std::isinf(8 * B) ? A : std::fmod(A, 8 * B);
            int k = static_cast<int>(std::nearbyint((r8 - remA) / B));
            if (std::isinf(8 * B)) k = static_cast<int>(std::nearbyint((A - remA) / B));
            uint32_t qi = static_cast<uint32_t>(k) & 7;
            SetST(0, rem);
            SetCC((qi & 4) != 0, (qi & 1) != 0, false, (qi & 2) != 0);
            return;
        }
        case 10: { // FSQRT
            double r;
            if (std::isnan(a)) r = fp::Quiet(a);
            else {
                fp::Scope s(RC());
                r = std::sqrt(a);
                flags |= s.Flags() & (fp::kIE | fp::kPE);
                if (std::isnan(r)) { r = fp::F64(fp::kIndefinite64); flags |= SW_IE; }
            }
            if (Signal(flags)) return;
            SetST(0, Round(r));
            return;
        }
        case 11: { // FSINCOS: ST0 = sin, push cos
            if (std::fabs(a) >= 9223372036854775808.0) { c_.fpuStatus |= SW_C2; return; }
            c_.fpuStatus &= static_cast<uint16_t>(~SW_C2);
            double s = std::sin(a), co = std::cos(a);
            if (!finish(s)) return;
            Push(Round(co));
            return;
        }
        case 12: { // FRNDINT
            double r;
            {
                fp::Scope s(RC());
                r = std::nearbyint(a);
            }
            if (r != a) flags |= SW_PE;
            if (Signal(flags)) return;
            SetST(0, r);
            return;
        }
        case 13: { // FSCALE: ST0 *= 2^trunc(ST1)
            double b;
            if (!Read(1, b)) return;
            double r;
            if (NaNResult(a, b, r, flags)) {
                if (Signal(flags)) return;
                SetST(0, r);
                return;
            }
            if ((std::isinf(b) && ((b < 0 && std::isinf(a)) || (b > 0 && a == 0.0)))) {
                flags |= SW_IE; // inf * 2^-inf, 0 * 2^inf
                r = fp::F64(fp::kIndefinite64);
            } else if (std::isinf(b)) {
                r = b > 0 ? std::copysign(INFINITY, a) : std::copysign(0.0, a); // точный результат, без округления
            } else {
                double t = std::trunc(b);
                if (t > 100000) t = 100000;
                if (t < -100000) t = -100000;
                fp::Scope s(RC());
                r = std::scalbn(a, static_cast<int>(t));
                flags |= s.Flags() & (fp::kOE | fp::kUE | fp::kPE);
            }
            if (Signal(flags)) return;
            SetST(0, r);
            return;
        }
        case 14: case 15: { // FSIN / FCOS
            if (std::fabs(a) >= 9223372036854775808.0) { c_.fpuStatus |= SW_C2; return; }
            c_.fpuStatus &= static_cast<uint16_t>(~SW_C2);
            finish(code == 14 ? std::sin(a) : std::cos(a));
            return;
        }
    }
}

uint16_t FullTagWord(const CpuState& c) {
    // Полный тег (2 бита на регистр) пересчитывается по значениям
    uint16_t t = 0;
    for (int p = 0; p < 8; ++p) {
        int tag = (c.fpuTag >> (p * 2)) & 3;
        if (tag != 3) tag = Fpu::Classify(c.st[p]);
        t = static_cast<uint16_t>(t | (tag << (p * 2)));
    }
    return t;
}

} // namespace

void ExecuteFpu(ExecContext& x) {
    Fpu f(x);
    f.Execute();
}

// FNSTENV/FLDENV (fxsave=false, 28 байт окружения) и FXSAVE/FXRSTOR (512 байт).
void FpuSave(ExecContext& x, uint32_t a, bool fxsave) {
    CpuState& c = x.c;
    if (!fxsave) {
        if (!x.m.IsRangeMapped(a, 28)) { x.m.Write8(a, 0); return; }
        x.m.Write32(a + 0, 0xFFFF0000u | c.fpuControl);
        x.m.Write32(a + 4, 0xFFFF0000u | c.GetFpuStatusWord());
        x.m.Write32(a + 8, 0xFFFF0000u | FullTagWord(c));
        x.m.Write32(a + 12, c.fpuLastIp);
        x.m.Write32(a + 16, (static_cast<uint32_t>(c.fpuLastOpcode & 0x7FF) << 16) | c.segSel[SEG_CS]);
        x.m.Write32(a + 20, c.fpuLastDp);
        x.m.Write32(a + 24, 0xFFFF0000u | c.segSel[SEG_DS]);
        return;
    }
    if (!x.m.IsRangeMapped(a, 512)) { x.m.Write8(a, 0); return; }
    if (a & 15) { x.Raise(EXC_ACCESS_VIOLATION); x.exc.faultAddress = 0xFFFFFFFFu; return; } // #GP: выравнивание
    uint8_t abridged = 0;
    for (int p = 0; p < 8; ++p) if (((c.fpuTag >> (p * 2)) & 3) != 3) abridged |= static_cast<uint8_t>(1 << p);
    x.m.Write16(a + 0, c.fpuControl);
    x.m.Write16(a + 2, c.GetFpuStatusWord());
    x.m.Write8(a + 4, abridged);
    x.m.Write8(a + 5, 0);
    x.m.Write16(a + 6, c.fpuLastOpcode & 0x7FF);
    x.m.Write32(a + 8, c.fpuLastIp);
    x.m.Write32(a + 12, c.segSel[SEG_CS]);
    x.m.Write32(a + 16, c.fpuLastDp);
    x.m.Write32(a + 20, c.segSel[SEG_DS]);
    x.m.Write32(a + 24, c.mxcsr);
    x.m.Write32(a + 28, 0x0000FFFFu); // MXCSR_MASK
    for (int i = 0; i < 8; ++i) {
        uint8_t b[10];
        fp::DoubleToExt80(c.st[(c.fpuTop + i) & 7], b);
        for (int k = 0; k < 10; ++k) x.m.Write8(a + 32 + i * 16 + k, b[k]);
        for (int k = 10; k < 16; ++k) x.m.Write8(a + 32 + i * 16 + k, 0);
    }
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 4; ++k) x.m.Write32(a + 160 + i * 16 + k * 4, c.xmm[i].d[k]);
}

void FpuRestore(ExecContext& x, uint32_t a, bool fxsave) {
    CpuState& c = x.c;
    if (!fxsave) {
        uint16_t cw = x.m.Read16(a + 0);
        uint16_t sw = x.m.Read16(a + 4);
        uint16_t tw = x.m.Read16(a + 8);
        if (x.m.Fault().pending) return;
        c.fpuControl = cw;
        c.fpuStatus = static_cast<uint16_t>(sw & ~0x3800u);
        c.fpuTop = static_cast<uint8_t>((sw >> 11) & 7);
        // теги «пусто/не пусто» берём как есть, остальное пересчитываем по значению
        c.fpuTag = tw;
        c.fpuLastIp = x.m.Read32(a + 12);
        c.fpuLastOpcode = static_cast<uint16_t>((x.m.Read32(a + 16) >> 16) & 0x7FF);
        c.fpuLastDp = x.m.Read32(a + 20);
        return;
    }
    if (a & 15) { x.Raise(EXC_ACCESS_VIOLATION); x.exc.faultAddress = 0xFFFFFFFFu; return; }
    uint16_t cw = x.m.Read16(a + 0);
    uint16_t sw = x.m.Read16(a + 2);
    uint8_t abridged = x.m.Read8(a + 4);
    uint32_t mx = x.m.Read32(a + 24);
    if (x.m.Fault().pending) return;
    c.fpuControl = cw;
    c.fpuStatus = static_cast<uint16_t>(sw & ~0x3800u);
    c.fpuTop = static_cast<uint8_t>((sw >> 11) & 7);
    c.fpuLastOpcode = x.m.Read16(a + 6) & 0x7FF;
    c.fpuLastIp = x.m.Read32(a + 8);
    c.fpuLastDp = x.m.Read32(a + 16);
    c.mxcsr = mx & 0xFFFF;
    for (int i = 0; i < 8; ++i) {
        uint8_t b[10];
        for (int k = 0; k < 10; ++k) b[k] = x.m.Read8(a + 32 + i * 16 + k);
        c.st[(c.fpuTop + i) & 7] = fp::Ext80ToDouble(b);
    }
    c.fpuTag = 0;
    for (int p = 0; p < 8; ++p) {
        int tag = (abridged & (1 << p)) ? Fpu::Classify(c.st[p]) : 3;
        c.fpuTag = static_cast<uint16_t>(c.fpuTag | (tag << (p * 2)));
    }
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 4; ++k) c.xmm[i].d[k] = x.m.Read32(a + 160 + i * 16 + k * 4);
}

} // namespace detail
} // namespace cpu

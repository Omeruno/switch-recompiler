// interpreter.cpp
//
// Целочисленная часть IA-32: однобайтовая карта опкодов и 0F-карта
// (кроме MMX/SSE — sse.cpp, и x87 — fpu.cpp).
//
// Флаги, которые Intel объявляет «неопределёнными», здесь выставляются как
// получится; сверка с железом (tests/cpu_oracle) их маскирует.

#include "interpreter.h"
#include "exec_context.h"

#include <cstdio>

namespace cpu {

using detail::ExecContext;
using detail::ModRM;

namespace {

uint64_t g_timeStampCounter = 0;

inline uint32_t Mask(uint8_t w) { return CpuState::Mask(w); }
inline uint32_t SignBit(uint8_t w) { return CpuState::SignBit(w); }
inline uint32_t SignExtend(uint32_t v, uint8_t w) {
    if (w == 8) return static_cast<uint32_t>(static_cast<int8_t>(v));
    if (w == 16) return static_cast<uint32_t>(static_cast<int16_t>(v));
    return v;
}

// Выставляет ZF/SF/PF по результату (остальные арифметические флаги
// передаются явно). Используется операциями с «ручными» флагами.
void SetResultFlags(CpuState& c, uint32_t res, uint8_t w, bool cf, bool of, bool af = false) {
    uint32_t f = c.eflags & ~FLAGS_ARITH;
    res &= Mask(w);
    if (cf) f |= FLAG_CF;
    if (of) f |= FLAG_OF;
    if (af) f |= FLAG_AF;
    if (res == 0) f |= FLAG_ZF;
    if (res & SignBit(w)) f |= FLAG_SF;
    if (Parity8(res)) f |= FLAG_PF;
    c.eflags = f;
    c.lazyOp = LazyOp::None;
}

// --- ALU: ADD OR ADC SBB AND SUB XOR CMP (номер = поле reg группы 1) ---
uint32_t Alu(CpuState& c, int op, uint32_t a, uint32_t b, uint8_t w) {
    uint32_t m = Mask(w);
    a &= m;
    b &= m;
    uint32_t r;
    switch (op) {
        case 0: r = a + b; c.SetLazy(LazyOp::Add, w, a, b, r); break;
        case 1: r = a | b; c.SetLazy(LazyOp::Logic, w, 0, 0, r); break;
        case 2: {
            uint8_t cin = c.CF() ? 1 : 0;
            r = a + b + cin;
            c.SetLazy(LazyOp::Adc, w, a, b, r);
            c.lazyCarryIn = cin;
            break;
        }
        case 3: {
            uint8_t cin = c.CF() ? 1 : 0;
            r = a - b - cin;
            c.SetLazy(LazyOp::Sbb, w, a, b, r);
            c.lazyCarryIn = cin;
            break;
        }
        case 4: r = a & b; c.SetLazy(LazyOp::Logic, w, 0, 0, r); break;
        case 5:
        case 7: r = a - b; c.SetLazy(LazyOp::Sub, w, a, b, r); break;
        default: r = a ^ b; c.SetLazy(LazyOp::Logic, w, 0, 0, r); break;
    }
    return r & m;
}

// --- Сдвиги/вращения группы 2: ROL ROR RCL RCR SHL SHR SAL SAR ---
uint32_t Shift(CpuState& c, int op, uint32_t v, uint32_t count, uint8_t w) {
    uint32_t m = Mask(w), s = SignBit(w);
    v &= m;
    count &= 31;
    if (op == 2 || op == 3) {          // RCL/RCR вращают w+1 бит
        if (w == 8) count %= 9;
        else if (w == 16) count %= 17;
    }
    if (count == 0) return v;           // флаги не меняются вовсе
    c.MaterializeFlags();
    bool cf = c.CF(), of = (c.eflags & FLAG_OF) != 0;
    uint32_t r = v;
    switch (op) {
        case 0: { // ROL
            uint32_t n = count % w;
            r = n ? ((v << n) | (v >> (w - n))) & m : v;
            cf = r & 1;
            of = ((r & s) != 0) != cf;
            c.eflags = (c.eflags & ~(FLAG_CF | FLAG_OF)) | (cf ? FLAG_CF : 0) | (of ? FLAG_OF : 0);
            return r;
        }
        case 1: { // ROR
            uint32_t n = count % w;
            r = n ? ((v >> n) | (v << (w - n))) & m : v;
            cf = (r & s) != 0;
            of = ((r & s) != 0) != ((r & (s >> 1)) != 0);
            c.eflags = (c.eflags & ~(FLAG_CF | FLAG_OF)) | (cf ? FLAG_CF : 0) | (of ? FLAG_OF : 0);
            return r;
        }
        case 2: { // RCL
            for (uint32_t i = 0; i < count; ++i) {
                bool out = (r & s) != 0;
                r = ((r << 1) | (cf ? 1 : 0)) & m;
                cf = out;
            }
            of = ((r & s) != 0) != cf;
            c.eflags = (c.eflags & ~(FLAG_CF | FLAG_OF)) | (cf ? FLAG_CF : 0) | (of ? FLAG_OF : 0);
            return r;
        }
        case 3: { // RCR
            of = ((v & s) != 0) != cf;
            for (uint32_t i = 0; i < count; ++i) {
                bool out = r & 1;
                r = (r >> 1) | (cf ? s : 0);
                cf = out;
            }
            c.eflags = (c.eflags & ~(FLAG_CF | FLAG_OF)) | (cf ? FLAG_CF : 0) | (of ? FLAG_OF : 0);
            return r;
        }
        case 4:
        case 6: { // SHL/SAL
            uint64_t wide = static_cast<uint64_t>(v) << count;
            r = static_cast<uint32_t>(wide) & m;
            cf = count <= w ? ((wide >> w) & 1) != 0 : false;
            of = ((r & s) != 0) != cf;
            SetResultFlags(c, r, w, cf, of);
            return r;
        }
        case 5: { // SHR
            cf = count <= w ? ((v >> (count - 1)) & 1) != 0 : false;
            r = count < w ? (v >> count) : 0;
            of = (v & s) != 0;
            SetResultFlags(c, r, w, cf, of);
            return r;
        }
        default: { // SAR
            int32_t sv = static_cast<int32_t>(SignExtend(v, w));
            uint32_t n = count < w ? count : w - 1;
            cf = count < w ? ((sv >> (count - 1)) & 1) != 0 : (sv < 0);
            r = static_cast<uint32_t>(sv >> n) & m;
            if (count >= w) r = sv < 0 ? m : 0;
            SetResultFlags(c, r, w, cf, false);
            return r;
        }
    }
}

// SHLD/SHRD: dst сдвигается, освободившиеся биты заполняются из src.
uint32_t DoubleShift(CpuState& c, bool left, uint32_t dst, uint32_t src, uint32_t count, uint8_t w) {
    count &= 31;
    if (count == 0) return dst & Mask(w);
    uint32_t m = Mask(w), s = SignBit(w);
    dst &= m;
    src &= m;
    uint32_t r;
    bool cf;
    if (w == 16) {
        // 16-битная форма: как 32-битный сдвиг склейки (поведение Intel при count > 16 не определено)
        uint32_t cat = left ? ((dst << 16) | src) : ((src << 16) | dst);
        if (left) {
            uint64_t wide = static_cast<uint64_t>(cat) << count;
            r = static_cast<uint32_t>(wide >> 16) & m;
            cf = count <= 32 ? ((cat >> (32 - count)) & 1) != 0 : false;
        } else {
            r = (cat >> count) & m;
            cf = ((cat >> (count - 1)) & 1) != 0;
        }
    } else if (left) {
        r = (dst << count) | (src >> (32 - count));
        cf = ((dst >> (32 - count)) & 1) != 0;
    } else {
        r = (dst >> count) | (src << (32 - count));
        cf = ((dst >> (count - 1)) & 1) != 0;
    }
    r &= m;
    bool of = ((r ^ dst) & s) != 0;
    SetResultFlags(c, r, w, cf, of);
    return r;
}

} // namespace

StepResult Interpreter::Step(CpuState& cpu, Instruction* outInstr) {
    // Снимок для отката при исключении (память не откатывается: сбойная
    // инструкция в x86 не успевает ничего записать, кроме REP-циклов,
    // которые ведут учёт сами).
    uint32_t savedRegs[8];
    std::memcpy(savedRegs, cpu.regs, sizeof(savedRegs));
    const uint32_t savedEip = cpu.eip;
    const uint32_t savedEflags = cpu.eflags;
    const LazyOp savedOp = cpu.lazyOp;
    const uint8_t savedW = cpu.lazyWidth, savedCin = cpu.lazyCarryIn;
    const uint32_t savedA = cpu.lazyA, savedB = cpu.lazyB, savedRes = cpu.lazyRes;

    memory_.ClearFault();
    ExecContext x(cpu, memory_, fsBase_, gsBase_);
    x.start = cpu.eip;
    x.code = memory_.RawPointer(cpu.eip);
    x.fetchLimit = memory_.FetchLimit(cpu.eip);
    g_lastKnownEip = cpu.eip;
    ++g_timeStampCounter;

    StepResult result = StepResult::Continue;
    CpuState& c = cpu;

    // --- Префиксы ---
    uint8_t op;
    for (;;) {
        op = x.Fetch8();
        if (x.fetchFault) break;
        if (op == 0x66) { x.osz = 16; x.opsizePrefix = true; }
        else if (op == 0x67) x.asz = 16;
        else if (op == 0xF2 || op == 0xF3) x.rep = op;
        else if (op == 0xF0) { /* LOCK: однопоточный гость, ничего не нужно */ }
        else if (op == 0x26) x.seg = SEG_ES;
        else if (op == 0x2E) x.seg = SEG_CS;
        else if (op == 0x36) x.seg = SEG_SS;
        else if (op == 0x3E) x.seg = SEG_DS;
        else if (op == 0x64) x.seg = SEG_FS;
        else if (op == 0x65) x.seg = SEG_GS;
        else break;
    }
    x.opcode = op;
    const uint8_t osz = x.osz;
    uint32_t next = 0; // новый EIP, если инструкция — переход (иначе 0)
    bool jumped = false;

    auto jumpTo = [&](uint32_t target) {
        if (osz == 16) target &= 0xFFFF;
        next = target;
        jumped = true;
    };

    if (x.fetchFault) goto done;

    // ===== Однобайтовая карта =====
    if (op < 0x40 && (op & 7) < 6) {
        // 00..3F: ALU в шести формах
        int aluOp = op >> 3;
        switch (op & 7) {
            case 0: case 1: case 2: case 3: {
                uint8_t w = (op & 1) ? osz : 8;
                ModRM r = x.DecodeModRM();
                if (x.fetchFault) break;
                if (op & 2) { // reg, r/m
                    uint32_t res = Alu(c, aluOp, x.GetReg(r.reg, w), x.ReadRM(r, w), w);
                    if (aluOp != 7) x.SetReg(r.reg, w, res);
                } else {      // r/m, reg
                    uint32_t a = x.ReadRM(r, w);
                    uint32_t res = Alu(c, aluOp, a, x.GetReg(r.reg, w), w);
                    if (aluOp != 7) x.WriteRM(r, w, res);
                }
                break;
            }
            case 4: {
                uint32_t imm = x.Fetch8();
                uint32_t res = Alu(c, aluOp, x.GetReg(0, 8), imm, 8);
                if (aluOp != 7) x.SetReg(0, 8, res);
                break;
            }
            case 5: {
                uint32_t imm = x.FetchImm(osz);
                uint32_t res = Alu(c, aluOp, x.GetReg(0, osz), imm, osz);
                if (aluOp != 7) x.SetReg(0, osz, res);
                break;
            }
        }
        goto done;
    }

    switch (op) {
        // --- PUSH/POP сегментов ---
        case 0x06: x.Push(c.segSel[SEG_ES], osz); break;
        case 0x0E: x.Push(c.segSel[SEG_CS], osz); break;
        case 0x16: x.Push(c.segSel[SEG_SS], osz); break;
        case 0x1E: x.Push(c.segSel[SEG_DS], osz); break;
        case 0x07: c.segSel[SEG_ES] = static_cast<uint16_t>(x.Pop(osz)); break;
        case 0x17: c.segSel[SEG_SS] = static_cast<uint16_t>(x.Pop(osz)); break;
        case 0x1F: c.segSel[SEG_DS] = static_cast<uint16_t>(x.Pop(osz)); break;

        // --- Десятичная коррекция ---
        case 0x27: case 0x2F: { // DAA / DAS (псевдокод из руководства Intel)
            uint32_t oldAl = c.regs[0] & 0xFF, al = oldAl;
            bool oldCf = c.CF(), af = c.AF();
            bool sub = op == 0x2F;
            bool cf = false, newAf = false;
            if ((al & 0x0F) > 9 || af) {
                cf = oldCf || (sub ? oldAl < 6 : oldAl + 6 > 0xFF);
                al = (sub ? al - 6 : al + 6) & 0xFF;
                newAf = true;
            }
            if (oldAl > 0x99 || oldCf) {
                al = (sub ? al - 0x60 : al + 0x60) & 0xFF;
                cf = true;
            } else if (!sub) {
                cf = false;
            }
            x.SetReg(0, 8, al);
            SetResultFlags(c, al, 8, cf, false, newAf);
            break;
        }
        case 0x37: case 0x3F: { // AAA / AAS
            uint32_t ax = c.regs[0] & 0xFFFF;
            bool adj = (ax & 0x0F) > 9 || c.AF();
            if (adj) {
                if (op == 0x37) ax = ax + 0x106;
                else ax = (ax - 6) - 0x100;
            }
            ax = (ax & 0xFF00) | (ax & 0x0F);
            c.regs[0] = (c.regs[0] & 0xFFFF0000u) | (ax & 0xFFFF);
            c.MaterializeFlags();
            c.eflags = (c.eflags & ~(FLAG_CF | FLAG_AF)) | (adj ? (FLAG_CF | FLAG_AF) : 0);
            break;
        }

        // --- INC/DEC r ---
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: {
            uint32_t a = x.GetReg(op & 7, osz);
            c.SetLazy(LazyOp::Inc, osz, a, 1, a + 1);
            x.SetReg(op & 7, osz, a + 1);
            break;
        }
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            uint32_t a = x.GetReg(op & 7, osz);
            c.SetLazy(LazyOp::Dec, osz, a, 1, a - 1);
            x.SetReg(op & 7, osz, a - 1);
            break;
        }

        // --- PUSH/POP r ---
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            x.Push(x.GetReg(op & 7, osz), osz);
            break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
            uint32_t v = x.Pop(osz);
            x.SetReg(op & 7, osz, v);
            break;
        }

        case 0x60: { // PUSHA/PUSHAD
            uint32_t sp = c.regs[4];
            for (int i = 0; i < 8; ++i) x.Push(i == 4 ? sp : x.GetReg(i, osz), osz);
            break;
        }
        case 0x61: { // POPA/POPAD (ESP пропускается)
            for (int i = 7; i >= 0; --i) {
                uint32_t v = x.Pop(osz);
                if (i != 4) x.SetReg(i, osz, v);
            }
            break;
        }
        case 0x62: { // BOUND
            ModRM r = x.DecodeModRM();
            if (r.isReg) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
            int32_t idx = static_cast<int32_t>(SignExtend(x.GetReg(r.reg, osz), osz));
            int32_t lo = static_cast<int32_t>(SignExtend(x.ReadMem(r.addr, osz), osz));
            int32_t hi = static_cast<int32_t>(SignExtend(x.ReadMem(r.addr + osz / 8, osz), osz));
            if (idx < lo || idx > hi) x.Raise(0xC000008Cu); // ARRAY_BOUNDS_EXCEEDED
            break;
        }

        case 0x68: x.Push(x.FetchImm(osz), osz); break;
        case 0x6A: x.Push(static_cast<uint32_t>(x.FetchSimm8()), osz); break;
        case 0x69: case 0x6B: { // IMUL r, r/m, imm
            ModRM r = x.DecodeModRM();
            int64_t a = static_cast<int32_t>(SignExtend(x.ReadRM(r, osz), osz));
            int64_t b = op == 0x6B ? x.FetchSimm8() : static_cast<int32_t>(SignExtend(x.FetchImm(osz), osz));
            int64_t p = a * b;
            uint32_t res = static_cast<uint32_t>(p) & Mask(osz);
            bool ov = static_cast<int64_t>(static_cast<int32_t>(SignExtend(res, osz))) != p;
            x.SetReg(r.reg, osz, res);
            SetResultFlags(c, res, osz, ov, ov);
            break;
        }

        // INS/OUTS, IN/OUT: привилегированные в пользовательском режиме Windows
        case 0x6C: case 0x6D: case 0x6E: case 0x6F:
        case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: case 0xEF:
        case 0xFA: case 0xFB: // CLI/STI
            x.Raise(EXC_PRIV_INSTRUCTION);
            break;

        // --- Jcc rel8 ---
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            int32_t rel = x.FetchSimm8();
            if (!x.fetchFault && x.Cond(op & 0xF)) jumpTo(x.NextEip() + rel);
            break;
        }

        // --- Группа 1: ALU r/m, imm ---
        case 0x80: case 0x81: case 0x82: case 0x83: {
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            uint32_t imm = (op == 0x81) ? x.FetchImm(osz) : (op == 0x83 ? static_cast<uint32_t>(x.FetchSimm8()) : x.Fetch8());
            if (x.fetchFault) break;
            uint32_t a = x.ReadRM(r, w);
            uint32_t res = Alu(c, r.reg, a, imm, w);
            if (r.reg != 7) x.WriteRM(r, w, res);
            break;
        }

        case 0x84: case 0x85: { // TEST r/m, reg
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            uint32_t res = x.ReadRM(r, w) & x.GetReg(r.reg, w);
            c.SetLazy(LazyOp::Logic, w, 0, 0, res);
            break;
        }
        case 0x86: case 0x87: { // XCHG r/m, reg
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            uint32_t a = x.ReadRM(r, w);
            uint32_t b = x.GetReg(r.reg, w);
            x.WriteRM(r, w, b);
            if (!memory_.Fault().pending) x.SetReg(r.reg, w, a);
            break;
        }
        case 0x88: case 0x89: { // MOV r/m, reg
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            if (x.fetchFault) break;
            x.WriteRM(r, w, x.GetReg(r.reg, w));
            break;
        }
        case 0x8A: case 0x8B: { // MOV reg, r/m
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            if (x.fetchFault) break;
            x.SetReg(r.reg, w, x.ReadRM(r, w));
            break;
        }
        case 0x8C: { // MOV r/m, Sreg
            ModRM r = x.DecodeModRM();
            if (r.reg > 5) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
            if (r.isReg) x.SetReg(r.rm, osz, c.segSel[r.reg]);
            else x.WriteMem(r.addr, 16, c.segSel[r.reg]);
            break;
        }
        case 0x8D: { // LEA
            ModRM r = x.DecodeModRM();
            if (x.fetchFault) break;
            if (r.isReg) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
            x.SetReg(r.reg, osz, r.ea);
            break;
        }
        case 0x8E: { // MOV Sreg, r/m
            ModRM r = x.DecodeModRM();
            if (r.reg > 5 || r.reg == SEG_CS) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
            uint32_t v = x.ReadRM(r, 16);
            c.segSel[r.reg] = static_cast<uint16_t>(v);
            break;
        }
        case 0x8F: { // POP r/m
            uint32_t v = x.Pop(osz);
            ModRM r = x.DecodeModRM(); // адрес считается с ESP уже после POP
            x.WriteRM(r, osz, v);
            break;
        }

        case 0x90: // NOP / PAUSE
            break;
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
            uint32_t a = x.GetReg(0, osz), b = x.GetReg(op & 7, osz);
            x.SetReg(0, osz, b);
            x.SetReg(op & 7, osz, a);
            break;
        }
        case 0x98: // CBW / CWDE
            if (osz == 16) x.SetReg(0, 16, SignExtend(c.regs[0] & 0xFF, 8));
            else c.regs[0] = SignExtend(c.regs[0] & 0xFFFF, 16);
            break;
        case 0x99: // CWD / CDQ
            if (osz == 16) x.SetReg(2, 16, (c.regs[0] & 0x8000) ? 0xFFFF : 0);
            else c.regs[2] = (c.regs[0] & 0x80000000u) ? 0xFFFFFFFFu : 0;
            break;
        case 0x9B: // FWAIT
            break;
        case 0x9C: // PUSHF(D): VM и RF не видны
            x.Push(c.GetEflags() & 0x00FCFFFFu, osz);
            break;
        case 0x9D: { // POPF(D)
            uint32_t v = x.Pop(osz);
            if (osz == 16) v = (c.GetEflags() & 0xFFFF0000u) | (v & 0xFFFF);
            // IOPL и IF пользовательский код поменять не может
            v = (v & ~(0x3000u | FLAG_IF)) | (c.eflags & (0x3000u | FLAG_IF));
            c.SetEflags(v);
            break;
        }
        case 0x9E: { // SAHF
            c.MaterializeFlags();
            uint32_t ah = (c.regs[0] >> 8) & 0xFF;
            uint32_t m = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF;
            c.eflags = (c.eflags & ~m) | (ah & m);
            break;
        }
        case 0x9F: { // LAHF
            uint32_t f = c.GetEflags();
            x.SetReg(4, 8, (f & 0xD5) | 0x02);
            break;
        }

        // --- MOV moffs ---
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
            uint8_t w = (op & 1) ? osz : 8;
            uint32_t off = x.asz == 16 ? x.Fetch16() : x.Fetch32();
            if (x.fetchFault) break;
            uint32_t a = off + x.DataSegBase();
            if (op < 0xA2) x.SetReg(0, w, x.ReadMem(a, w));
            else x.WriteMem(a, w, x.GetReg(0, w));
            break;
        }

        // --- Строковые инструкции ---
        case 0xA4: case 0xA5: case 0xA6: case 0xA7:
        case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
            uint8_t w = (op & 1) ? osz : 8;
            uint32_t step = w / 8;
            bool rep = x.rep != 0;
            bool isCmp = op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF;
            x.keepState = true; // учёт прогресса ведём сами
            uint32_t delta = c.DF() ? static_cast<uint32_t>(-static_cast<int32_t>(step)) : step;
            uint32_t srcBase = x.DataSegBase(SEG_DS), dstBase = x.SegBase(SEG_ES);
            auto addrReg = [&](int i) { return x.asz == 16 ? (c.regs[i] & 0xFFFF) : c.regs[i]; };
            auto advance = [&](int i) {
                if (x.asz == 16) c.regs[i] = (c.regs[i] & 0xFFFF0000u) | ((c.regs[i] + delta) & 0xFFFF);
                else c.regs[i] += delta;
            };
            while (!rep || x.Counter() != 0) {
                switch (op) {
                    case 0xA4: case 0xA5: { // MOVS
                        uint32_t v = x.ReadMem(addrReg(6) + srcBase, w);
                        if (memory_.Fault().pending) break;
                        x.WriteMem(addrReg(7) + dstBase, w, v);
                        if (memory_.Fault().pending) break;
                        advance(6); advance(7);
                        break;
                    }
                    case 0xA6: case 0xA7: { // CMPS
                        uint32_t a = x.ReadMem(addrReg(6) + srcBase, w);
                        uint32_t b = x.ReadMem(addrReg(7) + dstBase, w);
                        if (memory_.Fault().pending) break;
                        Alu(c, 7, a, b, w);
                        advance(6); advance(7);
                        break;
                    }
                    case 0xAA: case 0xAB: // STOS
                        x.WriteMem(addrReg(7) + dstBase, w, x.GetReg(0, w));
                        if (memory_.Fault().pending) break;
                        advance(7);
                        break;
                    case 0xAC: case 0xAD: { // LODS
                        uint32_t v = x.ReadMem(addrReg(6) + srcBase, w);
                        if (memory_.Fault().pending) break;
                        x.SetReg(0, w, v);
                        advance(6);
                        break;
                    }
                    default: { // SCAS
                        uint32_t b = x.ReadMem(addrReg(7) + dstBase, w);
                        if (memory_.Fault().pending) break;
                        Alu(c, 7, x.GetReg(0, w), b, w);
                        advance(7);
                        break;
                    }
                }
                if (memory_.Fault().pending) break;
                if (!rep) break;
                x.SetCounter(x.Counter() - 1);
                if (isCmp && ((x.rep == 0xF3 && !c.ZF()) || (x.rep == 0xF2 && c.ZF()))) break;
            }
            break;
        }
        case 0xA8: { // TEST AL, imm8
            uint32_t res = (c.regs[0] & 0xFF) & x.Fetch8();
            c.SetLazy(LazyOp::Logic, 8, 0, 0, res);
            break;
        }
        case 0xA9: {
            uint32_t res = x.GetReg(0, osz) & x.FetchImm(osz);
            c.SetLazy(LazyOp::Logic, osz, 0, 0, res);
            break;
        }

        // --- MOV reg, imm ---
        case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            x.SetReg(op & 7, 8, x.Fetch8());
            break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            x.SetReg(op & 7, osz, x.FetchImm(osz));
            break;

        // --- Группа 2: сдвиги ---
        case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            uint32_t count = (op <= 0xC1) ? x.Fetch8() : (op <= 0xD1 ? 1 : (c.regs[1] & 0xFF));
            if (x.fetchFault) break;
            uint32_t v = x.ReadRM(r, w);
            uint32_t res = Shift(c, r.reg, v, count, w);
            x.WriteRM(r, w, res);
            break;
        }

        // --- RET ---
        case 0xC2: {
            uint16_t n = x.Fetch16();
            if (x.fetchFault) break;
            uint32_t t = x.Pop(osz);
            c.regs[4] += n;
            jumpTo(t);
            break;
        }
        case 0xC3:
            jumpTo(x.Pop(osz));
            break;
        case 0xC4: case 0xC5: { // LES/LDS
            ModRM r = x.DecodeModRM();
            if (r.isReg) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
            uint32_t off = x.ReadMem(r.addr, osz);
            uint16_t sel = static_cast<uint16_t>(x.ReadMem(r.addr + osz / 8, 16));
            x.SetReg(r.reg, osz, off);
            c.segSel[op == 0xC4 ? SEG_ES : SEG_DS] = sel;
            break;
        }
        case 0xC6: case 0xC7: { // MOV r/m, imm
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            uint32_t imm = x.FetchImm(w);
            if (x.fetchFault) break;
            if (r.reg != 0) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
            x.WriteRM(r, w, imm);
            break;
        }
        case 0xC8: { // ENTER
            uint32_t size = x.Fetch16();
            uint32_t level = x.Fetch8() & 31;
            if (x.fetchFault) break;
            x.Push(x.GetReg(5, osz), osz);
            uint32_t frame = c.regs[4];
            for (uint32_t i = 1; i < level; ++i) {
                c.regs[5] -= osz / 8;
                x.Push(x.ReadMem(c.regs[5], osz), osz);
            }
            if (level > 0) x.Push(frame, osz);
            x.SetReg(5, osz, frame);
            c.regs[4] -= size;
            break;
        }
        case 0xC9: // LEAVE
            c.regs[4] = osz == 16 ? ((c.regs[4] & 0xFFFF0000u) | (c.regs[5] & 0xFFFF)) : c.regs[5];
            x.SetReg(5, osz, x.Pop(osz));
            break;
        case 0xCC: // INT3
            x.Raise(EXC_BREAKPOINT);
            break;
        case 0xCD: { // INT n
            uint8_t n = x.Fetch8();
            if (x.fetchFault) break;
            if (n == 3) x.Raise(EXC_BREAKPOINT);
            else { x.Raise(EXC_SOFTWARE_INTERRUPT); x.exc.vector = n; }
            break;
        }
        case 0xCE: // INTO
            if (c.OF()) x.Raise(EXC_INT_OVERFLOW);
            break;

        case 0xD4: { // AAM
            uint32_t base = x.Fetch8();
            if (x.fetchFault) break;
            if (base == 0) { x.Raise(EXC_INT_DIVIDE_BY_ZERO); break; }
            uint32_t al = c.regs[0] & 0xFF;
            uint32_t ax = ((al / base) << 8) | (al % base);
            c.regs[0] = (c.regs[0] & 0xFFFF0000u) | ax;
            SetResultFlags(c, ax & 0xFF, 8, false, false);
            break;
        }
        case 0xD5: { // AAD
            uint32_t base = x.Fetch8();
            if (x.fetchFault) break;
            uint32_t al = ((c.regs[0] & 0xFF) + ((c.regs[0] >> 8) & 0xFF) * base) & 0xFF;
            c.regs[0] = (c.regs[0] & 0xFFFF0000u) | al;
            SetResultFlags(c, al, 8, false, false);
            break;
        }
        case 0xD6: // SALC (недокументированная)
            x.SetReg(0, 8, c.CF() ? 0xFF : 0);
            break;
        case 0xD7: { // XLAT
            uint32_t base = x.asz == 16 ? (c.regs[3] & 0xFFFF) : c.regs[3];
            uint32_t a = base + (c.regs[0] & 0xFF);
            if (x.asz == 16) a &= 0xFFFF;
            x.SetReg(0, 8, x.ReadMem(a + x.DataSegBase(), 8));
            break;
        }

        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            detail::ExecuteFpu(x);
            break;

        // --- LOOP/JCXZ ---
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: {
            int32_t rel = x.FetchSimm8();
            if (x.fetchFault) break;
            bool take;
            if (op == 0xE3) {
                take = x.Counter() == 0;
            } else {
                x.SetCounter(x.Counter() - 1);
                take = x.Counter() != 0;
                if (op == 0xE0) take = take && !c.ZF();
                if (op == 0xE1) take = take && c.ZF();
            }
            if (take) jumpTo(x.NextEip() + rel);
            break;
        }

        case 0xE8: { // CALL rel
            uint32_t rel = x.FetchImm(osz);
            if (x.fetchFault) break;
            uint32_t ret = x.NextEip();
            x.Push(ret, osz);
            jumpTo(ret + (osz == 16 ? SignExtend(rel, 16) : rel));
            break;
        }
        case 0xE9: { // JMP rel
            uint32_t rel = x.FetchImm(osz);
            if (x.fetchFault) break;
            jumpTo(x.NextEip() + (osz == 16 ? SignExtend(rel, 16) : rel));
            break;
        }
        case 0xEB: {
            int32_t rel = x.FetchSimm8();
            if (x.fetchFault) break;
            jumpTo(x.NextEip() + rel);
            break;
        }

        case 0xF4: // HLT: наш маркер конца программы в тестах
            result = StepResult::Halted;
            break;
        case 0xF5: c.SetFlag(FLAG_CF, !c.CF()); break; // CMC
        case 0xF8: c.SetFlag(FLAG_CF, false); break;   // CLC
        case 0xF9: c.SetFlag(FLAG_CF, true); break;    // STC
        case 0xFC: c.eflags &= ~FLAG_DF; break;        // CLD (DF не арифметический — не ленивый)
        case 0xFD: c.eflags |= FLAG_DF; break;         // STD

        // --- Группа 3 ---
        case 0xF6: case 0xF7: {
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            if (x.fetchFault) break;
            switch (r.reg) {
                case 0: case 1: { // TEST r/m, imm
                    uint32_t imm = x.FetchImm(w);
                    if (x.fetchFault) break;
                    c.SetLazy(LazyOp::Logic, w, 0, 0, x.ReadRM(r, w) & imm);
                    break;
                }
                case 2: x.WriteRM(r, w, ~x.ReadRM(r, w)); break; // NOT
                case 3: { // NEG
                    uint32_t v = x.ReadRM(r, w);
                    uint32_t res = 0u - v;
                    c.SetLazy(LazyOp::Sub, w, 0, v, res);
                    x.WriteRM(r, w, res);
                    break;
                }
                case 4: { // MUL
                    uint32_t v = x.ReadRM(r, w);
                    bool hi;
                    if (w == 8) {
                        uint32_t p = (c.regs[0] & 0xFF) * v;
                        x.SetReg(0, 16, p);
                        hi = (p >> 8) != 0;
                        SetResultFlags(c, p, 8, hi, hi);
                    } else if (w == 16) {
                        uint32_t p = (c.regs[0] & 0xFFFF) * v;
                        x.SetReg(0, 16, p);
                        x.SetReg(2, 16, p >> 16);
                        hi = (p >> 16) != 0;
                        SetResultFlags(c, p, 16, hi, hi);
                    } else {
                        uint64_t p = static_cast<uint64_t>(c.regs[0]) * v;
                        c.regs[0] = static_cast<uint32_t>(p);
                        c.regs[2] = static_cast<uint32_t>(p >> 32);
                        hi = c.regs[2] != 0;
                        SetResultFlags(c, c.regs[0], 32, hi, hi);
                    }
                    break;
                }
                case 5: { // IMUL (однооперандный)
                    int64_t v = static_cast<int32_t>(SignExtend(x.ReadRM(r, w), w));
                    bool ov;
                    if (w == 8) {
                        int32_t p = static_cast<int8_t>(c.regs[0] & 0xFF) * static_cast<int32_t>(v);
                        x.SetReg(0, 16, static_cast<uint32_t>(p));
                        ov = p != static_cast<int8_t>(p);
                        SetResultFlags(c, static_cast<uint32_t>(p), 8, ov, ov);
                    } else if (w == 16) {
                        int32_t p = static_cast<int16_t>(c.regs[0] & 0xFFFF) * static_cast<int32_t>(v);
                        x.SetReg(0, 16, static_cast<uint32_t>(p));
                        x.SetReg(2, 16, static_cast<uint32_t>(p) >> 16);
                        ov = p != static_cast<int16_t>(p);
                        SetResultFlags(c, static_cast<uint32_t>(p), 16, ov, ov);
                    } else {
                        int64_t p = static_cast<int64_t>(static_cast<int32_t>(c.regs[0])) * v;
                        c.regs[0] = static_cast<uint32_t>(p);
                        c.regs[2] = static_cast<uint32_t>(static_cast<uint64_t>(p) >> 32);
                        ov = p != static_cast<int32_t>(p);
                        SetResultFlags(c, c.regs[0], 32, ov, ov);
                    }
                    break;
                }
                case 6: case 7: { // DIV / IDIV
                    bool isSigned = r.reg == 7;
                    uint32_t d = x.ReadRM(r, w);
                    if (memory_.Fault().pending) break;
                    if ((d & Mask(w)) == 0) {
                        if (lenientDivide) {
                            static int warned = 0;
                            if (g_consoleAvailable && warned++ < 5)
                                std::printf("[DIV] Деление на ноль на 0x%x (мягкий режим: результат 0)\n", x.start);
                            if (w == 8) c.regs[0] &= 0xFFFF0000u;
                            else { x.SetReg(0, w, 0); x.SetReg(2, w, 0); }
                        } else {
                            x.Raise(EXC_INT_DIVIDE_BY_ZERO);
                        }
                        break;
                    }
                    if (!isSigned) {
                        uint64_t n, q, rem;
                        if (w == 8) n = c.regs[0] & 0xFFFF;
                        else if (w == 16) n = ((c.regs[2] & 0xFFFF) << 16) | (c.regs[0] & 0xFFFF);
                        else n = (static_cast<uint64_t>(c.regs[2]) << 32) | c.regs[0];
                        q = n / d;
                        rem = n % d;
                        if (q > Mask(w)) { x.Raise(EXC_INT_OVERFLOW); break; }
                        if (w == 8) c.regs[0] = (c.regs[0] & 0xFFFF0000u) | static_cast<uint32_t>(q) | (static_cast<uint32_t>(rem) << 8);
                        else { x.SetReg(0, w, static_cast<uint32_t>(q)); x.SetReg(2, w, static_cast<uint32_t>(rem)); }
                    } else {
                        int64_t n;
                        if (w == 8) n = static_cast<int16_t>(c.regs[0] & 0xFFFF);
                        else if (w == 16) n = static_cast<int32_t>(((c.regs[2] & 0xFFFF) << 16) | (c.regs[0] & 0xFFFF));
                        else n = static_cast<int64_t>((static_cast<uint64_t>(c.regs[2]) << 32) | c.regs[0]);
                        int64_t dv = static_cast<int32_t>(SignExtend(d, w));
                        if (n == INT64_MIN && dv == -1) { x.Raise(EXC_INT_OVERFLOW); break; }
                        int64_t q = n / dv, rem = n % dv;
                        int64_t lim = static_cast<int64_t>(SignBit(w));
                        if (q >= lim || q < -lim) { x.Raise(EXC_INT_OVERFLOW); break; }
                        if (w == 8) c.regs[0] = (c.regs[0] & 0xFFFF0000u) | (static_cast<uint32_t>(q) & 0xFF) | ((static_cast<uint32_t>(rem) & 0xFF) << 8);
                        else { x.SetReg(0, w, static_cast<uint32_t>(q)); x.SetReg(2, w, static_cast<uint32_t>(rem)); }
                    }
                    break;
                }
            }
            break;
        }

        // --- Группа 4/5 ---
        case 0xFE: case 0xFF: {
            uint8_t w = (op & 1) ? osz : 8;
            ModRM r = x.DecodeModRM();
            if (x.fetchFault) break;
            if (op == 0xFE && r.reg > 1) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
            switch (r.reg) {
                case 0: { uint32_t a = x.ReadRM(r, w); c.SetLazy(LazyOp::Inc, w, a, 1, a + 1); x.WriteRM(r, w, a + 1); break; }
                case 1: { uint32_t a = x.ReadRM(r, w); c.SetLazy(LazyOp::Dec, w, a, 1, a - 1); x.WriteRM(r, w, a - 1); break; }
                case 2: { // CALL r/m
                    uint32_t t = x.ReadRM(r, osz);
                    if (memory_.Fault().pending) break;
                    x.Push(x.NextEip(), osz);
                    jumpTo(t);
                    break;
                }
                case 4: { // JMP r/m
                    uint32_t t = x.ReadRM(r, osz);
                    jumpTo(t);
                    break;
                }
                case 6: x.Push(x.ReadRM(r, osz), osz); break; // PUSH r/m
                default: x.Raise(EXC_ILLEGAL_INSTRUCTION); break; // дальние CALL/JMP
            }
            break;
        }

        // ===== Двухбайтовая карта 0F =====
        case 0x0F: {
            uint8_t op2 = x.Fetch8();
            if (x.fetchFault) break;
            switch (op2) {
                case 0x0B: x.Raise(EXC_ILLEGAL_INSTRUCTION); break; // UD2
                case 0x0D: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
                    // PREFETCH*/NOP r/m: только разбор операнда
                    (void)x.DecodeModRM();
                    break;
                case 0x31: { // RDTSC
                    uint64_t t = g_timeStampCounter * 4;
                    c.regs[0] = static_cast<uint32_t>(t);
                    c.regs[2] = static_cast<uint32_t>(t >> 32);
                    break;
                }
                case 0xA2: { // CPUID: условный Pentium 4 с SSE2
                    uint32_t leaf = c.regs[0];
                    uint32_t a = 0, b = 0, cc = 0, d = 0;
                    if (leaf == 0) {
                        a = 1;
                        b = 0x756E6547; d = 0x49656E69; cc = 0x6C65746E; // "GenuineIntel"
                    } else if (leaf == 1) {
                        a = 0x00000F29; // family 15, model 2, stepping 9
                        b = 0x00010800;
                        cc = 0;
                        d = (1u << 0) | (1u << 4) | (1u << 5) | (1u << 8) | (1u << 15) |
                            (1u << 23) | (1u << 24) | (1u << 25) | (1u << 26);
                    } else if (leaf == 0x80000000u) {
                        a = 0x80000004u;
                    } else if (leaf >= 0x80000002u && leaf <= 0x80000004u) {
                        static const char brand[49] = "switch-recompiler x86 interpreter               ";
                        uint32_t words[4];
                        std::memcpy(words, brand + (leaf - 0x80000002u) * 16, 16);
                        a = words[0]; b = words[1]; cc = words[2]; d = words[3];
                    }
                    c.regs[0] = a; c.regs[3] = b; c.regs[1] = cc; c.regs[2] = d;
                    break;
                }

                // CMOVcc
                case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
                case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    uint32_t v = x.ReadRM(r, osz); // читается всегда (может дать исключение)
                    if (x.Cond(op2 & 0xF)) x.SetReg(r.reg, osz, v);
                    break;
                }

                // Jcc rel16/32
                case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
                case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
                    uint32_t rel = x.FetchImm(osz);
                    if (x.fetchFault) break;
                    if (osz == 16) rel = SignExtend(rel, 16);
                    if (x.Cond(op2 & 0xF)) jumpTo(x.NextEip() + rel);
                    break;
                }

                // SETcc
                case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
                case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    x.WriteRM(r, 8, x.Cond(op2 & 0xF) ? 1 : 0);
                    break;
                }

                case 0xA0: x.Push(c.segSel[SEG_FS], osz); break;
                case 0xA1: c.segSel[SEG_FS] = static_cast<uint16_t>(x.Pop(osz)); break;
                case 0xA8: x.Push(c.segSel[SEG_GS], osz); break;
                case 0xA9: c.segSel[SEG_GS] = static_cast<uint16_t>(x.Pop(osz)); break;

                // BT/BTS/BTR/BTC r/m, reg
                case 0xA3: case 0xAB: case 0xB3: case 0xBB: {
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    uint32_t bitOff = x.GetReg(r.reg, osz);
                    uint32_t v, bit;
                    uint32_t addr = 0;
                    if (r.isReg) {
                        bit = bitOff & (osz - 1);
                        v = x.GetReg(r.rm, osz);
                    } else {
                        int32_t sOff = static_cast<int32_t>(SignExtend(bitOff, osz));
                        int32_t unit = sOff >> (osz == 16 ? 4 : 5);
                        addr = r.addr + static_cast<uint32_t>(unit * (osz / 8));
                        bit = static_cast<uint32_t>(sOff) & (osz - 1);
                        v = x.ReadMem(addr, osz);
                    }
                    bool old = (v >> bit) & 1;
                    c.SetFlag(FLAG_CF, old);
                    if (op2 != 0xA3) {
                        if (op2 == 0xAB) v |= 1u << bit;
                        else if (op2 == 0xB3) v &= ~(1u << bit);
                        else v ^= 1u << bit;
                        if (r.isReg) x.SetReg(r.rm, osz, v); else x.WriteMem(addr, osz, v);
                    }
                    break;
                }
                // BT* r/m, imm8
                case 0xBA: {
                    ModRM r = x.DecodeModRM();
                    uint32_t imm = x.Fetch8();
                    if (x.fetchFault) break;
                    if (r.reg < 4) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
                    uint32_t bit = imm & (osz - 1);
                    uint32_t v = x.ReadRM(r, osz);
                    bool old = (v >> bit) & 1;
                    c.SetFlag(FLAG_CF, old);
                    if (r.reg != 4) {
                        if (r.reg == 5) v |= 1u << bit;
                        else if (r.reg == 6) v &= ~(1u << bit);
                        else v ^= 1u << bit;
                        x.WriteRM(r, osz, v);
                    }
                    break;
                }

                // SHLD/SHRD
                case 0xA4: case 0xA5: case 0xAC: case 0xAD: {
                    ModRM r = x.DecodeModRM();
                    uint32_t count = (op2 & 1) ? (c.regs[1] & 0xFF) : x.Fetch8();
                    if (x.fetchFault) break;
                    uint32_t d = x.ReadRM(r, osz);
                    uint32_t res = DoubleShift(c, op2 < 0xA8, d, x.GetReg(r.reg, osz), count, osz);
                    x.WriteRM(r, osz, res);
                    break;
                }

                case 0xAE: { // группа 15: FXSAVE/FXRSTOR/LDMXCSR/STMXCSR/барьеры
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    if (r.isReg) break; // LFENCE/MFENCE/SFENCE
                    switch (r.reg) {
                        case 0: detail::FpuSave(x, r.addr, true); break;
                        case 1: detail::FpuRestore(x, r.addr, true); break;
                        case 2: c.mxcsr = x.ReadMem(r.addr, 32) & 0xFFFF; break;
                        case 3: x.WriteMem(r.addr, 32, c.mxcsr); break;
                        case 7: break; // CLFLUSH
                        default: x.Raise(EXC_ILLEGAL_INSTRUCTION); break;
                    }
                    break;
                }

                case 0xAF: { // IMUL r, r/m
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    int64_t a = static_cast<int32_t>(SignExtend(x.GetReg(r.reg, osz), osz));
                    int64_t b = static_cast<int32_t>(SignExtend(x.ReadRM(r, osz), osz));
                    int64_t p = a * b;
                    uint32_t res = static_cast<uint32_t>(p) & Mask(osz);
                    bool ov = static_cast<int64_t>(static_cast<int32_t>(SignExtend(res, osz))) != p;
                    x.SetReg(r.reg, osz, res);
                    SetResultFlags(c, res, osz, ov, ov);
                    break;
                }

                case 0xB0: case 0xB1: { // CMPXCHG
                    uint8_t w = (op2 & 1) ? osz : 8;
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    uint32_t d = x.ReadRM(r, w);
                    uint32_t acc = x.GetReg(0, w);
                    Alu(c, 7, acc, d, w);
                    if ((acc & Mask(w)) == (d & Mask(w))) x.WriteRM(r, w, x.GetReg(r.reg, w));
                    else {
                        x.WriteRM(r, w, d); // настоящий CPU всегда пишет
                        x.SetReg(0, w, d);
                    }
                    break;
                }

                case 0xB6: case 0xB7: { // MOVZX
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    uint32_t v = x.ReadRM(r, op2 == 0xB6 ? 8 : 16);
                    x.SetReg(r.reg, osz, v);
                    break;
                }
                case 0xBE: case 0xBF: { // MOVSX
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    uint8_t sw = op2 == 0xBE ? 8 : 16;
                    x.SetReg(r.reg, osz, SignExtend(x.ReadRM(r, sw), sw));
                    break;
                }

                case 0xBC: case 0xBD: { // BSF/BSR (F3: TZCNT/LZCNT на новых CPU — у нас как BSF/BSR)
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    uint32_t v = x.ReadRM(r, osz) & Mask(osz);
                    c.MaterializeFlags();
                    if (v == 0) {
                        c.eflags |= FLAG_ZF;
                    } else {
                        c.eflags &= ~FLAG_ZF;
                        uint32_t idx = op2 == 0xBC ? static_cast<uint32_t>(__builtin_ctz(v))
                                                   : 31u - static_cast<uint32_t>(__builtin_clz(v));
                        x.SetReg(r.reg, osz, idx);
                    }
                    break;
                }

                case 0xC0: case 0xC1: { // XADD
                    uint8_t w = (op2 & 1) ? osz : 8;
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    uint32_t d = x.ReadRM(r, w);
                    uint32_t s = x.GetReg(r.reg, w);
                    uint32_t sum = Alu(c, 0, d, s, w);
                    // Порядок Intel: SRC <- DEST, затем DEST <- сумма (важно, когда это один регистр)
                    if (r.isReg) {
                        x.SetReg(r.reg, w, d);
                        x.SetReg(r.rm, w, sum);
                    } else {
                        x.WriteMem(r.addr, w, sum);
                        if (!memory_.Fault().pending) x.SetReg(r.reg, w, d);
                    }
                    break;
                }

                case 0xC7: { // CMPXCHG8B
                    ModRM r = x.DecodeModRM();
                    if (x.fetchFault) break;
                    if (r.isReg || r.reg != 1) { x.Raise(EXC_ILLEGAL_INSTRUCTION); break; }
                    uint64_t v = x.ReadMem64(r.addr);
                    uint64_t cmp = (static_cast<uint64_t>(c.regs[2]) << 32) | c.regs[0];
                    c.MaterializeFlags();
                    if (v == cmp) {
                        x.WriteMem64(r.addr, (static_cast<uint64_t>(c.regs[1]) << 32) | c.regs[3]);
                        c.eflags |= FLAG_ZF;
                    } else {
                        x.WriteMem64(r.addr, v);
                        c.regs[0] = static_cast<uint32_t>(v);
                        c.regs[2] = static_cast<uint32_t>(v >> 32);
                        c.eflags &= ~FLAG_ZF;
                    }
                    break;
                }

                case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
                    uint32_t v = c.regs[op2 & 7];
                    c.regs[op2 & 7] = __builtin_bswap32(v); // BSWAP (16-битная форма не определена)
                    break;
                }

                default:
                    if (!detail::ExecuteSse0F(x, op2)) x.Raise(EXC_ILLEGAL_INSTRUCTION);
                    break;
            }
            break;
        }

        default:
            x.Raise(EXC_ILLEGAL_INSTRUCTION);
            break;
    }

done:
    if (outInstr) {
        outInstr->length = static_cast<uint8_t>(x.pos);
        outInstr->rawOpcodeForDebug = x.opcode;
        outInstr->mnemonic = Mnemonic::Unknown;
    }

    if (x.fetchFault && !x.exception) {
        // Не дочитали инструкцию: либо кончилась выделенная память, либо
        // длина больше 15 байт.
        if (x.pos >= 15) x.Raise(EXC_ILLEGAL_INSTRUCTION);
        else {
            x.Raise(EXC_ACCESS_VIOLATION);
            x.exc.faultAddress = x.start + x.pos;
        }
    }
    if (!x.exception && memory_.Fault().pending) {
        x.Raise(EXC_ACCESS_VIOLATION);
        x.exc.faultAddress = memory_.Fault().address;
        x.exc.write = memory_.Fault().write;
    }

    if (x.exception) {
        if (!x.keepState) {
            std::memcpy(cpu.regs, savedRegs, sizeof(savedRegs));
            cpu.eflags = savedEflags;
            cpu.lazyOp = savedOp;
            cpu.lazyWidth = savedW;
            cpu.lazyCarryIn = savedCin;
            cpu.lazyA = savedA;
            cpu.lazyB = savedB;
            cpu.lazyRes = savedRes;
        }
        cpu.eip = savedEip;
        exception_ = x.exc;
        memory_.ClearFault();
        return x.exc.code == EXC_ILLEGAL_INSTRUCTION ? StepResult::UnknownOpcode : StepResult::Exception;
    }

    cpu.eip = jumped ? next : x.NextEip();
    if (result == StepResult::Halted) cpu.eip = x.NextEip();
    return result;
}

} // namespace cpu

#pragma once
// exec_context.h
//
// Внутренности интерпретатора, общие для целочисленной части
// (interpreter.cpp), x87 (fpu.cpp) и MMX/SSE (sse.cpp): разбор префиксов
// и ModRM, доступ к операндам, стек, исключения. Не для использования вне
// source/cpu.

#include <cstdint>
#include <cstring>
#include "cpu_state.h"
#include "guest_memory.h"
#include "interpreter.h"

namespace cpu {
namespace detail {

// Разобранный операнд ModRM: либо регистр (поле rm), либо адрес в памяти.
struct ModRM {
    uint8_t mod = 0;
    uint8_t reg = 0;  // поле reg (номер регистра или расширение опкода)
    uint8_t rm = 0;
    bool isReg = false;
    uint32_t addr = 0; // линейный адрес (с учётом базы сегмента), если !isReg
    uint32_t ea = 0;   // смещение без базы сегмента (для LEA)
};

struct ExecContext {
    CpuState& c;
    IGuestMemory& m;
    uint32_t fsBase;
    uint32_t gsBase;

    const uint8_t* code = nullptr;
    uint32_t fetchLimit = 0;
    uint32_t pos = 0;
    bool fetchFault = false;

    uint32_t start = 0;      // EIP начала инструкции
    uint8_t osz = 32;        // размер операнда: 16 или 32
    uint8_t asz = 32;        // размер адреса: 16 или 32
    int8_t seg = -1;         // префикс сегмента или -1
    uint8_t rep = 0;         // 0, 0xF2 (REPNE) или 0xF3 (REP/REPE)
    bool opsizePrefix = false; // был ли 0x66 (для SSE это выбор "pd/66" формы)
    uint8_t opcode = 0;

    bool exception = false;
    bool keepState = false;  // не откатывать регистры при исключении (REP-инструкции)
    CpuException exc;

    ExecContext(CpuState& cpu, IGuestMemory& mem, uint32_t fs, uint32_t gs)
        : c(cpu), m(mem), fsBase(fs), gsBase(gs) {}

    // --- Выборка байт инструкции ---
    uint8_t Fetch8() {
        if (pos >= fetchLimit || pos >= 15) { fetchFault = true; return 0; }
        return code[pos++];
    }
    uint16_t Fetch16() { uint16_t lo = Fetch8(); return static_cast<uint16_t>(lo | (Fetch8() << 8)); }
    uint32_t Fetch32() { uint32_t lo = Fetch16(); return lo | (static_cast<uint32_t>(Fetch16()) << 16); }
    uint32_t FetchImm(uint8_t w) { return w == 8 ? Fetch8() : w == 16 ? Fetch16() : Fetch32(); }
    int32_t FetchSimm8() { return static_cast<int8_t>(Fetch8()); }
    uint32_t NextEip() const { return start + pos; }

    // --- Исключения ---
    void Raise(uint32_t code) {
        if (exception) return;
        exception = true;
        exc = CpuException{};
        exc.code = code;
        exc.eip = start;
    }

    // --- Сегменты ---
    uint32_t SegBase(int s) const {
        if (s == SEG_FS) return fsBase ? fsBase : c.segBase[SEG_FS];
        if (s == SEG_GS) return gsBase ? gsBase : c.segBase[SEG_GS];
        return c.segBase[s];
    }
    uint32_t DataSegBase(int defaultSeg = SEG_DS) const {
        return SegBase(seg >= 0 ? seg : defaultSeg);
    }

    // --- Регистры ---
    uint32_t GetReg(int i, uint8_t w) const {
        if (w == 32) return c.regs[i];
        if (w == 16) return c.regs[i] & 0xFFFF;
        return i < 4 ? (c.regs[i] & 0xFF) : ((c.regs[i - 4] >> 8) & 0xFF);
    }
    void SetReg(int i, uint8_t w, uint32_t v) {
        if (w == 32) { c.regs[i] = v; return; }
        if (w == 16) { c.regs[i] = (c.regs[i] & 0xFFFF0000u) | (v & 0xFFFF); return; }
        if (i < 4) c.regs[i] = (c.regs[i] & 0xFFFFFF00u) | (v & 0xFF);
        else c.regs[i - 4] = (c.regs[i - 4] & 0xFFFF00FFu) | ((v & 0xFF) << 8);
    }

    // --- Память ---
    uint32_t ReadMem(uint32_t a, uint8_t w) {
        return w == 8 ? m.Read8(a) : w == 16 ? m.Read16(a) : m.Read32(a);
    }
    void WriteMem(uint32_t a, uint8_t w, uint32_t v) {
        if (w == 8) m.Write8(a, static_cast<uint8_t>(v));
        else if (w == 16) m.Write16(a, static_cast<uint16_t>(v));
        else m.Write32(a, v);
    }
    uint64_t ReadMem64(uint32_t a) {
        uint64_t lo = m.Read32(a);
        return lo | (static_cast<uint64_t>(m.Read32(a + 4)) << 32);
    }
    void WriteMem64(uint32_t a, uint64_t v) {
        // Сначала проверка всего диапазона, чтобы не записать половину.
        if (!m.IsRangeMapped(a, 8)) { m.Write8(m.IsMapped(a) ? (a | 0xFFF) + 1 : a, 0); return; }
        m.Write32(a, static_cast<uint32_t>(v));
        m.Write32(a + 4, static_cast<uint32_t>(v >> 32));
    }

    // --- ModRM ---
    ModRM DecodeModRM() {
        ModRM r;
        uint8_t b = Fetch8();
        r.mod = b >> 6;
        r.reg = (b >> 3) & 7;
        r.rm = b & 7;
        if (r.mod == 3) { r.isReg = true; return r; }
        if (asz == 16) { Ea16(r); return r; }

        uint32_t a = 0;
        int defSeg = SEG_DS;
        if (r.rm == 4) {
            uint8_t sib = Fetch8();
            uint8_t scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
            if (index != 4) a = c.regs[index] << scale;
            if (base == 5 && r.mod == 0) {
                a += Fetch32();
            } else {
                a += c.regs[base];
                if (base == 4 || base == 5) defSeg = SEG_SS;
            }
        } else if (r.rm == 5 && r.mod == 0) {
            a = Fetch32();
        } else {
            a = c.regs[r.rm];
            if (r.rm == 5) defSeg = SEG_SS;
        }
        if (r.mod == 1) a += static_cast<uint32_t>(FetchSimm8());
        else if (r.mod == 2) a += Fetch32();
        r.ea = a;
        r.addr = a + DataSegBase(defSeg);
        return r;
    }

    void Ea16(ModRM& r) {
        uint32_t a = 0;
        int defSeg = SEG_DS;
        uint32_t bx = c.regs[3] & 0xFFFF, bp = c.regs[5] & 0xFFFF;
        uint32_t si = c.regs[6] & 0xFFFF, di = c.regs[7] & 0xFFFF;
        switch (r.rm) {
            case 0: a = bx + si; break;
            case 1: a = bx + di; break;
            case 2: a = bp + si; defSeg = SEG_SS; break;
            case 3: a = bp + di; defSeg = SEG_SS; break;
            case 4: a = si; break;
            case 5: a = di; break;
            case 6:
                if (r.mod == 0) a = Fetch16();
                else { a = bp; defSeg = SEG_SS; }
                break;
            case 7: a = bx; break;
        }
        if (r.mod == 1) a += static_cast<uint32_t>(FetchSimm8());
        else if (r.mod == 2) a += Fetch16();
        r.ea = a & 0xFFFF;
        r.addr = r.ea + DataSegBase(defSeg);
    }

    uint32_t ReadRM(const ModRM& r, uint8_t w) {
        return r.isReg ? GetReg(r.rm, w) : ReadMem(r.addr, w);
    }
    void WriteRM(const ModRM& r, uint8_t w, uint32_t v) {
        if (r.isReg) SetReg(r.rm, w, v); else WriteMem(r.addr, w, v);
    }

    // --- Стек ---
    void Push(uint32_t v, uint8_t w = 32) {
        uint32_t sp = c.regs[4] - (w / 8);
        WriteMem(sp + SegBase(SEG_SS), w, v);
        c.regs[4] = sp;
    }
    uint32_t Pop(uint8_t w = 32) {
        uint32_t v = ReadMem(c.regs[4] + SegBase(SEG_SS), w);
        c.regs[4] += w / 8;
        return v;
    }

    // --- Условия Jcc/SETcc/CMOVcc ---
    bool Cond(uint8_t cc) const {
        bool r;
        switch (cc >> 1) {
            case 0: r = c.OF(); break;                       // O
            case 1: r = c.CF(); break;                       // B
            case 2: r = c.ZF(); break;                       // E
            case 3: r = c.CF() || c.ZF(); break;             // BE
            case 4: r = c.SF(); break;                       // S
            case 5: r = c.PF(); break;                       // P
            case 6: r = c.SF() != c.OF(); break;             // L
            default: r = c.ZF() || (c.SF() != c.OF()); break; // LE
        }
        return (cc & 1) ? !r : r;
    }

    // Счётчик для LOOP/JECXZ/REP — зависит от размера адреса.
    uint32_t Counter() const { return asz == 16 ? (c.regs[1] & 0xFFFF) : c.regs[1]; }
    void SetCounter(uint32_t v) {
        if (asz == 16) c.regs[1] = (c.regs[1] & 0xFFFF0000u) | (v & 0xFFFF); else c.regs[1] = v;
    }
};

// Реализованы в fpu.cpp и sse.cpp. Возвращают false, если опкод не их.
void ExecuteFpu(ExecContext& x);                 // D8..DF
bool ExecuteSse0F(ExecContext& x, uint8_t op);   // 0F xx: MMX/SSE/SSE2
void FpuSave(ExecContext& x, uint32_t addr, bool fxsave);    // FNSAVE/FXSAVE
void FpuRestore(ExecContext& x, uint32_t addr, bool fxsave); // FRSTOR/FXRSTOR

} // namespace detail
} // namespace cpu

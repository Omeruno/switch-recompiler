#pragma once
// cpu_state.h
//
// Состояние 32-битного x86 CPU (IA-32, пользовательский режим): регистры
// общего назначения, EFLAGS, сегментные базы, x87 FPU, MMX/SSE.
// Индексы регистров совпадают с их кодировкой в ModRM/opcode x86 — это
// не совпадение, а специально сделано, чтобы decoder мог просто
// использовать 3-битное поле reg/rm напрямую как индекс массива.
//
// Флаги считаются лениво: арифметическая инструкция запоминает операнды и
// результат (lazy*), а CF/PF/AF/ZF/SF/OF вычисляются, только когда их
// кто-то читает (Jcc, PUSHF, ...). Большинство флагов никто не читает,
// а их вычисление — заметная доля стоимости интерпретации.

#include <cstdint>

namespace cpu {

// Порядок ВАЖЕН — это реальная кодировка регистров в x86 ModRM byte
enum class Reg32 : uint8_t {
    EAX = 0,
    ECX = 1,
    EDX = 2,
    EBX = 3,
    ESP = 4,
    EBP = 5,
    ESI = 6,
    EDI = 7,
};

inline const char* RegName(Reg32 r) {
    static const char* names[8] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
    return names[static_cast<uint8_t>(r) & 7];
}

// Сегменты в порядке кодировки (поле reg у MOV Sreg и префиксы).
enum Seg : uint8_t { SEG_ES = 0, SEG_CS = 1, SEG_SS = 2, SEG_DS = 3, SEG_FS = 4, SEG_GS = 5 };

// Биты EFLAGS
constexpr uint32_t FLAG_CF = 1u << 0;
constexpr uint32_t FLAG_PF = 1u << 2;
constexpr uint32_t FLAG_AF = 1u << 4;
constexpr uint32_t FLAG_ZF = 1u << 6;
constexpr uint32_t FLAG_SF = 1u << 7;
constexpr uint32_t FLAG_TF = 1u << 8;
constexpr uint32_t FLAG_IF = 1u << 9;
constexpr uint32_t FLAG_DF = 1u << 10;
constexpr uint32_t FLAG_OF = 1u << 11;
constexpr uint32_t FLAG_AC = 1u << 18;
constexpr uint32_t FLAG_ID = 1u << 21;
constexpr uint32_t FLAGS_ARITH = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_OF;

// Какая операция последней выставила арифметические флаги.
enum class LazyOp : uint8_t {
    None,   // eflags содержит актуальные значения всех флагов
    Add,    // res = a + b
    Adc,    // res = a + b + carryIn
    Sub,    // res = a - b (также CMP, NEG как 0 - b)
    Sbb,    // res = a - b - carryIn
    Logic,  // AND/OR/XOR/TEST: CF=OF=AF=0
    Inc,    // как Add с b=1, но CF сохраняется
    Dec,    // как Sub с b=1, но CF сохраняется
};

inline bool Parity8(uint32_t v) {
    v &= 0xFF;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;
}

union XmmReg {
    uint8_t  b[16];
    uint16_t w[8];
    uint32_t d[4];
    uint64_t q[2];
    int8_t   sb[16];
    int16_t  sw[8];
    int32_t  sd[4];
    int64_t  sq[2];
    float    f[4];
    double   fd[2];
};

struct CpuState {
    uint32_t regs[8] = {0};  // индексируется через Reg32
    uint32_t eip = 0;

    // --- EFLAGS (см. комментарий в начале файла) ---
    uint32_t eflags = 0x202;   // бит 1 всегда 1, IF=1
    LazyOp lazyOp = LazyOp::None;
    uint8_t lazyWidth = 32;    // 8/16/32
    uint8_t lazyCarryIn = 0;   // для Adc/Sbb
    uint32_t lazyA = 0, lazyB = 0, lazyRes = 0; // уже обрезаны до lazyWidth

    // --- Сегменты: плоская модель, базы ненулевые только у FS/GS ---
    uint16_t segSel[6] = {0x23, 0x1B, 0x23, 0x23, 0x3B, 0x00};
    uint32_t segBase[6] = {0, 0, 0, 0, 0, 0};

    // --- x87 FPU. Регистры хранятся как double: Windows по умолчанию
    // ставит точность 53 бита (_controlfp), так что для реальных программ
    // это почти всегда совпадает с железом. st[i] — физический регистр,
    // ST(i) = st[(fpuTop + i) & 7].
    double st[8] = {0};
    uint16_t fpuControl = 0x027F;
    uint16_t fpuStatus = 0;     // без поля TOP (оно в fpuTop)
    uint16_t fpuTag = 0xFFFF;   // 2 бита на регистр, 11 = пусто
    uint8_t fpuTop = 0;
    uint32_t fpuLastIp = 0, fpuLastDp = 0;
    uint16_t fpuLastOpcode = 0;

    // --- MMX (отдельно от st, псевдоним не эмулируется) и SSE ---
    uint64_t mm[8] = {0};
    XmmReg xmm[8] = {};
    uint32_t mxcsr = 0x1F80;

    uint32_t& Get(Reg32 r)       { return regs[static_cast<uint8_t>(r) & 7]; }
    uint32_t  Get(Reg32 r) const { return regs[static_cast<uint8_t>(r) & 7]; }

    uint32_t& ESP() { return regs[static_cast<uint8_t>(Reg32::ESP)]; }
    uint32_t& EBP() { return regs[static_cast<uint8_t>(Reg32::EBP)]; }

    // --- Флаги ---

    static uint32_t Mask(uint8_t w) { return w == 32 ? 0xFFFFFFFFu : ((1u << w) - 1u); }
    static uint32_t SignBit(uint8_t w) { return 1u << (w - 1); }

    bool CF() const {
        switch (lazyOp) {
            case LazyOp::None:
            case LazyOp::Inc:
            case LazyOp::Dec:   return (eflags & FLAG_CF) != 0;
            case LazyOp::Add:   return lazyRes < lazyA;
            case LazyOp::Adc:   return lazyRes < lazyA || (lazyCarryIn && lazyRes == lazyA);
            case LazyOp::Sub:   return lazyA < lazyB;
            case LazyOp::Sbb:   return lazyA < lazyB || (lazyCarryIn && lazyA == lazyB);
            case LazyOp::Logic: return false;
        }
        return false;
    }
    bool ZF() const { return lazyOp == LazyOp::None ? (eflags & FLAG_ZF) != 0 : lazyRes == 0; }
    bool SF() const {
        return lazyOp == LazyOp::None ? (eflags & FLAG_SF) != 0 : (lazyRes & SignBit(lazyWidth)) != 0;
    }
    bool PF() const { return lazyOp == LazyOp::None ? (eflags & FLAG_PF) != 0 : Parity8(lazyRes); }
    bool AF() const {
        switch (lazyOp) {
            case LazyOp::None:  return (eflags & FLAG_AF) != 0;
            case LazyOp::Logic: return false;
            default:            return ((lazyA ^ lazyB ^ lazyRes) & 0x10) != 0;
        }
    }
    bool OF() const {
        uint32_t s = SignBit(lazyWidth);
        switch (lazyOp) {
            case LazyOp::None:  return (eflags & FLAG_OF) != 0;
            case LazyOp::Logic: return false;
            case LazyOp::Add:
            case LazyOp::Adc:
            case LazyOp::Inc:   return ((lazyA ^ lazyRes) & (lazyB ^ lazyRes) & s) != 0;
            case LazyOp::Sub:
            case LazyOp::Sbb:
            case LazyOp::Dec:   return ((lazyA ^ lazyB) & (lazyA ^ lazyRes) & s) != 0;
        }
        return false;
    }
    bool DF() const { return (eflags & FLAG_DF) != 0; }

    // Полное значение EFLAGS (для PUSHF, контекста исключения и т.п.).
    uint32_t GetEflags() const {
        if (lazyOp == LazyOp::None) return eflags;
        uint32_t f = eflags & ~FLAGS_ARITH;
        if (CF()) f |= FLAG_CF;
        if (PF()) f |= FLAG_PF;
        if (AF()) f |= FLAG_AF;
        if (ZF()) f |= FLAG_ZF;
        if (SF()) f |= FLAG_SF;
        if (OF()) f |= FLAG_OF;
        return f;
    }
    // Превращает ленивые флаги в явные (перед частичным обновлением флагов).
    void MaterializeFlags() {
        if (lazyOp == LazyOp::None) return;
        eflags = GetEflags();
        lazyOp = LazyOp::None;
    }
    void SetEflags(uint32_t f) {
        eflags = (f & 0x003F7FD5u) | 0x2u; // сохраняемые в user mode биты + бит 1
        lazyOp = LazyOp::None;
    }
    void SetFlag(uint32_t bit, bool on) {
        MaterializeFlags();
        if (on) eflags |= bit; else eflags &= ~bit;
    }
    void SetLazy(LazyOp op, uint8_t width, uint32_t a, uint32_t b, uint32_t res) {
        if (op == LazyOp::Inc || op == LazyOp::Dec) {
            // CF не меняется: фиксируем его текущее значение в eflags.
            bool cf = CF();
            eflags = (eflags & ~FLAGS_ARITH) | (cf ? FLAG_CF : 0);
        } else if (lazyOp == LazyOp::None) {
            // eflags хранит актуальные флаги; арифметические сейчас будут
            // перезаписаны ленивыми, остальные (DF, IF, ...) остаются.
        }
        uint32_t m = Mask(width);
        lazyOp = op;
        lazyWidth = width;
        lazyA = a & m;
        lazyB = b & m;
        lazyRes = res & m;
    }

    // --- x87 вспомогательное ---
    uint16_t GetFpuStatusWord() const {
        return static_cast<uint16_t>((fpuStatus & ~0x3800u) | ((fpuTop & 7u) << 11));
    }
};

} // namespace cpu

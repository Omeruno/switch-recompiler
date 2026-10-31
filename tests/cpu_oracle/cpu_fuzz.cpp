// cpu_fuzz.cpp — сверка интерпретатора с настоящим процессором.
//
// Генерирует случайные тесты из одной инструкции (опкоды, префиксы,
// ModRM/SIB, непосредственные значения, состояние регистров и флагов),
// прогоняет их через oracle (нативно, 32-бит) и через cpu::Interpreter и
// сравнивает: как завершилась инструкция, регистры, определённые флаги,
// содержимое памяти (хеш), XMM/MXCSR/статус FPU.
//
//   cpu_fuzz <путь к oracle> [число тестов] [сид] [фильтр: int|fpu|sse|all]
//
// Код возврата 0 — расхождений нет.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>

#include "source/cpu/interpreter.h"

extern "C" {
#include "oracle_common.h"
}

namespace {

constexpr uint32_t F_CF = cpu::FLAG_CF, F_PF = cpu::FLAG_PF, F_AF = cpu::FLAG_AF, F_ZF = cpu::FLAG_ZF,
                   F_SF = cpu::FLAG_SF, F_OF = cpu::FLAG_OF, F_DF = cpu::FLAG_DF;
constexpr uint32_t F_ALL = F_CF | F_PF | F_AF | F_ZF | F_SF | F_OF;

struct Rng {
    uint64_t s;
    uint32_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 11);
    }
    uint32_t Below(uint32_t n) { return n ? Next() % n : 0; }
    bool Chance(uint32_t percent) { return Below(100) < percent; }
};

// Интересные значения для регистров/непосредственных операндов.
uint32_t Interesting(Rng& r) {
    static const uint32_t vals[] = {0, 1, 2, 3, 7, 8, 9, 0x0F, 0x10, 0x1F, 0x20, 0x7F, 0x80, 0xFF, 0x100,
                                    0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u, 0x80000001u,
                                    0xFFFFFFFFu, 0xFFFFFFFEu, 0x99, 0x9A, 0x55555555, 0xAAAAAAAAu};
    return vals[r.Below(sizeof(vals) / sizeof(vals[0]))];
}
uint32_t RandomValue(Rng& r) {
    switch (r.Below(4)) {
        case 0: return Interesting(r);
        case 1: return r.Below(64);
        default: return r.Next() ^ (r.Next() << 16);
    }
}

struct Built {
    OracleCase t{};
    uint32_t flagMask = F_ALL | F_DF; // какие флаги сравнивать
    bool compareXmm = false;
    bool compareFpu = false;
    std::string name;
};

class Builder {
public:
    Builder(Rng& r, Built& b) : r_(r), b_(b) {
        for (int i = 0; i < 8; ++i) b_.t.regs[i] = RandomValue(r_);
        b_.t.regs[4] = ORACLE_DATA_BASE + 0x1000 + (r_.Below(0x400) & ~3u); // ESP
        b_.t.regs[5] = ORACLE_DATA_BASE + 0x400 + (r_.Below(0x800) & ~3u);  // EBP
        uint32_t fl = 0x202;
        if (r_.Chance(50)) fl |= F_CF;
        if (r_.Chance(50)) fl |= F_PF;
        if (r_.Chance(50)) fl |= F_AF;
        if (r_.Chance(50)) fl |= F_ZF;
        if (r_.Chance(50)) fl |= F_SF;
        if (r_.Chance(50)) fl |= F_OF;
        if (r_.Chance(10)) fl |= F_DF;
        b_.t.eflags = fl;
        b_.t.seed = r_.Next() | 1;
    }

    void Byte(uint8_t v) { if (b_.t.codeLen < ORACLE_CODE_MAX) b_.t.code[b_.t.codeLen++] = v; }
    void Imm(uint32_t v, int bytes) { for (int i = 0; i < bytes; ++i) Byte(static_cast<uint8_t>(v >> (8 * i))); }

    uint32_t DataTarget(uint32_t margin = 16) {
        return ORACLE_DATA_BASE + 0x100 + r_.Below(ORACLE_DATA_SIZE - 0x300 - margin);
    }
    void Patch(uint32_t addr, uint32_t value) {
        if (b_.t.patchCount < ORACLE_MAX_PATCHES) {
            b_.t.patchAddr[b_.t.patchCount] = addr;
            b_.t.patchValue[b_.t.patchCount] = value;
            b_.t.patchCount++;
        }
    }

    // ModRM с операндом-памятью, указывающим в область данных. reg — поле
    // reg. Возвращает эффективный адрес (для заплаток).
    uint32_t ModRMMem(uint8_t reg) {
        for (;;) {
            uint8_t mod = static_cast<uint8_t>(r_.Below(3));
            uint8_t rm = static_cast<uint8_t>(r_.Below(8));
            uint32_t target = DataTarget() & ~(align_ - 1);
            uint32_t regs[8];
            std::memcpy(regs, b_.t.regs, sizeof(regs));
            std::vector<uint8_t> enc;
            enc.push_back(static_cast<uint8_t>((mod << 6) | (reg << 3) | rm));
            int32_t disp = mod == 1 ? static_cast<int8_t>(r_.Next()) : (mod == 2 ? static_cast<int32_t>(r_.Below(0x200)) - 0x100 : 0);
            uint32_t ea;
            if (rm == 4) {
                uint8_t scale = static_cast<uint8_t>(r_.Below(4));
                uint8_t index = static_cast<uint8_t>(r_.Below(8));
                uint8_t base = static_cast<uint8_t>(r_.Below(8));
                enc.push_back(static_cast<uint8_t>((scale << 6) | (index << 3) | base));
                uint32_t idxVal = 0;
                if (index != 4) {
                    if (index == 5 || index == base) continue; // EBP индексом и совпадения не подгоняем
                    regs[index] = r_.Below(32);
                    idxVal = regs[index] << scale;
                }
                if (base == 5 && mod == 0) {
                    uint32_t d = target - idxVal;
                    for (int i = 0; i < 4; ++i) enc.push_back(static_cast<uint8_t>(d >> (8 * i)));
                    ea = target;
                } else {
                    if (base == 4 || base == 5) {
                        ea = regs[base] + idxVal + static_cast<uint32_t>(disp);
                    } else {
                        regs[base] = target - idxVal - static_cast<uint32_t>(disp);
                        ea = target;
                    }
                    if (mod == 1) enc.push_back(static_cast<uint8_t>(disp));
                    if (mod == 2) for (int i = 0; i < 4; ++i) enc.push_back(static_cast<uint8_t>(static_cast<uint32_t>(disp) >> (8 * i)));
                }
            } else if (rm == 5 && mod == 0) {
                for (int i = 0; i < 4; ++i) enc.push_back(static_cast<uint8_t>(target >> (8 * i)));
                ea = target;
            } else {
                if (rm == 5) ea = regs[5] + static_cast<uint32_t>(disp);
                else { regs[rm] = target - static_cast<uint32_t>(disp); ea = target; }
                if (mod == 1) enc.push_back(static_cast<uint8_t>(disp));
                if (mod == 2) for (int i = 0; i < 4; ++i) enc.push_back(static_cast<uint8_t>(static_cast<uint32_t>(disp) >> (8 * i)));
            }
            std::memcpy(b_.t.regs, regs, sizeof(regs));
            for (uint8_t e : enc) Byte(e);
            return ea;
        }
    }
    void ModRMReg(uint8_t reg, uint8_t rm) { Byte(static_cast<uint8_t>(0xC0 | (reg << 3) | rm)); }
    // Регистр или память; возвращает EA (0 для регистра).
    uint32_t ModRMAny(uint8_t reg, bool allowReg = true, bool allowMem = true) {
        if (allowReg && (!allowMem || r_.Chance(45))) {
            uint8_t rm = static_cast<uint8_t>(r_.Below(8));
            ModRMReg(reg, rm);
            lastRm_ = rm;
            return 0;
        }
        lastRm_ = 0xFF;
        return ModRMMem(reg);
    }
    uint8_t lastRm_ = 0xFF;
    uint32_t align_ = 1;

    Rng& r_;
    Built& b_;
};

// --- Генераторы отдельных групп инструкций ---

void GenAlu(Builder& g, Rng& r, bool o16) {
    uint8_t aluOp = static_cast<uint8_t>(r.Below(8));
    uint8_t form = static_cast<uint8_t>(r.Below(6));
    g.Byte(static_cast<uint8_t>((aluOp << 3) | form));
    uint32_t undef = (aluOp == 1 || aluOp == 4 || aluOp == 6) ? F_AF : 0;
    g.b_.flagMask &= ~undef;
    if (form < 4) g.ModRMAny(static_cast<uint8_t>(r.Below(8)));
    else if (form == 4) g.Imm(RandomValue(r), 1);
    else g.Imm(RandomValue(r), o16 ? 2 : 4);
    g.b_.name = "alu";
}

void GenGroup1(Builder& g, Rng& r, bool o16) {
    static const uint8_t ops[] = {0x80, 0x81, 0x83};
    uint8_t op = ops[r.Below(3)];
    g.Byte(op);
    uint8_t aluOp = static_cast<uint8_t>(r.Below(8));
    g.ModRMAny(aluOp);
    if (op == 0x81) g.Imm(RandomValue(r), o16 ? 2 : 4);
    else g.Imm(RandomValue(r), 1);
    if (aluOp == 1 || aluOp == 4 || aluOp == 6) g.b_.flagMask &= ~F_AF;
    g.b_.name = "grp1";
}

void GenShift(Builder& g, Rng& r, bool o16) {
    static const uint8_t ops[] = {0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3};
    uint8_t op = ops[r.Below(6)];
    uint8_t sub = static_cast<uint8_t>(r.Below(8));
    g.Byte(op);
    g.ModRMAny(sub);
    uint32_t count;
    if (op <= 0xC1) { count = r.Chance(70) ? r.Below(34) : r.Below(256); g.Imm(count, 1); }
    else if (op <= 0xD1) count = 1;
    else {
        count = r.Chance(70) ? r.Below(34) : r.Below(256);
        g.b_.t.regs[1] = (g.b_.t.regs[1] & ~0xFFu) | count;
    }
    uint8_t w = (op & 1) ? (o16 ? 16 : 32) : 8;
    // Если CL — это ещё и операнд-регистр, его значение = count.
    uint32_t c = count & 31;
    if (sub == 2 || sub == 3) c = w == 8 ? c % 9 : (w == 16 ? c % 17 : c);
    uint32_t undef = 0;
    if (c != 0) {
        undef |= F_AF;
        if (c != 1) undef |= F_OF;
        if ((sub == 4 || sub == 5 || sub == 6 || sub == 7) && c >= w) undef |= F_CF | F_OF;
    }
    // Вращения: OF определён только для счётчика 1. Если (count & 31) != 0, но
    // после деления по модулю 9/17 вышел 0, OF разные CPU ведут по-разному
    // (Intel не трогает, AMD меняет) — тоже не сравниваем.
    if (sub < 4 && (count & 31) != 0 && (count & 31) != 1) undef |= F_OF;
    g.b_.flagMask &= ~undef;
    g.b_.name = "shift";
}

void GenGroup3(Builder& g, Rng& r, bool o16) {
    uint8_t op = r.Chance(50) ? 0xF6 : 0xF7;
    uint8_t sub = static_cast<uint8_t>(r.Below(8));
    g.Byte(op);
    g.ModRMAny(sub);
    uint8_t w = op == 0xF7 ? (o16 ? 16 : 32) : 8;
    if (sub < 2) g.Imm(RandomValue(r), w / 8);
    if (sub == 0 || sub == 1) g.b_.flagMask &= ~F_AF;
    if (sub == 4 || sub == 5) g.b_.flagMask &= ~(F_SF | F_ZF | F_AF | F_PF);
    if (sub >= 6) {
        g.b_.flagMask &= ~F_ALL;
        // Делимое поменьше, чтобы чаще не было переполнения
        if (r.Chance(70)) g.b_.t.regs[2] = r.Below(4);
    }
    g.b_.name = "grp3";
}

void GenIncDecPushPop(Builder& g, Rng& r, bool o16) {
    (void)o16;
    switch (r.Below(5)) {
        case 0: g.Byte(static_cast<uint8_t>(0x40 + r.Below(16))); g.b_.name = "incdec"; break;
        case 1: g.Byte(static_cast<uint8_t>(0x50 + r.Below(8))); g.b_.name = "push"; break;
        case 2: g.Byte(static_cast<uint8_t>(0x58 + r.Below(8))); g.b_.name = "pop"; break;
        case 3: { uint8_t s = static_cast<uint8_t>(r.Below(2)); g.Byte(0xFE); g.ModRMAny(s); g.b_.name = "incdec8"; break; }
        default: { uint8_t s = static_cast<uint8_t>(r.Below(3)); s = s == 2 ? 6 : s; g.Byte(0xFF); g.ModRMAny(s); g.b_.name = "ff"; break; }
    }
}

void GenMov(Builder& g, Rng& r, bool o16) {
    switch (r.Below(9)) {
        case 0: g.Byte(static_cast<uint8_t>(0x88 + r.Below(4))); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.b_.name = "mov"; break;
        case 1: g.Byte(static_cast<uint8_t>(0xB0 + r.Below(8))); g.Imm(RandomValue(r), 1); g.b_.name = "movimm8"; break;
        case 2: g.Byte(static_cast<uint8_t>(0xB8 + r.Below(8))); g.Imm(RandomValue(r), o16 ? 2 : 4); g.b_.name = "movimm"; break;
        case 3: { uint8_t op = r.Chance(50) ? 0xC6 : 0xC7; g.Byte(op); g.ModRMAny(0); g.Imm(RandomValue(r), op == 0xC6 ? 1 : (o16 ? 2 : 4)); g.b_.name = "movrmimm"; break; }
        case 4: g.Byte(0x8D); g.ModRMMem(static_cast<uint8_t>(r.Below(8))); g.b_.name = "lea"; break;
        case 5: { uint8_t op = static_cast<uint8_t>(0xA0 + r.Below(4)); g.Byte(op); g.Imm(g.DataTarget(), 4); g.b_.name = "moffs"; break; }
        case 6: g.Byte(static_cast<uint8_t>(0x84 + r.Below(4))); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.b_.flagMask &= ~F_AF; g.b_.name = "testxchg"; break;
        case 7: g.Byte(static_cast<uint8_t>(0x90 + r.Below(8))); g.b_.name = "xchgacc"; break;
        default: { static const uint8_t m[] = {0x98, 0x99, 0x9E, 0x9F, 0xF5, 0xF8, 0xF9, 0xFC, 0xFD, 0x9C, 0x60, 0x61, 0xC9};
                   uint8_t op = m[r.Below(sizeof(m))]; g.Byte(op); g.b_.name = "misc"; break; }
    }
}

void GenImul(Builder& g, Rng& r, bool o16) {
    switch (r.Below(3)) {
        case 0: g.Byte(0x0F); g.Byte(0xAF); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); break;
        case 1: g.Byte(0x69); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.Imm(RandomValue(r), o16 ? 2 : 4); break;
        default: g.Byte(0x6B); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.Imm(RandomValue(r), 1); break;
    }
    g.b_.flagMask &= ~(F_SF | F_ZF | F_AF | F_PF);
    g.b_.name = "imul";
}

void GenBcd(Builder& g, Rng& r, bool) {
    static const uint8_t ops[] = {0x27, 0x2F, 0x37, 0x3F, 0xD4, 0xD5};
    uint8_t op = ops[r.Below(6)];
    g.Byte(op);
    if (op == 0xD4 || op == 0xD5) g.Imm(r.Chance(70) ? 10 : r.Below(256), 1);
    if (op == 0x37 || op == 0x3F) g.b_.flagMask &= ~(F_OF | F_SF | F_ZF | F_PF);
    if (op == 0x27 || op == 0x2F) g.b_.flagMask &= ~F_OF;
    if (op == 0xD4 || op == 0xD5) g.b_.flagMask &= ~(F_OF | F_AF | F_CF);
    g.b_.name = "bcd";
}

void Gen0F(Builder& g, Rng& r, bool o16) {
    switch (r.Below(11)) {
        case 0: g.Byte(0x0F); g.Byte(static_cast<uint8_t>(0x40 + r.Below(16))); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.b_.name = "cmov"; break;
        case 1: g.Byte(0x0F); g.Byte(static_cast<uint8_t>(0x90 + r.Below(16))); g.ModRMAny(0); g.b_.name = "setcc"; break;
        case 2: { static const uint8_t ops[] = {0xA3, 0xAB, 0xB3, 0xBB}; g.Byte(0x0F); g.Byte(ops[r.Below(4)]);
                  uint8_t reg = static_cast<uint8_t>(r.Below(8));
                  uint32_t ea = g.ModRMAny(reg);
                  if (ea && reg != 4 && reg != 5) {
                      // смещение бита — небольшое, иначе адрес уйдёт за область данных
                      int32_t off = static_cast<int32_t>(r.Below(512)) - 256;
                      g.b_.t.regs[reg] = static_cast<uint32_t>(off);
                  }
                  g.b_.flagMask &= ~(F_OF | F_SF | F_AF | F_PF);
                  g.b_.name = "bt";
                  break; }
        case 3: g.Byte(0x0F); g.Byte(0xBA); g.ModRMAny(static_cast<uint8_t>(4 + r.Below(4))); g.Imm(r.Below(256), 1);
                g.b_.flagMask &= ~(F_OF | F_SF | F_AF | F_PF); g.b_.name = "btimm"; break;
        case 4: { static const uint8_t ops[] = {0xA4, 0xA5, 0xAC, 0xAD}; uint8_t op = ops[r.Below(4)];
                  g.Byte(0x0F); g.Byte(op); g.ModRMAny(static_cast<uint8_t>(r.Below(8)));
                  uint32_t count = r.Below(o16 ? 17 : 32);
                  if (op & 1) g.b_.t.regs[1] = (g.b_.t.regs[1] & ~0xFFu) | count; else g.Imm(count, 1);
                  uint32_t c = count & 31;
                  if (c) { g.b_.flagMask &= ~F_AF; if (c != 1) g.b_.flagMask &= ~F_OF; }
                  g.b_.name = "shxd"; break; }
        case 5: { uint8_t op = static_cast<uint8_t>(0xB0 + r.Below(2)); g.Byte(0x0F); g.Byte(op); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.b_.name = "cmpxchg"; break; }
        case 6: { static const uint8_t ops[] = {0xB6, 0xB7, 0xBE, 0xBF}; g.Byte(0x0F); g.Byte(ops[r.Below(4)]); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.b_.name = "movx"; break; }
        case 7: { g.Byte(0x0F); g.Byte(static_cast<uint8_t>(0xBC + r.Below(2))); g.ModRMAny(static_cast<uint8_t>(r.Below(8)));
                  g.b_.flagMask &= ~(F_CF | F_OF | F_SF | F_AF | F_PF); g.b_.name = "bsf"; break; }
        case 8: { g.Byte(0x0F); g.Byte(static_cast<uint8_t>(0xC0 + r.Below(2))); g.ModRMAny(static_cast<uint8_t>(r.Below(8))); g.b_.name = "xadd"; break; }
        case 9: { if (o16) { g.b_.t.codeLen = 0; } g.Byte(0x0F); g.Byte(0xC7); g.ModRMMem(1); g.b_.name = "cmpxchg8b"; break; }
        default: { if (o16) { g.b_.t.codeLen = 0; } g.Byte(0x0F); g.Byte(static_cast<uint8_t>(0xC8 + r.Below(8))); g.b_.name = "bswap"; break; }
    }
}

void GenString(Builder& g, Rng& r, bool) {
    static const uint8_t ops[] = {0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    uint8_t op = ops[r.Below(10)];
    bool rep = r.Chance(60);
    // префикс REP должен стоять до 0x66, порядок не важен для CPU
    uint8_t code0 = g.b_.t.code[0];
    bool hadOsz = g.b_.t.codeLen == 1 && code0 == 0x66;
    if (rep) {
        if (hadOsz) { g.b_.t.code[0] = r.Chance(50) ? 0xF3 : 0xF2; g.Byte(0x66); }
        else g.Byte(r.Chance(50) ? 0xF3 : 0xF2);
    }
    g.Byte(op);
    g.b_.t.regs[6] = ORACLE_DATA_BASE + 0x200 + r.Below(0x800);
    g.b_.t.regs[7] = ORACLE_DATA_BASE + 0x200 + r.Below(0x800);
    if (rep) g.b_.t.regs[1] = r.Below(24);
    g.b_.name = "string";
}

void GenMiscMem(Builder& g, Rng& r, bool o16) {
    switch (r.Below(5)) {
        case 0: g.Byte(0x8F); g.ModRMAny(0); g.b_.name = "poprm"; break;
        case 1: g.Byte(0x68); g.Imm(RandomValue(r), o16 ? 2 : 4); g.b_.name = "pushimm"; break;
        case 2: g.Byte(0x6A); g.Imm(RandomValue(r), 1); g.b_.name = "pushimm8"; break;
        case 3: g.b_.t.regs[3] = g.DataTarget(256); g.Byte(0xD7); g.b_.name = "xlat"; break;
        default: g.Byte(0xC8); g.Imm(r.Below(64) * 4, 2); g.Imm(r.Below(4), 1); g.b_.name = "enter"; break;
    }
}

// Переходы: цель — байт после маркера INC EAX, так что «перешёл/не
// перешёл» видно по EAX.
void GenBranch(Builder& g, Rng& r, bool o16) {
    if (o16) { g.b_.t.codeLen = 0; } // 16-битные переходы обрезают EIP — не тестируем
    uint32_t kind = r.Below(9);
    auto marker = [&]() { g.Byte(0x40); };
    switch (kind) {
        case 0: g.Byte(static_cast<uint8_t>(0x70 + r.Below(16))); g.Byte(1); marker(); g.b_.name = "jcc8"; break;
        case 1: g.Byte(0x0F); g.Byte(static_cast<uint8_t>(0x80 + r.Below(16))); g.Imm(1, 4); marker(); g.b_.name = "jcc32"; break;
        case 2: g.Byte(0xEB); g.Byte(1); marker(); g.b_.name = "jmp8"; break;
        case 3: g.Byte(0xE9); g.Imm(1, 4); marker(); g.b_.name = "jmp32"; break;
        case 4: g.Byte(0xE8); g.Imm(1, 4); marker(); g.b_.name = "call"; break;
        case 5: { uint8_t op = static_cast<uint8_t>(0xE0 + r.Below(4)); g.Byte(op); g.Byte(1); marker();
                  if (r.Chance(50)) g.b_.t.regs[1] = r.Below(3);
                  g.b_.name = "loop"; break; }
        case 6: { // RET / RET imm16: адрес возврата кладём на вершину стека
                  bool imm = r.Chance(50);
                  uint32_t len = imm ? 3 : 1;
                  uint32_t target = ORACLE_CODE_BASE + g.b_.t.codeLen + len + 1;
                  g.Patch(g.b_.t.regs[4], target);
                  g.Byte(imm ? 0xC2 : 0xC3);
                  if (imm) g.Imm(r.Below(16) * 4, 2);
                  marker();
                  g.b_.name = "ret";
                  break; }
        default: { // CALL/JMP r/m
                  uint8_t sub = kind == 7 ? 2 : 4;
                  g.Byte(0xFF);
                  uint8_t rm = static_cast<uint8_t>(r.Below(8));
                  if (r.Chance(40) && rm != 4) {
                      g.ModRMReg(sub, rm);
                      g.b_.t.regs[rm] = ORACLE_CODE_BASE + g.b_.t.codeLen + 1;
                  } else {
                      uint32_t ea = g.ModRMMem(sub);
                      g.Patch(ea, ORACLE_CODE_BASE + g.b_.t.codeLen + 1);
                  }
                  marker();
                  g.b_.name = sub == 2 ? "callrm" : "jmprm";
                  break; }
    }
}

using GenFn = void (*)(Builder&, Rng&, bool);

struct GenEntry { const char* group; GenFn fn; int weight; };

const GenEntry kIntGens[] = {
    {"int", GenAlu, 12}, {"int", GenGroup1, 8}, {"int", GenShift, 10}, {"int", GenGroup3, 8},
    {"int", GenIncDecPushPop, 6}, {"int", GenMov, 10}, {"int", GenImul, 4}, {"int", GenBcd, 3},
    {"int", GenString, 6}, {"int", Gen0F, 14}, {"int", GenMiscMem, 4}, {"int", GenBranch, 8},
};

// FPU/SSE генераторы — в fuzz_fp.inc (подключается ниже).
#include "fuzz_fp.inc"

bool BuildCase(Rng& r, Built& b, const std::string& filter) {
    std::vector<const GenEntry*> pool;
    int total = 0;
    auto add = [&](const GenEntry* e, size_t n) {
        for (size_t i = 0; i < n; ++i) { pool.push_back(&e[i]); total += e[i].weight; }
    };
    if (filter == "int" || filter == "all") add(kIntGens, sizeof(kIntGens) / sizeof(kIntGens[0]));
    if (filter == "fpu" || filter == "all") add(kFpuGens, sizeof(kFpuGens) / sizeof(kFpuGens[0]));
    if (filter == "sse" || filter == "all") add(kSseGens, sizeof(kSseGens) / sizeof(kSseGens[0]));
    if (pool.empty()) return false;
    int pick = static_cast<int>(r.Below(static_cast<uint32_t>(total)));
    const GenEntry* e = pool[0];
    for (const GenEntry* p : pool) { if (pick < p->weight) { e = p; break; } pick -= p->weight; }

    Builder g(r, b);
    bool o16 = std::string(e->group) == "int" && r.Chance(15);
    if (o16) g.Byte(0x66);
    e->fn(g, r, o16);
    return b.t.codeLen > (o16 ? 1u : 0u);
}

const char* StatusName(uint32_t s) {
    static const char* n[] = {"OK", "SEGV", "ILL", "FPE", "TRAP", "OTHER"};
    return s < 6 ? n[s] : "?";
}

uint32_t OurStatus(cpu::StepResult res, const cpu::CpuException& e) {
    if (res == cpu::StepResult::UnknownOpcode) return ORACLE_ILL;
    switch (e.code) {
        case cpu::EXC_ACCESS_VIOLATION:
        case cpu::EXC_PRIV_INSTRUCTION:
        case cpu::EXC_SOFTWARE_INTERRUPT: return ORACLE_SEGV;
        case cpu::EXC_ILLEGAL_INSTRUCTION: return ORACLE_ILL;
        case cpu::EXC_INT_DIVIDE_BY_ZERO:
        case cpu::EXC_INT_OVERFLOW: return ORACLE_FPE;
        case cpu::EXC_BREAKPOINT: return ORACLE_TRAP;
        default: return ORACLE_OTHER;
    }
}

struct OurRun {
    OracleResult r{};
    uint8_t data[ORACLE_DATA_SIZE];
};

void RunOurs(cpu::PagedGuestMemory& mem, const OracleCase& t, OurRun& out) {
    uint8_t data[ORACLE_DATA_SIZE];
    OracleFillData(data, &t);
    mem.LoadBytes(ORACLE_DATA_BASE, data, ORACLE_DATA_SIZE);
    uint8_t zeros[0x100] = {};
    mem.LoadBytes(ORACLE_CODE_BASE, zeros, sizeof(zeros));
    mem.LoadBytes(ORACLE_CODE_BASE, t.code, t.codeLen);

    cpu::CpuState c;
    std::memcpy(c.regs, t.regs, sizeof(c.regs));
    c.SetEflags(t.eflags);
    c.eip = ORACLE_CODE_BASE;
    for (int i = 0; i < 8; ++i) std::memcpy(c.xmm[i].d, t.xmmInit[i], 16);
    c.mxcsr = t.mxcsr ? t.mxcsr : 0x1F80;
    c.fpuControl = t.fpuControl ? t.fpuControl : 0x027F;

    cpu::Interpreter interp(mem);
    interp.lenientDivide = false;
    uint32_t end = ORACLE_CODE_BASE + t.codeLen;
    out.r = OracleResult{};
    out.r.status = ORACLE_OK;
    for (int steps = 0; c.eip != end; ++steps) {
        if (steps > 64) { out.r.status = ORACLE_OTHER; break; }
        cpu::StepResult res = interp.Step(c);
        if (res == cpu::StepResult::Continue) continue;
        if (res == cpu::StepResult::Halted) { out.r.status = ORACLE_OTHER; break; }
        out.r.status = OurStatus(res, interp.LastException());
        break;
    }
    std::memcpy(out.r.regs, c.regs, sizeof(out.r.regs));
    out.r.eflags = c.GetEflags();
    for (int i = 0; i < 8; ++i) std::memcpy(out.r.xmm[i], c.xmm[i].d, 16);
    out.r.mxcsr = c.mxcsr;
    out.r.fpuStatus = c.GetFpuStatusWord();
    out.r.fpuControl = c.fpuControl;
    out.r.fpuTag = c.fpuTag;
    mem.ReadBytes(ORACLE_DATA_BASE, out.data, ORACLE_DATA_SIZE);
    out.r.dataHash = OracleHash(out.data, ORACLE_DATA_SIZE);
}

std::string Hex(const uint8_t* p, uint32_t n) {
    std::string s;
    char buf[4];
    for (uint32_t i = 0; i < n; ++i) { std::snprintf(buf, sizeof(buf), "%02x ", p[i]); s += buf; }
    return s;
}

// Перезапускает один тест в оракуле с дампом памяти и показывает разницу.
std::string MemoryDiff(const char* oracle, const OracleCase& t, const uint8_t* ours) {
    char inPath[] = "/tmp/cpu_fuzz_oneXXXXXX";
    int fd = mkstemp(inPath);
    if (fd < 0) return "";
    FILE* f = fdopen(fd, "wb");
    fwrite(&t, sizeof(t), 1, f);
    fclose(f);
    std::string cmd = std::string(oracle) + " -d < " + inPath;
    FILE* p = popen(cmd.c_str(), "r");
    OracleResult r;
    static uint8_t data[ORACLE_DATA_SIZE];
    std::string out;
    if (p && fread(&r, sizeof(r), 1, p) == 1 && fread(data, ORACLE_DATA_SIZE, 1, p) == 1) {
        int n = 0;
        for (uint32_t i = 0; i < ORACLE_DATA_SIZE && n < 16; ++i) {
            if (data[i] != ours[i]) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "[+%03x ours=%02x cpu=%02x]", i, ours[i], data[i]);
                out += buf;
                ++n;
            }
        }
    }
    if (p) pclose(p);
    unlink(inPath);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <oracle> [count] [seed] [int|fpu|sse|all]\n", argv[0]);
        return 2;
    }
    const char* oracle = argv[1];
    int count = argc > 2 ? std::atoi(argv[2]) : 20000;
    uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 0) : 12345;
    std::string filter = argc > 4 ? argv[4] : "int";

    Rng rng{seed * 0x9E3779B97F4A7C15ull + 1};
    std::vector<Built> cases;
    cases.reserve(count);
    while (static_cast<int>(cases.size()) < count) {
        Built b;
        if (BuildCase(rng, b, filter)) cases.push_back(b);
    }

    char inPath[] = "/tmp/cpu_fuzz_inXXXXXX";
    int fd = mkstemp(inPath);
    if (fd < 0) { perror("mkstemp"); return 2; }
    FILE* f = fdopen(fd, "wb");
    for (const Built& b : cases) fwrite(&b.t, sizeof(b.t), 1, f);
    fclose(f);

    std::string cmd = std::string(oracle) + " < " + inPath;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { perror("popen"); return 2; }
    std::vector<OracleResult> results(cases.size());
    size_t got = fread(results.data(), sizeof(OracleResult), results.size(), p);
    pclose(p);
    if (got != results.size()) {
        std::fprintf(stderr, "oracle returned %zu of %zu results\n", got, results.size());
        unlink(inPath);
        return 2;
    }

    cpu::PagedGuestMemory mem;
    mem.Map(ORACLE_DATA_BASE, ORACLE_DATA_SIZE);
    mem.Map(ORACLE_CODE_BASE, 0x1000, cpu::kProtRWX);

    int mismatches = 0, shown = 0;
    std::map<std::string, int> byName, failByName;
    OurRun ours;
    for (size_t i = 0; i < cases.size(); ++i) {
        const Built& b = cases[i];
        const OracleResult& want = results[i];
        mem.EnableIntegerFastPath(true);RunOurs(mem, b.t, ours);
        OurRun reference;mem.EnableIntegerFastPath(false);RunOurs(mem,b.t,reference);
        if(std::memcmp(&ours.r,&reference.r,sizeof(OracleResult)) || std::memcmp(ours.data,reference.data,sizeof(ours.data))) {
            std::fprintf(stderr,"fast/reference differential failure case %zu\n",i);return 3;
        }

        byName[b.name]++;
        std::string why;
        if (ours.r.status != want.status) {
            why = std::string("status ours=") + StatusName(ours.r.status) + " cpu=" + StatusName(want.status);
        } else if (want.status == ORACLE_OK) {
            for (int k = 0; k < 8; ++k) {
                if (ours.r.regs[k] != want.regs[k]) {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "%s ours=%08x cpu=%08x; ", cpu::RegName(static_cast<cpu::Reg32>(k)), ours.r.regs[k], want.regs[k]);
                    why += buf;
                }
            }
            uint32_t m = b.flagMask;
            if ((ours.r.eflags & m) != (want.eflags & m)) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "flags ours=%03x cpu=%03x (mask %03x); ", ours.r.eflags & m, want.eflags & m, m);
                why += buf;
            }
            if (ours.r.dataHash != want.dataHash) why += "memory; ";
            if (b.compareXmm) {
                if (std::memcmp(ours.r.xmm, want.xmm, sizeof(want.xmm)) != 0) {
                    for (int k = 0; k < 8; ++k) if (std::memcmp(ours.r.xmm[k], want.xmm[k], 16)) {
                        char buf[160];
                        std::snprintf(buf, sizeof(buf), "xmm%d ours=%08x%08x%08x%08x cpu=%08x%08x%08x%08x; ", k,
                                      ours.r.xmm[k][3], ours.r.xmm[k][2], ours.r.xmm[k][1], ours.r.xmm[k][0],
                                      want.xmm[k][3], want.xmm[k][2], want.xmm[k][1], want.xmm[k][0]);
                        why += buf;
                    }
                }
                if ((ours.r.mxcsr & 0x3F) != (want.mxcsr & 0x3F)) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "mxcsr ours=%x cpu=%x; ", ours.r.mxcsr, want.mxcsr);
                    why += buf;
                }
            }
            if (b.compareFpu) {
                // C0..C3 и TOP; биты исключений сравниваются только IE/ZE (точность PE слишком тонкая)
                uint16_t sm = 0x4500 | 0x3800 | 0x0005;
                if ((ours.r.fpuStatus & sm) != (want.fpuStatus & sm)) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "fsw ours=%04x cpu=%04x; ", ours.r.fpuStatus, want.fpuStatus);
                    why += buf;
                }
            }
        }
        if (!why.empty()) {
            ++mismatches;
            failByName[b.name]++;
            if (shown < 40) {
                ++shown;
                if (why.find("memory") != std::string::npos) why += MemoryDiff(oracle, b.t, ours.data);
                std::printf("MISMATCH #%zu [%s] code: %s\n  %s\n", i, b.name.c_str(), Hex(b.t.code, b.t.codeLen).c_str(), why.c_str());
                if (b.compareFpu || b.compareXmm) {
                    uint8_t d[ORACLE_DATA_SIZE];
                    OracleFillData(d, &b.t);
                    double fa, fb; float ff;
                    std::memcpy(&fa, d + 0x100, 8); std::memcpy(&fb, d + 0x110, 8); std::memcpy(&ff, d + 0x110, 4);
                    uint64_t ua, ub; std::memcpy(&ua, &fa, 8); std::memcpy(&ub, &fb, 8);
                    std::printf("  A=%016llx (%.17g) B=%016llx (%.17g) Bf=%.9g fcw=%04x mxcsr=%04x\n",
                                (unsigned long long)ua, fa, (unsigned long long)ub, fb, ff, b.t.fpuControl, b.t.mxcsr);
                }
                std::printf("  in: eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x fl=%03x\n",
                            b.t.regs[0], b.t.regs[1], b.t.regs[2], b.t.regs[3], b.t.regs[4], b.t.regs[5], b.t.regs[6], b.t.regs[7], b.t.eflags);
            }
        }
    }
    unlink(inPath);

    int statusCount[6] = {};
    for (const OracleResult& rr : results) if (rr.status < 6) statusCount[rr.status]++;
    std::printf("\n%d тестов, расхождений: %d\n", static_cast<int>(cases.size()), mismatches);
    std::printf("исходы на CPU:");
    for (int k = 0; k < 6; ++k) std::printf(" %s=%d", StatusName(k), statusCount[k]);
    std::printf("\n");
    for (auto& kv : byName) {
        int fails = failByName.count(kv.first) ? failByName[kv.first] : 0;
        std::printf("  %-12s %6d%s\n", kv.first.c_str(), kv.second, fails ? (" FAIL " + std::to_string(fails)).c_str() : "");
    }
    std::printf("fast/reference parity: %zu cases\n",cases.size());
    return mismatches ? 1 : 0;
}

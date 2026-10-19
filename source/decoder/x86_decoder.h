#pragma once
// x86_decoder.h
//
// Собственный decoder x86-32 инструкций (без сторонних библиотек).
// Покрывает практичный подмножество, которое реально генерируют компиляторы
// (MSVC/GCC) для простого кода на C: пересылка данных, арифметика,
// сравнения, переходы, стек, вызовы функций.
//
// НЕ покрывает (пока): SSE/AVX, x87 FPU, строковые инструкции (movs/stos),
// префиксы (lock/rep), 16-битные операнды. Будем добавлять по мере того,
// как реальные .exe будут "спотыкаться" о неподдержанные инструкции.

#include <cstdint>
#include "../cpu/cpu_state.h"

namespace cpu {

enum class Mnemonic : uint8_t {
    Unknown = 0,
    Mov,
    Add,
    Sub,
    Cmp,
    Inc,
    Dec,
    Push,
    Pop,
    Jmp,
    Jcc,
    Call,
    Ret,
    Lea,
    Test,
    Xor,
    Or,
    And,
    Adc,
    Sbb,
    Movzx,
    Movsx,
    Setcc,
    Not,
    Neg,
    Leave,
    Shl,
    Shr,
    Sar,
    Rol,
    Ror,
    Imul,
    ImulImm3, // IMUL r32, r/m32, imm32 — 3-операндная форма (0x69): dst = src * imm32 (immediate — в instr.imm32Extra)
    Cdq,
    Mul,
    ImulImplicit,
    Div,
    Idiv,
    Nop,
    Stos,   // STOSB/STOSD — запись AL/EAX по [EDI], автоинкремент EDI (полагаем DF=0 — почти всегда так в реальном коде)
    Movs,   // MOVSB/MOVSD — копирование [ESI]->[EDI], автоинкремент обоих (тоже полагаем DF=0)
    Cmps,   // CMPSB/CMPSD — сравнение [ESI] и [EDI], автоинкремент обоих, с REPE — ранний выход при неравенстве
    Halt,   // 0xF4 HLT — используем как маркер конца программы в наших тестах
};

// Переопределение сегмента для memory-операндов (fs:/gs: — классика MSVC
// для доступа к Thread Information Block: SEH-цепочка, TLS, stack cookie)
enum class Segment : uint8_t { None, FS, GS };

// Условия переходов (Jcc) — значение соответствует младшему нибблу опкода
// как в 0x7X (Jcc rel8) так и в 0x0F8X (Jcc rel32)
enum class Condition : uint8_t {
    O = 0x0, NO = 0x1,
    B = 0x2, AE = 0x3,   // B/NAE/C , AE/NB/NC
    E = 0x4, NE = 0x5,   // E/Z , NE/NZ
    BE = 0x6, A = 0x7,   // BE/NA , A/NBE
    S = 0x8, NS = 0x9,
    P = 0xA, NP = 0xB,
    L = 0xC, GE = 0xD,   // L/NGE , GE/NL
    LE = 0xE, G = 0xF,   // LE/NG , G/NLE
    None = 0xFF,
};

struct Operand {
    enum class Kind : uint8_t { None, Register, Immediate, Memory } kind = Kind::None;

    Reg32 reg = Reg32::EAX;      // валиден при Kind::Register
    uint32_t immediate = 0;      // валиден при Kind::Immediate
    uint8_t width = 32;          // 32, 16 или 8
    bool highByte8 = false;      // валидно при width==8: true для AH/CH/DH/BH

    // Memory: effective_address = (hasBase ? reg[base] : 0)
    //                            + (hasIndex ? reg[index]*scale : 0)
    //                            + disp
    bool hasBase = false;
    Reg32 base = Reg32::EAX;
    bool hasIndex = false;
    Reg32 index = Reg32::EAX;
    uint8_t scale = 1;
    int32_t disp = 0;
    Segment segment = Segment::None; // валидно только при Kind::Memory

    static Operand MakeRegister(Reg32 r) {
        Operand o; o.kind = Kind::Register; o.reg = r; return o;
    }
    static Operand MakeImmediate(uint32_t v) {
        Operand o; o.kind = Kind::Immediate; o.immediate = v; return o;
    }
};

struct Instruction {
    Mnemonic mnemonic = Mnemonic::Unknown;
    Condition cc = Condition::None;   // для Jcc

    Operand dst;
    Operand src;

    uint8_t length = 0;         // длина инструкции в байтах (сколько продвинуть EIP)
    uint32_t branchTarget = 0;  // для прямых Jmp/Jcc/Call — уже вычисленный абсолютный адрес
    bool indirect = false;      // для Call/Jmp через регистр/память (напр. call [IAT_slot]) —
                                 // тогда реальная цель лежит в src и вычисляется в интерпретаторе
    bool hasImm16Cleanup = false; // для RET imm16
    uint16_t imm16Cleanup = 0;
    bool hasRepPrefix = false; // для Stos/Movs и т.п. — повторять ECX раз
    uint32_t imm32Extra = 0; // для ImulImm3 (0x69) — сама константа-множитель

    uint8_t rawOpcodeForDebug = 0; // на случай Unknown — что именно не распознали
};

// Декодирует одну инструкцию, начиная с байта по адресу code.
// currentEip нужен, чтобы посчитать абсолютный target для относительных переходов
// (rel8/rel32 отсчитываются от адреса СЛЕДУЮЩЕЙ инструкции, т.е. currentEip + length).
Instruction Decode(const uint8_t* code, uint32_t currentEip);

const char* ToString(Mnemonic m);
const char* ToString(Condition c);

} // namespace cpu

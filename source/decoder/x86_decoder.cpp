#include "x86_decoder.h"
#include <cstring>

namespace cpu {

const char* ToString(Mnemonic m) {
    switch (m) {
        case Mnemonic::Unknown: return "???";
        case Mnemonic::Mov:  return "mov";
        case Mnemonic::Add:  return "add";
        case Mnemonic::Sub:  return "sub";
        case Mnemonic::Cmp:  return "cmp";
        case Mnemonic::Inc:  return "inc";
        case Mnemonic::Dec:  return "dec";
        case Mnemonic::Push: return "push";
        case Mnemonic::Pop:  return "pop";
        case Mnemonic::Jmp:  return "jmp";
        case Mnemonic::Jcc:  return "j";
        case Mnemonic::Call: return "call";
        case Mnemonic::Ret:  return "ret";
        case Mnemonic::Lea:  return "lea";
        case Mnemonic::Test: return "test";
        case Mnemonic::Xor:  return "xor";
        case Mnemonic::Or:   return "or";
        case Mnemonic::And:  return "and";
        case Mnemonic::Adc:  return "adc";
        case Mnemonic::Sbb:  return "sbb";
        case Mnemonic::Movzx: return "movzx";
        case Mnemonic::Movsx: return "movsx";
        case Mnemonic::Setcc: return "set";
        case Mnemonic::Not:   return "not";
        case Mnemonic::Neg:   return "neg";
        case Mnemonic::Leave: return "leave";
        case Mnemonic::Shl:   return "shl";
        case Mnemonic::Shr:   return "shr";
        case Mnemonic::Sar:   return "sar";
        case Mnemonic::Rol:   return "rol";
        case Mnemonic::Ror:   return "ror";
        case Mnemonic::Imul:  return "imul";
        case Mnemonic::ImulImm3: return "imul3";
        case Mnemonic::Cdq:   return "cdq";
        case Mnemonic::Mul:   return "mul";
        case Mnemonic::ImulImplicit: return "imul";
        case Mnemonic::Div:   return "div";
        case Mnemonic::Idiv:  return "idiv";
        case Mnemonic::Nop:  return "nop";
        case Mnemonic::Stos: return "stos";
        case Mnemonic::Movs: return "movs";
        case Mnemonic::Cmps: return "cmps";
        case Mnemonic::Halt: return "hlt";
    }
    return "???";
}

const char* ToString(Condition c) {
    switch (c) {
        case Condition::O:  return "o";
        case Condition::NO: return "no";
        case Condition::B:  return "b";
        case Condition::AE: return "ae";
        case Condition::E:  return "e";
        case Condition::NE: return "ne";
        case Condition::BE: return "be";
        case Condition::A:  return "a";
        case Condition::S:  return "s";
        case Condition::NS: return "ns";
        case Condition::P:  return "p";
        case Condition::NP: return "np";
        case Condition::L:  return "l";
        case Condition::GE: return "ge";
        case Condition::LE: return "le";
        case Condition::G:  return "g";
        case Condition::None: return "";
    }
    return "";
}

namespace {

// Разбирает ModRM (и, если нужно, SIB + disp8/disp32) начиная с p[0].
// Возвращает операнд для поля rm и отдельно вынимает поле reg (вызывающий
// код сам решает, интерпретировать ли reg как регистр-операнд или как
// расширение опкода, например для группы 0x83/0x81).
struct ModRmParse {
    Operand rmOperand;
    uint8_t regField;
    uint8_t consumed; // сколько байт от начала modrm (включая сам modrm байт) съедено
};

ModRmParse ParseModRM(const uint8_t* p) {
    ModRmParse result{};
    uint8_t modrm = p[0];
    uint8_t mod = (modrm >> 6) & 0x3;
    uint8_t regField = (modrm >> 3) & 0x7;
    uint8_t rm = modrm & 0x7;
    size_t consumed = 1;

    result.regField = regField;

    Operand op;

    if (mod == 3) {
        op.kind = Operand::Kind::Register;
        op.reg = static_cast<Reg32>(rm);
        result.rmOperand = op;
        result.consumed = static_cast<uint8_t>(consumed);
        return result;
    }

    op.kind = Operand::Kind::Memory;

    bool hasSib = (rm == 4);
    bool dispOnlyNoBase = (mod == 0 && rm == 5);

    if (hasSib) {
        uint8_t sib = p[consumed]; consumed += 1;
        uint8_t scaleBits = (sib >> 6) & 0x3;
        uint8_t indexBits = (sib >> 3) & 0x7;
        uint8_t baseBits = sib & 0x7;

        if (indexBits != 4) { // 100 = "нет индекса"
            op.hasIndex = true;
            op.index = static_cast<Reg32>(indexBits);
            op.scale = static_cast<uint8_t>(1u << scaleBits);
        }

        if (baseBits == 5 && mod == 0) {
            // особый случай: нет base, дальше идёт disp32
            op.hasBase = false;
            int32_t disp32 = 0;
            std::memcpy(&disp32, p + consumed, 4);
            consumed += 4;
            op.disp = disp32;
        } else {
            op.hasBase = true;
            op.base = static_cast<Reg32>(baseBits);
        }
    } else if (dispOnlyNoBase) {
        op.hasBase = false;
        int32_t disp32 = 0;
        std::memcpy(&disp32, p + consumed, 4);
        consumed += 4;
        op.disp = disp32;
    } else {
        op.hasBase = true;
        op.base = static_cast<Reg32>(rm);
    }

    // disp8/disp32 к уже определённому base (кроме случаев, где disp уже
    // прочитан выше как единственная компонента адреса)
    if (mod == 1) {
        int8_t d8 = static_cast<int8_t>(p[consumed]);
        consumed += 1;
        op.disp += d8;
    } else if (mod == 2) {
        int32_t d32 = 0;
        std::memcpy(&d32, p + consumed, 4);
        consumed += 4;
        op.disp += d32;
    }

    result.rmOperand = op;
    result.consumed = static_cast<uint8_t>(consumed);
    return result;
}

uint32_t Read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

int32_t ReadS32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Классическая (без REX, чистый 32-битный режим) схема кодирования 8-битных
// регистров: 0-3 = AL/CL/DL/BL (младший байт), 4-7 = AH/CH/DH/BH (старший
// байт EAX/ECX/EDX/EBX). Это НЕ то же самое, что "младший байт ESP/EBP/ESI/EDI".
Operand MakeRegister8(uint8_t rawRegField) {
    Operand op;
    op.kind = Operand::Kind::Register;
    op.width = 8;
    if (rawRegField < 4) {
        op.reg = static_cast<Reg32>(rawRegField);
        op.highByte8 = false;
    } else {
        op.reg = static_cast<Reg32>(rawRegField - 4);
        op.highByte8 = true;
    }
    return op;
}

// Берёт уже распарсенный rm-операнд (регистр ИЛИ память) и приводит к 8-битному виду
Operand ToWidth8Operand(const Operand& rmOperand) {
    if (rmOperand.kind == Operand::Kind::Register) {
        return MakeRegister8(static_cast<uint8_t>(rmOperand.reg));
    }
    Operand mem = rmOperand;
    mem.width = 8;
    return mem;
}

} // namespace

// Основная логика декодирования одного опкода (без учёта префиксов —
// префиксы 0x66/0x64/0x65 снимаются в публичной Decode() ниже, которая
// вызывает эту функцию).
//
// opSize16 == true означает, что перед опкодом стоял префикс 0x66:
// для инструкций БЕЗ immediate-операнда (mov reg,reg / mov reg,mem / арифметика
// регистр-регистр и т.п.) это ничего не меняет в РАЗБОРЕ байт — просто потом
// операнды помечаются как 16-битные для интерпретатора. Но для инструкций,
// где immediate зашит в саму кодировку (mov r32,imm32 / mov r/m32,imm32 /
// group1 r/m32,imm32) под 0x66 реальный x86 читает imm16 вместо imm32 —
// мы это пока сознательно НЕ поддерживаем (чтобы не рисковать неверной
// длиной инструкции и рассинхронизацией) и честно возвращаем Unknown.
// Group2 (сдвиги/ротации): regField выбирает конкретную операцию.
// 2=RCL, 3=RCR (ротация через флаг carry) сознательно отложены — реже
// встречаются в обычном компилируемом C-коде, добавим при реальной необходимости.
Mnemonic Group2Mnemonic(uint8_t regField) {
    switch (regField) {
        case 0: return Mnemonic::Rol;
        case 1: return Mnemonic::Ror;
        case 4: return Mnemonic::Shl;
        case 5: return Mnemonic::Shr;
        case 6: return Mnemonic::Shl; // недокументированный дубль-алиас SHL/SAL
        case 7: return Mnemonic::Sar;
        default: return Mnemonic::Unknown;
    }
}

Instruction DecodeCore(const uint8_t* code, uint32_t currentEip, bool opSize16) {
    Instruction instr;
    uint8_t opcode = code[0];

    auto makeBranchTarget = [&](uint32_t instrLen, int32_t rel) {
        return currentEip + instrLen + static_cast<uint32_t>(rel);
    };

    // --- MOV r8, imm8 (0xB0 + reg) — короткая форма, которую предпочитает nasm/msvc ---
    if (opcode >= 0xB0 && opcode <= 0xB7) {
        instr.mnemonic = Mnemonic::Mov;
        instr.dst = MakeRegister8(static_cast<uint8_t>(opcode - 0xB0));
        instr.src = Operand::MakeImmediate(code[1]);
        instr.length = 2;
        return instr;
    }

    // --- MOV r32, imm32 (0xB8 + reg), или MOV r16, imm16 под 0x66 ---
    if (opcode >= 0xB8 && opcode <= 0xBF) {
        instr.mnemonic = Mnemonic::Mov;
        instr.dst = Operand::MakeRegister(static_cast<Reg32>(opcode - 0xB8));
        if (opSize16) {
            uint16_t imm16;
            std::memcpy(&imm16, code + 1, 2);
            instr.src = Operand::MakeImmediate(imm16);
            instr.length = 3;
        } else {
            instr.src = Operand::MakeImmediate(Read32(code + 1));
            instr.length = 5;
        }
        return instr;
    }

    // --- INC r32 (0x40 + reg) ---
    if (opcode >= 0x40 && opcode <= 0x47) {
        instr.mnemonic = Mnemonic::Inc;
        instr.dst = Operand::MakeRegister(static_cast<Reg32>(opcode - 0x40));
        instr.length = 1;
        return instr;
    }

    // --- DEC r32 (0x48 + reg) ---
    if (opcode >= 0x48 && opcode <= 0x4F) {
        instr.mnemonic = Mnemonic::Dec;
        instr.dst = Operand::MakeRegister(static_cast<Reg32>(opcode - 0x48));
        instr.length = 1;
        return instr;
    }

    // --- PUSH r32 (0x50 + reg) ---
    if (opcode >= 0x50 && opcode <= 0x57) {
        instr.mnemonic = Mnemonic::Push;
        instr.src = Operand::MakeRegister(static_cast<Reg32>(opcode - 0x50));
        instr.length = 1;
        return instr;
    }

    // --- POP r32 (0x58 + reg) ---
    if (opcode >= 0x58 && opcode <= 0x5F) {
        instr.mnemonic = Mnemonic::Pop;
        instr.dst = Operand::MakeRegister(static_cast<Reg32>(opcode - 0x58));
        instr.length = 1;
        return instr;
    }

    // --- Jcc rel8 (0x70 + cc) ---
    if (opcode >= 0x70 && opcode <= 0x7F) {
        instr.mnemonic = Mnemonic::Jcc;
        instr.cc = static_cast<Condition>(opcode - 0x70);
        int8_t rel = static_cast<int8_t>(code[1]);
        instr.length = 2;
        instr.branchTarget = makeBranchTarget(2, rel);
        return instr;
    }

    switch (opcode) {
        case 0x00: { // ADD r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Add;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x01: { // ADD r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Add;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x02: { // ADD r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Add;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x03: { // ADD r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Add;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x28: { // SUB r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sub;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x29: { // SUB r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sub;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x2A: { // SUB r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sub;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x2B: { // SUB r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sub;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x39: { // CMP r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Cmp;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x3B: { // CMP r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Cmp;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x38: { // CMP r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Cmp;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x3A: { // CMP r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Cmp;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x30: { // XOR r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Xor;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x31: { // XOR r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Xor;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x32: { // XOR r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Xor;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x84: { // TEST r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Test;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x85: { // TEST r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Test;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0xA4: { // MOVSB — [ES:EDI] = [DS:ESI] (байт), ESI/EDI += 1 (полагаем DF=0)
            instr.mnemonic = Mnemonic::Movs;
            instr.dst = MakeRegister8(0); // маркер размера: 8 бит
            instr.length = 1;
            return instr;
        }
        case 0xA5: { // MOVSD — [ES:EDI] = [DS:ESI] (двойное слово), ESI/EDI += 4 (полагаем DF=0)
            instr.mnemonic = Mnemonic::Movs;
            instr.dst = Operand::MakeRegister(Reg32::EAX); // маркер размера: 32 бита
            instr.length = 1;
            return instr;
        }
        case 0xA6: { // CMPSB — сравнить [ESI] и [EDI] (байт), ESI/EDI += 1 (полагаем DF=0)
            instr.mnemonic = Mnemonic::Cmps;
            instr.dst = MakeRegister8(0); // маркер размера: 8 бит
            instr.length = 1;
            return instr;
        }
        case 0xA7: { // CMPSD — сравнить [ESI] и [EDI] (двойное слово), ESI/EDI += 4 (полагаем DF=0).
                     // Найдено в paint.exe: "repe cmpsd" сравнивает распарсенный
                     // CLSID (CLSIDFromString) с ожидаемой константой в .rdata.
            instr.mnemonic = Mnemonic::Cmps;
            instr.dst = Operand::MakeRegister(Reg32::EAX); // маркер размера: 32 бита
            instr.length = 1;
            return instr;
        }
        case 0xAA: { // STOSB — [ES:EDI] = AL, EDI += 1 (полагаем DF=0)
            instr.mnemonic = Mnemonic::Stos;
            instr.dst = MakeRegister8(0); // AL — ширина 8 бит, используем как маркер размера
            instr.length = 1;
            return instr;
        }
        case 0xAB: { // STOSD — [ES:EDI] = EAX, EDI += 4 (полагаем DF=0)
            instr.mnemonic = Mnemonic::Stos;
            instr.dst = Operand::MakeRegister(Reg32::EAX); // ширина 32 бита — маркер размера
            instr.length = 1;
            return instr;
        }
        case 0xA8: { // TEST AL, imm8 — короткая форма без ModRM (найдено через
                     // реальный клик в winmine.exe, отсутствовала полностью)
            instr.mnemonic = Mnemonic::Test;
            instr.dst = MakeRegister8(0); // AL
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0xA9: { // TEST eAX, imm32 (или AX, imm16 под 0x66)
            instr.mnemonic = Mnemonic::Test;
            instr.dst = Operand::MakeRegister(Reg32::EAX);
            if (opSize16) {
                uint16_t imm16;
                std::memcpy(&imm16, code + 1, 2);
                instr.src = Operand::MakeImmediate(imm16);
                instr.length = 3;
            } else {
                instr.src = Operand::MakeImmediate(Read32(code + 1));
                instr.length = 5;
            }
            return instr;
        }
        case 0x10: { // ADC r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Adc;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x11: { // ADC r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Adc;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x12: { // ADC r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Adc;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x13: { // ADC r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Adc;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x18: { // SBB r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sbb;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x19: { // SBB r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sbb;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x1A: { // SBB r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sbb;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x1B: { // SBB r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Sbb;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x08: { // OR r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Or;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x09: { // OR r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Or;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x0A: { // OR r8, r/m8 — найдено через реальный клик в winmine.exe
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Or;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x0B: { // OR r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Or;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x20: { // AND r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::And;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x21: { // AND r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::And;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x22: { // AND r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::And;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x23: { // AND r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::And;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x33: { // XOR r32, r/m32 (обратное направление к уже имеющемуся 0x31)
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Xor;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x89: { // MOV r/m32, r32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x88: { // MOV r/m8, r8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(m.regField);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x8A: { // MOV r8, r/m8
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            instr.dst = MakeRegister8(m.regField);
            instr.src = ToWidth8Operand(m.rmOperand);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x8B: { // MOV r32, r/m32
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x8D: { // LEA r32, m
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Mnemonic::Lea;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand; // должен быть Memory (адрес не читаем, а вычисляем)
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            return instr;
        }
        case 0x04: { // ADD AL, imm8
            instr.mnemonic = Mnemonic::Add;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x0C: { // OR AL, imm8
            instr.mnemonic = Mnemonic::Or;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x14: { // ADC AL, imm8
            instr.mnemonic = Mnemonic::Adc;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x1C: { // SBB AL, imm8
            instr.mnemonic = Mnemonic::Sbb;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x24: { // AND AL, imm8
            instr.mnemonic = Mnemonic::And;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x2C: { // SUB AL, imm8
            instr.mnemonic = Mnemonic::Sub;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x34: { // XOR AL, imm8
            instr.mnemonic = Mnemonic::Xor;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x3C: { // CMP AL, imm8
            instr.mnemonic = Mnemonic::Cmp;
            instr.dst = MakeRegister8(0);
            instr.src = Operand::MakeImmediate(code[1]);
            instr.length = 2;
            return instr;
        }
        case 0x05: case 0x0D: case 0x15: case 0x1D:
        case 0x25: case 0x2D: case 0x35: case 0x3D: {
            // ADD/OR/ADC/SBB/AND/SUB/XOR/CMP EAX, imm32 (или AX,imm16 под 0x66)
            static const Mnemonic table[8] = {
                Mnemonic::Add, Mnemonic::Or, Mnemonic::Adc, Mnemonic::Sbb,
                Mnemonic::And, Mnemonic::Sub, Mnemonic::Xor, Mnemonic::Cmp
            };
            uint8_t idx = (opcode - 0x05) >> 3; // 0x05->0, 0x0D->1, ..., 0x3D->7
            instr.mnemonic = table[idx];
            instr.dst = Operand::MakeRegister(Reg32::EAX);
            if (opSize16) {
                uint16_t imm16;
                std::memcpy(&imm16, code + 1, 2);
                instr.src = Operand::MakeImmediate(imm16);
                instr.length = 3;
            } else {
                instr.src = Operand::MakeImmediate(Read32(code + 1));
                instr.length = 5;
            }
            return instr;
        }
        case 0x80: { // Group1 r/m8, imm8 — 8-битные add/or/adc/sbb/and/sub/xor/cmp
            auto m = ParseModRM(code + 1);
            uint8_t imm8 = code[1 + m.consumed];
            instr.length = static_cast<uint8_t>(1 + m.consumed + 1);

            if (m.rmOperand.kind == Operand::Kind::Register) {
                uint8_t rawRm = static_cast<uint8_t>(m.rmOperand.reg);
                instr.dst = MakeRegister8(rawRm);
            } else {
                Operand mem = m.rmOperand;
                mem.width = 8;
                instr.dst = mem;
            }
            instr.src = Operand::MakeImmediate(imm8); // без sign-extend — это честный 8-битный imm

            switch (m.regField) {
                case 0: instr.mnemonic = Mnemonic::Add; break;
                case 1: instr.mnemonic = Mnemonic::Or;  break;
                case 2: instr.mnemonic = Mnemonic::Adc; break;
                case 3: instr.mnemonic = Mnemonic::Sbb; break;
                case 4: instr.mnemonic = Mnemonic::And; break;
                case 5: instr.mnemonic = Mnemonic::Sub; break;
                case 6: instr.mnemonic = Mnemonic::Xor; break;
                case 7: instr.mnemonic = Mnemonic::Cmp; break;
                default:
                    instr.mnemonic = Mnemonic::Unknown;
                    instr.rawOpcodeForDebug = opcode;
                    break;
            }
            return instr;
        }
        case 0x83: { // Group1 r/m32, imm8 (sign-extended)
            auto m = ParseModRM(code + 1);
            int8_t imm8 = static_cast<int8_t>(code[1 + m.consumed]);
            uint32_t imm = static_cast<uint32_t>(static_cast<int32_t>(imm8));
            instr.length = static_cast<uint8_t>(1 + m.consumed + 1);
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeImmediate(imm);
            switch (m.regField) {
                case 0: instr.mnemonic = Mnemonic::Add; break;
                case 1: instr.mnemonic = Mnemonic::Or;  break;
                case 2: instr.mnemonic = Mnemonic::Adc; break;
                case 3: instr.mnemonic = Mnemonic::Sbb; break;
                case 4: instr.mnemonic = Mnemonic::And; break;
                case 5: instr.mnemonic = Mnemonic::Sub; break;
                case 6: instr.mnemonic = Mnemonic::Xor; break;
                case 7: instr.mnemonic = Mnemonic::Cmp; break;
                default:
                    instr.mnemonic = Mnemonic::Unknown;
                    instr.rawOpcodeForDebug = opcode;
                    break;
            }
            return instr;
        }
        case 0x69: { // IMUL r32, r/m32, imm32 — 3-операндная форма
            auto m = ParseModRM(code + 1);
            uint32_t imm = Read32(code + 1 + m.consumed);
            instr.mnemonic = Mnemonic::ImulImm3;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField)); // назначение — ТОЛЬКО запись, не читается как множитель
            instr.src = m.rmOperand; // первый множитель
            instr.imm32Extra = imm;  // второй множитель
            instr.length = static_cast<uint8_t>(1 + m.consumed + 4);
            return instr;
        }
        case 0x6B: { // IMUL r32, r/m32, imm8 — та же форма, но immediate 1 байт со знаковым расширением
            auto m = ParseModRM(code + 1);
            int8_t imm8 = static_cast<int8_t>(code[1 + m.consumed]);
            instr.mnemonic = Mnemonic::ImulImm3;
            instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
            instr.src = m.rmOperand;
            instr.imm32Extra = static_cast<uint32_t>(static_cast<int32_t>(imm8)); // знаковое расширение
            instr.length = static_cast<uint8_t>(1 + m.consumed + 1);
            return instr;
        }
        case 0x81: { // Group1 r/m32, imm32 (или r/m16, imm16 под 0x66)
            auto m = ParseModRM(code + 1);
            uint32_t imm;
            uint8_t immBytes;
            if (opSize16) {
                uint16_t imm16;
                std::memcpy(&imm16, code + 1 + m.consumed, 2);
                imm = imm16;
                immBytes = 2;
            } else {
                imm = Read32(code + 1 + m.consumed);
                immBytes = 4;
            }
            instr.length = static_cast<uint8_t>(1 + m.consumed + immBytes);
            instr.dst = m.rmOperand;
            instr.src = Operand::MakeImmediate(imm);
            switch (m.regField) {
                case 0: instr.mnemonic = Mnemonic::Add; break;
                case 1: instr.mnemonic = Mnemonic::Or;  break;
                case 2: instr.mnemonic = Mnemonic::Adc; break;
                case 3: instr.mnemonic = Mnemonic::Sbb; break;
                case 4: instr.mnemonic = Mnemonic::And; break;
                case 5: instr.mnemonic = Mnemonic::Sub; break;
                case 6: instr.mnemonic = Mnemonic::Xor; break;
                case 7: instr.mnemonic = Mnemonic::Cmp; break;
                default:
                    instr.mnemonic = Mnemonic::Unknown;
                    instr.rawOpcodeForDebug = opcode;
                    break;
            }
            return instr;
        }
        case 0xC6: { // Group11: MOV r/m8, imm8 (regField==0 — единственная валидная форма)
            auto m = ParseModRM(code + 1);
            uint8_t imm8 = code[1 + m.consumed];
            instr.length = static_cast<uint8_t>(1 + m.consumed + 1);
            if (m.regField == 0) {
                instr.mnemonic = Mnemonic::Mov;
                instr.dst = ToWidth8Operand(m.rmOperand);
                instr.src = Operand::MakeImmediate(imm8);
            } else {
                instr.mnemonic = Mnemonic::Unknown;
                instr.rawOpcodeForDebug = opcode;
            }
            return instr;
        }
        case 0xC7: { // Group11: MOV r/m32, imm32 (или r/m16,imm16 под 0x66; regField==0 — единственная валидная форма)
            auto m = ParseModRM(code + 1);
            uint32_t imm;
            uint8_t immBytes;
            if (opSize16) {
                uint16_t imm16;
                std::memcpy(&imm16, code + 1 + m.consumed, 2);
                imm = imm16;
                immBytes = 2;
            } else {
                imm = Read32(code + 1 + m.consumed);
                immBytes = 4;
            }
            instr.length = static_cast<uint8_t>(1 + m.consumed + immBytes);
            if (m.regField == 0) {
                instr.mnemonic = Mnemonic::Mov;
                instr.dst = m.rmOperand;
                instr.src = Operand::MakeImmediate(imm);
            } else {
                instr.mnemonic = Mnemonic::Unknown;
                instr.rawOpcodeForDebug = opcode;
            }
            return instr;
        }
        case 0xA0: { // MOV AL, moffs8
            uint32_t absAddr = Read32(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            instr.dst = MakeRegister8(0); // AL
            Operand mem;
            mem.kind = Operand::Kind::Memory;
            mem.width = 8;
            mem.hasBase = false;
            mem.hasIndex = false;
            mem.disp = static_cast<int32_t>(absAddr);
            instr.src = mem;
            instr.length = 5;
            return instr;
        }
        case 0xA2: { // MOV moffs8, AL
            uint32_t absAddr = Read32(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            Operand mem;
            mem.kind = Operand::Kind::Memory;
            mem.width = 8;
            mem.hasBase = false;
            mem.hasIndex = false;
            mem.disp = static_cast<int32_t>(absAddr);
            instr.dst = mem;
            instr.src = MakeRegister8(0); // AL
            instr.length = 5;
            return instr;
        }
        case 0xA1: { // MOV EAX, moffs32 — компактная форма без ModRM,
                      // абсолютный адрес зашит прямо в инструкцию.
                      // Частый паттерн для чтения глобальных переменных.
            uint32_t absAddr = Read32(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            instr.dst = Operand::MakeRegister(Reg32::EAX);
            Operand mem;
            mem.kind = Operand::Kind::Memory;
            mem.hasBase = false;
            mem.hasIndex = false;
            mem.disp = static_cast<int32_t>(absAddr);
            instr.src = mem;
            instr.length = 5;
            return instr;
        }
        case 0xA3: { // MOV moffs32, EAX — обратная форма (запись в глобальную переменную)
            uint32_t absAddr = Read32(code + 1);
            instr.mnemonic = Mnemonic::Mov;
            Operand mem;
            mem.kind = Operand::Kind::Memory;
            mem.hasBase = false;
            mem.hasIndex = false;
            mem.disp = static_cast<int32_t>(absAddr);
            instr.dst = mem;
            instr.src = Operand::MakeRegister(Reg32::EAX);
            instr.length = 5;
            return instr;
        }
        case 0xC0: { // Group2 (8-бит): rol/ror/shl/shr/sar, imm8
            auto m = ParseModRM(code + 1);
            uint8_t imm8 = code[1 + m.consumed];
            instr.mnemonic = Group2Mnemonic(m.regField);
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = Operand::MakeImmediate(imm8);
            instr.length = static_cast<uint8_t>(1 + m.consumed + 1);
            if (instr.mnemonic == Mnemonic::Unknown) instr.rawOpcodeForDebug = opcode;
            return instr;
        }
        case 0xC1: { // Group2 (32-бит, или 16-бит под 0x66): rol/ror/shl/shr/sar, imm8
            auto m = ParseModRM(code + 1);
            uint8_t imm8 = code[1 + m.consumed];
            instr.mnemonic = Group2Mnemonic(m.regField);
            instr.dst = m.rmOperand;
            if (opSize16) instr.dst.width = 16;
            instr.src = Operand::MakeImmediate(imm8);
            instr.length = static_cast<uint8_t>(1 + m.consumed + 1);
            if (instr.mnemonic == Mnemonic::Unknown) instr.rawOpcodeForDebug = opcode;
            return instr;
        }
        case 0xD0: { // Group2 (8-бит), сдвиг на 1
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Group2Mnemonic(m.regField);
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = Operand::MakeImmediate(1);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            if (instr.mnemonic == Mnemonic::Unknown) instr.rawOpcodeForDebug = opcode;
            return instr;
        }
        case 0xD1: { // Group2 (32/16-бит), сдвиг на 1
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Group2Mnemonic(m.regField);
            instr.dst = m.rmOperand;
            if (opSize16) instr.dst.width = 16;
            instr.src = Operand::MakeImmediate(1);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            if (instr.mnemonic == Mnemonic::Unknown) instr.rawOpcodeForDebug = opcode;
            return instr;
        }
        case 0xD2: { // Group2 (8-бит), сдвиг на CL
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Group2Mnemonic(m.regField);
            instr.dst = ToWidth8Operand(m.rmOperand);
            instr.src = MakeRegister8(1); // CL
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            if (instr.mnemonic == Mnemonic::Unknown) instr.rawOpcodeForDebug = opcode;
            return instr;
        }
        case 0xD3: { // Group2 (32/16-бит), сдвиг на CL
            auto m = ParseModRM(code + 1);
            instr.mnemonic = Group2Mnemonic(m.regField);
            instr.dst = m.rmOperand;
            if (opSize16) instr.dst.width = 16;
            instr.src = MakeRegister8(1); // CL
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            if (instr.mnemonic == Mnemonic::Unknown) instr.rawOpcodeForDebug = opcode;
            return instr;
        }
        case 0xF6: { // Group3 (8-бит): test/not/neg/mul/imul/div/idiv через regField
            auto m = ParseModRM(code + 1);
            Operand dst = ToWidth8Operand(m.rmOperand);
            switch (m.regField) {
                case 0: { // TEST r/m8, imm8
                    uint8_t imm8 = code[1 + m.consumed];
                    instr.mnemonic = Mnemonic::Test;
                    instr.dst = dst;
                    instr.src = Operand::MakeImmediate(imm8);
                    instr.length = static_cast<uint8_t>(1 + m.consumed + 1);
                    return instr;
                }
                case 2: // NOT r/m8
                    instr.mnemonic = Mnemonic::Not;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 3: // NEG r/m8
                    instr.mnemonic = Mnemonic::Neg;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 4: // MUL r/m8 — AX = AL * r/m8 (беззнаковое)
                    instr.mnemonic = Mnemonic::Mul;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 5: // IMUL r/m8 — AX = AL * r/m8 (знаковое)
                    instr.mnemonic = Mnemonic::ImulImplicit;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 6: // DIV r/m8 — AL:AH = AX / r/m8 (беззнаковое)
                    instr.mnemonic = Mnemonic::Div;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 7: // IDIV r/m8 — AL:AH = AX / r/m8 (знаковое)
                    instr.mnemonic = Mnemonic::Idiv;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                default:
                    instr.mnemonic = Mnemonic::Unknown;
                    instr.rawOpcodeForDebug = opcode;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
            }
        }
        case 0xF7: { // Group3 (32-бит, или 16-бит под 0x66)
            auto m = ParseModRM(code + 1);
            Operand dst = m.rmOperand;
            if (opSize16) dst.width = 16;
            switch (m.regField) {
                case 0: { // TEST r/m32, imm32 (или r/m16,imm16 под 0x66)
                    uint32_t imm;
                    uint8_t immBytes;
                    if (opSize16) {
                        uint16_t imm16;
                        std::memcpy(&imm16, code + 1 + m.consumed, 2);
                        imm = imm16;
                        immBytes = 2;
                    } else {
                        imm = Read32(code + 1 + m.consumed);
                        immBytes = 4;
                    }
                    instr.mnemonic = Mnemonic::Test;
                    instr.dst = dst;
                    instr.src = Operand::MakeImmediate(imm);
                    instr.length = static_cast<uint8_t>(1 + m.consumed + immBytes);
                    return instr;
                }
                case 2:
                    instr.mnemonic = Mnemonic::Not;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 3:
                    instr.mnemonic = Mnemonic::Neg;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 4: // MUL r/m32 — EDX:EAX = EAX * r/m32 (беззнаковое)
                    instr.mnemonic = Mnemonic::Mul;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 5: // IMUL r/m32 — EDX:EAX = EAX * r/m32 (знаковое)
                    instr.mnemonic = Mnemonic::ImulImplicit;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 6: // DIV r/m32 — EAX:EDX = EDX:EAX / r/m32 (беззнаковое)
                    instr.mnemonic = Mnemonic::Div;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                case 7: // IDIV r/m32 — EAX:EDX = EDX:EAX / r/m32 (знаковое)
                    instr.mnemonic = Mnemonic::Idiv;
                    instr.dst = dst;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
                default:
                    instr.mnemonic = Mnemonic::Unknown;
                    instr.rawOpcodeForDebug = opcode;
                    instr.length = static_cast<uint8_t>(1 + m.consumed);
                    return instr;
            }
        }
        case 0xFF: { // Group5: inc/dec/call/jmp/push через r/m32 —
                      // это КЛЮЧЕВОЙ опкод для вызова импортированных функций:
                      // "call dword ptr [IAT_slot]" кодируется именно так
            auto m = ParseModRM(code + 1);
            instr.length = static_cast<uint8_t>(1 + m.consumed);
            switch (m.regField) {
                case 0: // INC r/m32
                    instr.mnemonic = Mnemonic::Inc;
                    instr.dst = m.rmOperand;
                    break;
                case 1: // DEC r/m32
                    instr.mnemonic = Mnemonic::Dec;
                    instr.dst = m.rmOperand;
                    break;
                case 2: // CALL r/m32 (near, indirect) — вызов через IAT/vtable/регистр
                    instr.mnemonic = Mnemonic::Call;
                    instr.indirect = true;
                    instr.src = m.rmOperand; // цель вычисляется в рантайме
                    break;
                case 4: // JMP r/m32 (near, indirect)
                    instr.mnemonic = Mnemonic::Jmp;
                    instr.indirect = true;
                    instr.src = m.rmOperand;
                    break;
                case 6: // PUSH r/m32
                    instr.mnemonic = Mnemonic::Push;
                    instr.src = m.rmOperand;
                    break;
                default: // 3,5 = far call/jmp (не поддерживаем), 7 = зарезервировано
                    instr.mnemonic = Mnemonic::Unknown;
                    instr.rawOpcodeForDebug = opcode;
                    break;
            }
            return instr;
        }
        case 0x68: { // PUSH imm32
            instr.mnemonic = Mnemonic::Push;
            instr.src = Operand::MakeImmediate(Read32(code + 1));
            instr.length = 5;
            return instr;
        }
        case 0x6A: { // PUSH imm8 (sign-extended)
            instr.mnemonic = Mnemonic::Push;
            int8_t imm8 = static_cast<int8_t>(code[1]);
            instr.src = Operand::MakeImmediate(static_cast<uint32_t>(static_cast<int32_t>(imm8)));
            instr.length = 2;
            return instr;
        }
        case 0xE8: { // CALL rel32
            instr.mnemonic = Mnemonic::Call;
            int32_t rel = ReadS32(code + 1);
            instr.length = 5;
            instr.branchTarget = makeBranchTarget(5, rel);
            return instr;
        }
        case 0xE9: { // JMP rel32
            instr.mnemonic = Mnemonic::Jmp;
            int32_t rel = ReadS32(code + 1);
            instr.length = 5;
            instr.branchTarget = makeBranchTarget(5, rel);
            return instr;
        }
        case 0xEB: { // JMP rel8
            instr.mnemonic = Mnemonic::Jmp;
            int8_t rel = static_cast<int8_t>(code[1]);
            instr.length = 2;
            instr.branchTarget = makeBranchTarget(2, rel);
            return instr;
        }
        case 0x99: { // CDQ — знаковое расширение EAX в EDX:EAX (перед IDIV)
            instr.mnemonic = Mnemonic::Cdq;
            instr.length = 1;
            return instr;
        }
        case 0xC9: { // LEAVE — свёрнутые mov esp,ebp + pop ebp
            instr.mnemonic = Mnemonic::Leave;
            instr.length = 1;
            return instr;
        }
        case 0xC3: { // RET
            instr.mnemonic = Mnemonic::Ret;
            instr.length = 1;
            return instr;
        }
        case 0xC2: { // RET imm16
            instr.mnemonic = Mnemonic::Ret;
            uint16_t imm16;
            std::memcpy(&imm16, code + 1, 2);
            instr.hasImm16Cleanup = true;
            instr.imm16Cleanup = imm16;
            instr.length = 3;
            return instr;
        }
        case 0x90: { // NOP
            instr.mnemonic = Mnemonic::Nop;
            instr.length = 1;
            return instr;
        }
        case 0xF4: { // HLT — маркер конца программы в наших тестах
            instr.mnemonic = Mnemonic::Halt;
            instr.length = 1;
            return instr;
        }
        case 0x0F: { // двухбайтовые опкоды
            uint8_t opcode2 = code[1];
            if (opcode2 >= 0x80 && opcode2 <= 0x8F) { // Jcc rel32
                instr.mnemonic = Mnemonic::Jcc;
                instr.cc = static_cast<Condition>(opcode2 - 0x80);
                int32_t rel = ReadS32(code + 2);
                instr.length = 6;
                instr.branchTarget = makeBranchTarget(6, rel);
                return instr;
            }
            if (opcode2 == 0xB6 || opcode2 == 0xB7 ||  // MOVZX r32, r/m8 или r/m16
                opcode2 == 0xBE || opcode2 == 0xBF) {  // MOVSX r32, r/m8 или r/m16
                auto m = ParseModRM(code + 2);
                bool is8 = (opcode2 == 0xB6 || opcode2 == 0xBE);
                bool zeroExtend = (opcode2 == 0xB6 || opcode2 == 0xB7);

                instr.mnemonic = zeroExtend ? Mnemonic::Movzx : Mnemonic::Movsx;
                instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField)); // всегда 32-бит

                if (m.rmOperand.kind == Operand::Kind::Register) {
                    // regField в rmOperand.reg сейчас хранит "сырое" значение rm (0-7),
                    // т.к. ParseModRM формировал его в 32-битном предположении —
                    // переинтерпретируем как 8- или 16-битный регистр-источник
                    uint8_t rawRm = static_cast<uint8_t>(m.rmOperand.reg);
                    if (is8) {
                        instr.src = MakeRegister8(rawRm);
                    } else {
                        Operand r16 = m.rmOperand;
                        r16.width = 16;
                        instr.src = r16;
                    }
                } else {
                    Operand mem = m.rmOperand;
                    mem.width = is8 ? 8 : 16;
                    instr.src = mem;
                }
                instr.length = static_cast<uint8_t>(2 + m.consumed);
                return instr;
            }
            if (opcode2 >= 0x90 && opcode2 <= 0x9F) { // SETcc r/m8
                auto m = ParseModRM(code + 2);
                instr.mnemonic = Mnemonic::Setcc;
                instr.cc = static_cast<Condition>(opcode2 - 0x90);
                instr.dst = ToWidth8Operand(m.rmOperand);
                instr.length = static_cast<uint8_t>(2 + m.consumed);
                return instr;
            }
            if (opcode2 == 0xAF) { // IMUL r32, r/m32 — "современная" двухоперандная форма
                auto m = ParseModRM(code + 2);
                instr.mnemonic = Mnemonic::Imul;
                instr.dst = Operand::MakeRegister(static_cast<Reg32>(m.regField));
                instr.src = m.rmOperand;
                instr.length = static_cast<uint8_t>(2 + m.consumed);
                return instr;
            }
            instr.mnemonic = Mnemonic::Unknown;
            instr.rawOpcodeForDebug = opcode;
            instr.length = 2;
            return instr;
        }
        default: {
            instr.mnemonic = Mnemonic::Unknown;
            instr.rawOpcodeForDebug = opcode;
            instr.length = 1;
            return instr;
        }
    }
}

// Публичная точка входа: снимает префиксы (сегмент fs:/gs:, размер операнда
// 0x66), затем зовёт DecodeCore на реальном опкоде.
Instruction Decode(const uint8_t* code, uint32_t currentEip) {
    Segment seg = Segment::None;
    bool opSize16 = false;
    bool repPrefix = false;
    uint32_t prefixBytes = 0;

    // Реальный x86 допускает несколько префиксов подряd в любом порядке —
    // снимаем их в цикле, пока не упрёмся в настоящий опкод
    while (true) {
        uint8_t b = code[prefixBytes];
        if (b == 0x64) { seg = Segment::FS; prefixBytes++; continue; }
        if (b == 0x65) { seg = Segment::GS; prefixBytes++; continue; }
        if (b == 0x66) { opSize16 = true; prefixBytes++; continue; }
        if (b == 0xF3) { repPrefix = true; prefixBytes++; continue; } // REP/REPE — нужен для Stos/Movs
        break;
    }

    Instruction instr = DecodeCore(code + prefixBytes, currentEip + prefixBytes, opSize16);
    instr.length = static_cast<uint8_t>(instr.length + prefixBytes);
    instr.hasRepPrefix = repPrefix;

    if (seg != Segment::None) {
        if (instr.dst.kind == Operand::Kind::Memory) instr.dst.segment = seg;
        if (instr.src.kind == Operand::Kind::Memory) instr.src.segment = seg;
    }

    if (opSize16 && instr.mnemonic != Mnemonic::Unknown) {
        // Помечаем операнды как 16-битные — но только если decoder ещё не
        // выставил ширину явно сам (напр. movzx/0x80 всегда работают с
        // 8-битными операндами независимо от этого префикса).
        if (instr.dst.kind != Operand::Kind::None && instr.dst.width == 32) instr.dst.width = 16;
        if (instr.src.kind != Operand::Kind::None && instr.src.width == 32) instr.src.width = 16;
    }

    return instr;
}

} // namespace cpu

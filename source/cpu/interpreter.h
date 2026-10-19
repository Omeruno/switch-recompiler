#pragma once
// interpreter.h
//
// Простой interpreter-style исполнитель декодированных x86-инструкций.
// "Простой" в смысле архитектуры (не JIT) — но исполняет настоящий,
// декодированный из реальных байт код, один-в-один.

#include "cpu_state.h"
#include "guest_memory.h"
#include "../decoder/x86_decoder.h"

namespace cpu {

enum class StepResult {
    Continue,
    Halted,       // встретили HLT — наш маркер конца программы
    UnknownOpcode,
};

class Interpreter {
public:
    // fsBase/gsBase — адрес нашего "фейкового" TEB (Thread Environment Block).
    // Настоящий Windows TEB — сложная структура, нам пока нужно только
    // несколько первых полей (SEH-цепочка, границы стека) — этого достаточно,
    // чтобы CRT-код инициализации структурной обработки исключений не падал.
    explicit Interpreter(IGuestMemory& memory, uint32_t fsBase = 0, uint32_t gsBase = 0)
        : memory_(memory), fsBase_(fsBase), gsBase_(gsBase) {}

    // Выполняет одну инструкцию по адресу cpu.eip, обновляет cpu.eip сама
    // (либо на length, либо на branchTarget при переходе/вызове/возврате).
    StepResult Step(CpuState& cpu, Instruction* outInstr = nullptr) {
        const uint8_t* code = memory_.RawPointer(cpu.eip);
        Instruction instr = Decode(code, cpu.eip);
        if (outInstr) *outInstr = instr;

        if (instr.mnemonic == Mnemonic::Unknown) {
            return StepResult::UnknownOpcode;
        }
        if (instr.mnemonic == Mnemonic::Halt) {
            return StepResult::Halted;
        }

        Execute(instr, cpu);
        return StepResult::Continue;
    }

private:
    IGuestMemory& memory_;
    uint32_t fsBase_ = 0;
    uint32_t gsBase_ = 0;

    uint32_t EffectiveAddress(const Operand& op, const CpuState& cpu) const {
        uint32_t addr = static_cast<uint32_t>(op.disp);
        if (op.hasBase)  addr += cpu.Get(op.base);
        if (op.hasIndex) addr += cpu.Get(op.index) * op.scale;
        if (op.segment == Segment::FS) addr += fsBase_;
        else if (op.segment == Segment::GS) addr += gsBase_;
        return addr;
    }

    uint32_t ReadOperand(const Operand& op, const CpuState& cpu) const {
        switch (op.kind) {
            case Operand::Kind::Register:
                if (op.width == 8) {
                    uint32_t regValue = cpu.Get(op.reg);
                    return op.highByte8 ? ((regValue >> 8) & 0xFF) : (regValue & 0xFF);
                }
                return op.width == 16 ? (cpu.Get(op.reg) & 0xFFFF) : cpu.Get(op.reg);
            case Operand::Kind::Immediate:
                return op.immediate;
            case Operand::Kind::Memory:
                if (op.width == 8)  return memory_.Read8(EffectiveAddress(op, cpu));
                if (op.width == 16) return memory_.Read16(EffectiveAddress(op, cpu));
                return memory_.Read32(EffectiveAddress(op, cpu));
            case Operand::Kind::None:
                return 0;
        }
        return 0;
    }

    void WriteOperand(const Operand& op, CpuState& cpu, uint32_t value) {
        switch (op.kind) {
            case Operand::Kind::Register:
                if (op.width == 8) {
                    // 8-битная запись меняет только соответствующий байт —
                    // AL/CL/DL/BL (младший) или AH/CH/DH/BH (второй байт),
                    // остальные три байта регистра не трогаем.
                    uint32_t cur = cpu.Get(op.reg);
                    if (op.highByte8) {
                        cpu.Get(op.reg) = (cur & 0xFFFF00FFu) | ((value & 0xFFu) << 8);
                    } else {
                        cpu.Get(op.reg) = (cur & 0xFFFFFF00u) | (value & 0xFFu);
                    }
                } else if (op.width == 16) {
                    // 16-битная запись меняет только младшие 16 бит регистра —
                    // так ведёт себя настоящее железо (mov ax,... не трогает верхние биты eax)
                    cpu.Get(op.reg) = (cpu.Get(op.reg) & 0xFFFF0000u) | (value & 0xFFFFu);
                } else {
                    cpu.Get(op.reg) = value;
                }
                return;
            case Operand::Kind::Memory:
                if (op.width == 8) {
                    memory_.Write8(EffectiveAddress(op, cpu), static_cast<uint8_t>(value));
                } else if (op.width == 16) {
                    memory_.Write16(EffectiveAddress(op, cpu), static_cast<uint16_t>(value));
                } else {
                    memory_.Write32(EffectiveAddress(op, cpu), value);
                }
                return;
            default:
                return; // immediate/none — писать некуда, это баг вызывающего кода
        }
    }

    // Флаги для ADD: result = a + b. width — 8/16/32, влияет на позицию
    // знакового бита и на то, что считается переполнением.
    static void UpdateFlagsAdd(EFlags& f, uint32_t a, uint32_t b, uint32_t result, uint8_t width = 32) {
        uint32_t mask = (width == 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
        uint32_t signBit = 1u << (width - 1);
        uint32_t maskedResult = result & mask;
        f.zf = (maskedResult == 0);
        f.sf = (maskedResult & signBit) != 0;
        // Для 32-бит result уже "обёрнут" через uint32_t-арифметику на месте
        // вызова (переполнение произошло ДО того, как мы его увидели) —
        // поэтому перенос детектируется через классический "result < a".
        // Для 8/16-бит result считается в 32-битном intermediate БЕЗ обёртки,
        // поэтому там перенос — это просто "вышли за пределы своей разрядности".
        f.cf = (width == 32) ? (result < a) : (result > mask);
        bool signA = (a & signBit) != 0;
        bool signB = (b & signBit) != 0;
        bool signR = (maskedResult & signBit) != 0;
        f.of = (signA == signB) && (signR != signA); // знаковое переполнение
    }

    // Флаги для SUB/CMP: result = a - b
    static void UpdateFlagsSub(EFlags& f, uint32_t a, uint32_t b, uint32_t result, uint8_t width = 32) {
        uint32_t mask = (width == 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
        uint32_t signBit = 1u << (width - 1);
        uint32_t maskedResult = result & mask;
        f.zf = (maskedResult == 0);
        f.sf = (maskedResult & signBit) != 0;
        f.cf = a < b; // беззнаковый заём (a,b уже в диапазоне своей разрядности)
        bool signA = (a & signBit) != 0;
        bool signB = (b & signBit) != 0;
        bool signR = (maskedResult & signBit) != 0;
        f.of = (signA != signB) && (signR != signA);
    }

    static void UpdateFlagsLogical(EFlags& f, uint32_t result, uint8_t width = 32) {
        uint32_t mask = (width == 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
        uint32_t signBit = 1u << (width - 1);
        uint32_t maskedResult = result & mask;
        f.zf = (maskedResult == 0);
        f.sf = (maskedResult & signBit) != 0;
        f.cf = false;
        f.of = false;
    }

    static bool EvaluateCondition(Condition cc, const EFlags& f) {
        switch (cc) {
            case Condition::E:  return f.zf;
            case Condition::NE: return !f.zf;
            case Condition::L:  return f.sf != f.of;
            case Condition::GE: return f.sf == f.of;
            case Condition::LE: return f.zf || (f.sf != f.of);
            case Condition::G:  return !f.zf && (f.sf == f.of);
            case Condition::B:  return f.cf;
            case Condition::AE: return !f.cf;
            case Condition::BE: return f.cf || f.zf;
            case Condition::A:  return !f.cf && !f.zf;
            case Condition::S:  return f.sf;
            case Condition::NS: return !f.sf;
            case Condition::O:  return f.of;
            case Condition::NO: return !f.of;
            case Condition::P:
            case Condition::NP:
                // Parity flag не отслеживается в нашей минимальной реализации
                FatalError("Condition P/NP не поддерживается (parity flag не отслеживается)");
            case Condition::None:
                FatalError("EvaluateCondition вызван без условия");
        }
        return false;
    }

    void Execute(const Instruction& instr, CpuState& cpu) {
        switch (instr.mnemonic) {
            case Mnemonic::Mov: {
                uint32_t value = ReadOperand(instr.src, cpu);
                WriteOperand(instr.dst, cpu, value);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Add: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint32_t result = a + b;
                UpdateFlagsAdd(cpu.flags, a, b, result, instr.dst.width);
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Sub: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint32_t result = a - b;
                UpdateFlagsSub(cpu.flags, a, b, result, instr.dst.width);
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Imul: {
                // Двухоперандная форма (0x0F 0xAF): dst = dst * src (знаковое),
                // результат усекается до 32 бит. CF/OF = "не влезло в 32 бита";
                // SF/ZF по факту недокументированы, но выставляем разумно.
                int32_t a = static_cast<int32_t>(ReadOperand(instr.dst, cpu));
                int32_t b = static_cast<int32_t>(ReadOperand(instr.src, cpu));
                int64_t fullProduct = static_cast<int64_t>(a) * static_cast<int64_t>(b);
                int32_t result = static_cast<int32_t>(fullProduct);

                bool overflowed = (fullProduct != static_cast<int64_t>(result));
                cpu.flags.cf = overflowed;
                cpu.flags.of = overflowed;
                cpu.flags.zf = (result == 0);
                cpu.flags.sf = (result < 0);

                WriteOperand(instr.dst, cpu, static_cast<uint32_t>(result));
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::ImulImm3: {
                // IMUL r32, r/m32, imm32 — dst = src(r/m) * imm32 (знаковое).
                // В отличие от 2-операндной формы, dst тут ТОЛЬКО назначение
                // для записи — не читается как один из множителей.
                int32_t a = static_cast<int32_t>(ReadOperand(instr.src, cpu));
                int32_t b = static_cast<int32_t>(instr.imm32Extra);
                int64_t fullProduct = static_cast<int64_t>(a) * static_cast<int64_t>(b);
                int32_t result = static_cast<int32_t>(fullProduct);

                bool overflowed = (fullProduct != static_cast<int64_t>(result));
                cpu.flags.cf = overflowed;
                cpu.flags.of = overflowed;
                cpu.flags.zf = (result == 0);
                cpu.flags.sf = (result < 0);

                WriteOperand(instr.dst, cpu, static_cast<uint32_t>(result));
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Cdq: {
                // Знаковое расширение EAX в EDX:EAX — готовит операнды для IDIV
                uint32_t eax = cpu.Get(Reg32::EAX);
                cpu.Get(Reg32::EDX) = (eax & 0x80000000u) ? 0xFFFFFFFFu : 0u;
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Mul: {
                // Однооперандный MUL: неявный множитель — AL (8-бит) или EAX (32-бит),
                // результат — в AX или EDX:EAX соответственно
                uint8_t width = instr.dst.width;
                uint32_t operand = ReadOperand(instr.dst, cpu);
                if (width == 8) {
                    uint32_t al = cpu.Get(Reg32::EAX) & 0xFFu;
                    uint32_t product = al * (operand & 0xFFu);
                    cpu.Get(Reg32::EAX) = (cpu.Get(Reg32::EAX) & 0xFFFF0000u) | (product & 0xFFFFu);
                    bool overflow = (product > 0xFFu);
                    cpu.flags.cf = overflow;
                    cpu.flags.of = overflow;
                } else {
                    uint64_t product = static_cast<uint64_t>(cpu.Get(Reg32::EAX)) * static_cast<uint64_t>(operand);
                    cpu.Get(Reg32::EAX) = static_cast<uint32_t>(product);
                    cpu.Get(Reg32::EDX) = static_cast<uint32_t>(product >> 32);
                    bool overflow = (cpu.Get(Reg32::EDX) != 0);
                    cpu.flags.cf = overflow;
                    cpu.flags.of = overflow;
                }
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::ImulImplicit: {
                uint8_t width = instr.dst.width;
                if (width == 8) {
                    int32_t al = static_cast<int8_t>(cpu.Get(Reg32::EAX) & 0xFFu);
                    int32_t operand = static_cast<int8_t>(ReadOperand(instr.dst, cpu) & 0xFFu);
                    int32_t product = al * operand;
                    cpu.Get(Reg32::EAX) = (cpu.Get(Reg32::EAX) & 0xFFFF0000u) | (static_cast<uint32_t>(product) & 0xFFFFu);
                    bool overflow = (product < -128 || product > 127);
                    cpu.flags.cf = overflow;
                    cpu.flags.of = overflow;
                } else {
                    int64_t a = static_cast<int32_t>(cpu.Get(Reg32::EAX));
                    int64_t b = static_cast<int32_t>(ReadOperand(instr.dst, cpu));
                    int64_t product = a * b;
                    cpu.Get(Reg32::EAX) = static_cast<uint32_t>(product);
                    cpu.Get(Reg32::EDX) = static_cast<uint32_t>(product >> 32);
                    int32_t low = static_cast<int32_t>(product);
                    bool overflow = (product != static_cast<int64_t>(low));
                    cpu.flags.cf = overflow;
                    cpu.flags.of = overflow;
                }
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Div: {
                uint8_t width = instr.dst.width;
                uint32_t divisor = ReadOperand(instr.dst, cpu);
                if (divisor == 0) {
                    // См. подробный комментарий у IDIV — та же защитная мера.
                    if (cpu::g_consoleAvailable) {
                        std::printf("[DIV] Деление на ноль — вероятно, незаполненное поле эмуляции. EIP=0x%x\n", cpu.eip);
                    }
                    cpu.Get(Reg32::EAX) = 0;
                    cpu.Get(Reg32::EDX) = 0;
                    cpu.eip += instr.length;
                    return;
                }
                if (width == 8) {
                    uint32_t dividend = cpu.Get(Reg32::EAX) & 0xFFFFu; // AX
                    uint32_t quotient = dividend / divisor;
                    uint32_t remainder = dividend % divisor;
                    if (quotient > 0xFFu) FatalError("DIV: частное не влезло в AL (#DE)");
                    cpu.Get(Reg32::EAX) = (cpu.Get(Reg32::EAX) & 0xFFFF0000u) | (quotient & 0xFFu) | ((remainder & 0xFFu) << 8);
                } else {
                    uint64_t dividend = (static_cast<uint64_t>(cpu.Get(Reg32::EDX)) << 32) | cpu.Get(Reg32::EAX);
                    uint64_t quotient = dividend / divisor;
                    uint64_t remainder = dividend % divisor;
                    if (quotient > 0xFFFFFFFFull) FatalError("DIV: частное не влезло в EAX (#DE)");
                    cpu.Get(Reg32::EAX) = static_cast<uint32_t>(quotient);
                    cpu.Get(Reg32::EDX) = static_cast<uint32_t>(remainder);
                }
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Idiv: {
                uint8_t width = instr.dst.width;
                if (width == 8) {
                    int32_t divisor = static_cast<int8_t>(ReadOperand(instr.dst, cpu) & 0xFFu);
                    if (divisor == 0) {
                        // ЗАЩИТНАЯ МЕРА (не настоящая x86-семантика #DE!):
                        // почти всегда означает, что КАКОЕ-ТО поле в нашей
                        // эмуляции осталось незаполненным (0) там, где
                        // реальная Windows дала бы осмысленное значение
                        // (например DPI/метрики экрана) — а не то, что сама
                        // программа реально пытается поделить на ноль.
                        // Логируем и продолжаем с нулевым результатом,
                        // вместо жёсткого падения — как и с RaiseException.
                        if (cpu::g_consoleAvailable) {
                            std::printf("[IDIV8] Деление на ноль — вероятно, незаполненное поле эмуляции. EIP=0x%x\n", cpu.eip);
                        }
                        cpu.Get(Reg32::EAX) &= 0xFFFF0000u; // AL=0, AH=0
                        cpu.eip += instr.length;
                        return;
                    }
                    int32_t dividend = static_cast<int16_t>(cpu.Get(Reg32::EAX) & 0xFFFFu); // AX
                    int32_t quotient = dividend / divisor;
                    int32_t remainder = dividend % divisor;
                    if (quotient < -128 || quotient > 127) FatalError("IDIV: частное не влезло в AL (#DE)");
                    cpu.Get(Reg32::EAX) = (cpu.Get(Reg32::EAX) & 0xFFFF0000u)
                        | (static_cast<uint32_t>(quotient) & 0xFFu)
                        | ((static_cast<uint32_t>(remainder) & 0xFFu) << 8);
                } else {
                    int32_t divisor = static_cast<int32_t>(ReadOperand(instr.dst, cpu));
                    if (divisor == 0) {
                        // См. комментарий выше (8-битная ветка) — та же защитная мера.
                        if (cpu::g_consoleAvailable) {
                            std::printf("[IDIV32] Деление на ноль — вероятно, незаполненное поле эмуляции. EIP=0x%x\n", cpu.eip);
                        }
                        cpu.Get(Reg32::EAX) = 0;
                        cpu.Get(Reg32::EDX) = 0;
                        cpu.eip += instr.length;
                        return;
                    }
                    // Сначала собираем 64-битное делимое как БЕЗЗНАКОВОЕ (сдвиг
                    // отрицательного int64_t влево — undefined behavior в C++!),
                    // и только потом реинтерпретируем как знаковое.
                    uint64_t dividendBits = (static_cast<uint64_t>(cpu.Get(Reg32::EDX)) << 32)
                                          | static_cast<uint64_t>(cpu.Get(Reg32::EAX));
                    int64_t dividend = static_cast<int64_t>(dividendBits);
                    int64_t quotient = dividend / divisor;   // C++ округляет к нулю — совпадает с IDIV
                    int64_t remainder = dividend % divisor;
                    if (quotient < INT32_MIN || quotient > INT32_MAX) {
                        FatalError("IDIV: частное не влезло в EAX (#DE)");
                    }
                    cpu.Get(Reg32::EAX) = static_cast<uint32_t>(static_cast<int32_t>(quotient));
                    cpu.Get(Reg32::EDX) = static_cast<uint32_t>(static_cast<int32_t>(remainder));
                }
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Cmp: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint32_t result = a - b;
                UpdateFlagsSub(cpu.flags, a, b, result, instr.dst.width); // не пишем результат — это и есть cmp
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Xor: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint32_t result = a ^ b;
                UpdateFlagsLogical(cpu.flags, result, instr.dst.width);
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Or: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint32_t result = a | b;
                UpdateFlagsLogical(cpu.flags, result, instr.dst.width);
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::And: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint32_t result = a & b;
                UpdateFlagsLogical(cpu.flags, result, instr.dst.width);
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Adc: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint64_t wide = static_cast<uint64_t>(a) + b + (cpu.flags.cf ? 1 : 0);
                uint32_t result = static_cast<uint32_t>(wide);
                uint32_t mask = (instr.dst.width == 32) ? 0xFFFFFFFFu : ((1u << instr.dst.width) - 1u);
                bool newCf = (instr.dst.width == 32) ? (wide > 0xFFFFFFFFull) : (result > mask);
                UpdateFlagsAdd(cpu.flags, a, b, result, instr.dst.width); // SF/ZF/OF по обычной add-логике
                cpu.flags.cf = newCf;                     // CF считаем честно, с переносом
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Sbb: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint64_t bPlusCarry = static_cast<uint64_t>(b) + (cpu.flags.cf ? 1 : 0);
                uint32_t result = static_cast<uint32_t>(static_cast<uint64_t>(a) - bPlusCarry);
                bool newCf = static_cast<uint64_t>(a) < bPlusCarry;
                UpdateFlagsSub(cpu.flags, a, b, result, instr.dst.width); // SF/ZF/OF по обычной sub-логике
                cpu.flags.cf = newCf;
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Test: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t b = ReadOperand(instr.src, cpu);
                uint32_t result = a & b;
                UpdateFlagsLogical(cpu.flags, result, instr.dst.width); // не пишем — это и есть test
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Not: {
                // NOT — единственная арифметическая инструкция x86, которая
                // принципиально НЕ трогает флаги вообще (ни один из них)
                uint32_t a = ReadOperand(instr.dst, cpu);
                WriteOperand(instr.dst, cpu, ~a);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Neg: {
                // NEG r/m == "0 - r/m" — переиспользуем логику флагов SUB,
                // она даёт корректный CF (единица всегда, кроме случая value==0)
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t result = static_cast<uint32_t>(0) - a;
                UpdateFlagsSub(cpu.flags, 0, a, result, instr.dst.width);
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Shl: {
                uint8_t width = instr.dst.width;
                uint32_t mask = (width == 8) ? 0xFFu : (width == 16) ? 0xFFFFu : 0xFFFFFFFFu;
                uint32_t signBit = (width == 8) ? 0x80u : (width == 16) ? 0x8000u : 0x80000000u;
                uint32_t val = ReadOperand(instr.dst, cpu) & mask;
                uint32_t count = ReadOperand(instr.src, cpu) & 0x1Fu; // x86 маскирует счётчик 5 битами

                if (count != 0) {
                    uint32_t result = val;
                    bool cf = cpu.flags.cf;
                    for (uint32_t i = 0; i < count; i++) {
                        cf = (result & signBit) != 0;
                        result = (result << 1) & mask;
                    }
                    cpu.flags.cf = cf;
                    bool sf = (result & signBit) != 0;
                    cpu.flags.sf = sf;
                    cpu.flags.zf = (result == 0);
                    if (count == 1) cpu.flags.of = (sf != cf);
                    WriteOperand(instr.dst, cpu, result);
                }
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Shr: {
                uint8_t width = instr.dst.width;
                uint32_t mask = (width == 8) ? 0xFFu : (width == 16) ? 0xFFFFu : 0xFFFFFFFFu;
                uint32_t signBit = (width == 8) ? 0x80u : (width == 16) ? 0x8000u : 0x80000000u;
                uint32_t val = ReadOperand(instr.dst, cpu) & mask;
                uint32_t count = ReadOperand(instr.src, cpu) & 0x1Fu;

                if (count != 0) {
                    bool ofForOne = (val & signBit) != 0; // OF для 1-битного SHR = исходный старший бит
                    uint32_t result = val;
                    bool cf = cpu.flags.cf;
                    for (uint32_t i = 0; i < count; i++) {
                        cf = (result & 1) != 0;
                        result >>= 1;
                    }
                    cpu.flags.cf = cf;
                    cpu.flags.sf = (result & signBit) != 0;
                    cpu.flags.zf = (result == 0);
                    if (count == 1) cpu.flags.of = ofForOne;
                    WriteOperand(instr.dst, cpu, result);
                }
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Sar: {
                uint8_t width = instr.dst.width;
                uint32_t mask = (width == 8) ? 0xFFu : (width == 16) ? 0xFFFFu : 0xFFFFFFFFu;
                uint32_t signBit = (width == 8) ? 0x80u : (width == 16) ? 0x8000u : 0x80000000u;
                uint32_t raw = ReadOperand(instr.dst, cpu) & mask;
                uint32_t count = ReadOperand(instr.src, cpu) & 0x1Fu;

                if (count != 0) {
                    // Трактуем как знаковое число нужной ширины для арифметического сдвига
                    int32_t signedVal;
                    if (width == 8)       signedVal = static_cast<int8_t>(raw);
                    else if (width == 16) signedVal = static_cast<int16_t>(raw);
                    else                   signedVal = static_cast<int32_t>(raw);

                    bool cf = cpu.flags.cf;
                    int32_t result = signedVal;
                    for (uint32_t i = 0; i < count; i++) {
                        cf = (result & 1) != 0;
                        result >>= 1; // арифметический сдвиг для знакового типа — гарантированно на GCC/Clang
                    }
                    uint32_t maskedResult = static_cast<uint32_t>(result) & mask;
                    cpu.flags.cf = cf;
                    cpu.flags.sf = (maskedResult & signBit) != 0;
                    cpu.flags.zf = (maskedResult == 0);
                    if (count == 1) cpu.flags.of = false; // OF всегда 0 для SAR при сдвиге на 1
                    WriteOperand(instr.dst, cpu, maskedResult);
                }
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Rol: {
                uint8_t width = instr.dst.width;
                uint32_t mask = (width == 8) ? 0xFFu : (width == 16) ? 0xFFFFu : 0xFFFFFFFFu;
                uint32_t signBit = (width == 8) ? 0x80u : (width == 16) ? 0x8000u : 0x80000000u;
                uint32_t bits = (width == 8) ? 8u : (width == 16) ? 16u : 32u;
                uint32_t val = ReadOperand(instr.dst, cpu) & mask;
                uint32_t count = ReadOperand(instr.src, cpu) % bits;

                uint32_t result = val;
                for (uint32_t i = 0; i < count; i++) {
                    bool top = (result & signBit) != 0;
                    result = ((result << 1) | (top ? 1u : 0u)) & mask;
                }
                if (count != 0) cpu.flags.cf = (result & 1) != 0;
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Ror: {
                uint8_t width = instr.dst.width;
                uint32_t mask = (width == 8) ? 0xFFu : (width == 16) ? 0xFFFFu : 0xFFFFFFFFu;
                uint32_t signBit = (width == 8) ? 0x80u : (width == 16) ? 0x8000u : 0x80000000u;
                uint32_t bits = (width == 8) ? 8u : (width == 16) ? 16u : 32u;
                uint32_t val = ReadOperand(instr.dst, cpu) & mask;
                uint32_t count = ReadOperand(instr.src, cpu) % bits;

                uint32_t result = val;
                for (uint32_t i = 0; i < count; i++) {
                    bool bottom = (result & 1) != 0;
                    result = (result >> 1) | (bottom ? signBit : 0u);
                    result &= mask;
                }
                if (count != 0) cpu.flags.cf = (result & signBit) != 0;
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Inc: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t result = a + 1;
                // Реальный x86 INC не трогает CF — сохраняем текущий cf, обновляем остальное
                bool savedCf = cpu.flags.cf;
                UpdateFlagsAdd(cpu.flags, a, 1, result, instr.dst.width);
                cpu.flags.cf = savedCf;
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Dec: {
                uint32_t a = ReadOperand(instr.dst, cpu);
                uint32_t result = a - 1;
                bool savedCf = cpu.flags.cf;
                UpdateFlagsSub(cpu.flags, a, 1, result, instr.dst.width);
                cpu.flags.cf = savedCf;
                WriteOperand(instr.dst, cpu, result);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Lea: {
                uint32_t addr = EffectiveAddress(instr.src, cpu);
                WriteOperand(instr.dst, cpu, addr);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Movzx: {
                // Нулевое расширение: ReadOperand уже возвращает значение,
                // замаскированное по ширине источника (8 или 16 бит) —
                // просто пишем его как есть в 32-битный dst.
                uint32_t value = ReadOperand(instr.src, cpu);
                WriteOperand(instr.dst, cpu, value);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Movsx: {
                uint32_t raw = ReadOperand(instr.src, cpu);
                int32_t signExtended;
                if (instr.src.width == 8) {
                    signExtended = static_cast<int32_t>(static_cast<int8_t>(raw & 0xFF));
                } else { // 16
                    signExtended = static_cast<int32_t>(static_cast<int16_t>(raw & 0xFFFF));
                }
                WriteOperand(instr.dst, cpu, static_cast<uint32_t>(signExtended));
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Setcc: {
                uint32_t value = EvaluateCondition(instr.cc, cpu.flags) ? 1u : 0u;
                WriteOperand(instr.dst, cpu, value);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Push: {
                uint32_t value = ReadOperand(instr.src, cpu);
                cpu.ESP() -= 4;
                memory_.Write32(cpu.ESP(), value);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Pop: {
                uint32_t value = memory_.Read32(cpu.ESP());
                cpu.ESP() += 4;
                WriteOperand(instr.dst, cpu, value);
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Jmp: {
                cpu.eip = instr.indirect ? ReadOperand(instr.src, cpu) : instr.branchTarget;
                return;
            }
            case Mnemonic::Jcc: {
                if (EvaluateCondition(instr.cc, cpu.flags)) {
                    cpu.eip = instr.branchTarget; // Jcc всегда прямой (rel8/rel32), indirect не бывает
                } else {
                    cpu.eip += instr.length;
                }
                return;
            }
            case Mnemonic::Call: {
                uint32_t returnAddr = cpu.eip + instr.length;
                uint32_t target = instr.indirect ? ReadOperand(instr.src, cpu) : instr.branchTarget;
                cpu.ESP() -= 4;
                memory_.Write32(cpu.ESP(), returnAddr);
                cpu.eip = target;
                return;
            }
            case Mnemonic::Leave: {
                // LEAVE == mov esp,ebp; pop ebp — типичный эпилог функции
                cpu.ESP() = cpu.EBP();
                uint32_t savedEbp = memory_.Read32(cpu.ESP());
                cpu.ESP() += 4;
                cpu.EBP() = savedEbp;
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Ret: {
                uint32_t returnAddr = memory_.Read32(cpu.ESP());
                cpu.ESP() += 4;
                if (instr.hasImm16Cleanup) {
                    cpu.ESP() += instr.imm16Cleanup;
                }
                cpu.eip = returnAddr;
                return;
            }
            case Mnemonic::Stos: {
                // STOSB/STOSD — записываем AL/EAX по адресу [EDI], сдвигаем
                // EDI на 1 или 4 байта. С префиксом REP — повторяем ECX раз
                // (реальный x86: пока ECX!=0, каждый раз ECX--), без REP —
                // ровно один раз. Полагаем DF=0 (инкремент, флага
                // направления у нас пока нет — почти всегда так в реальном
                // скомпилированном коде).
                uint32_t count = instr.hasRepPrefix ? cpu.Get(Reg32::ECX) : 1;
                uint32_t edi = cpu.Get(Reg32::EDI);
                if (instr.dst.width == 8) {
                    uint8_t al = static_cast<uint8_t>(cpu.Get(Reg32::EAX) & 0xFFu);
                    for (uint32_t i = 0; i < count; i++) {
                        memory_.Write8(edi, al);
                        edi += 1;
                    }
                } else {
                    uint32_t eax = cpu.Get(Reg32::EAX);
                    for (uint32_t i = 0; i < count; i++) {
                        memory_.Write32(edi, eax);
                        edi += 4;
                    }
                }
                cpu.Get(Reg32::EDI) = edi;
                if (instr.hasRepPrefix) cpu.Get(Reg32::ECX) = 0;
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Movs: {
                // MOVSB/MOVSD — копируем [ESI] в [EDI], сдвигаем ОБА на 1
                // или 4 байта. С REP — повторяем ECX раз. Полагаем DF=0.
                uint32_t count = instr.hasRepPrefix ? cpu.Get(Reg32::ECX) : 1;
                uint32_t esi = cpu.Get(Reg32::ESI);
                uint32_t edi = cpu.Get(Reg32::EDI);
                if (instr.dst.width == 8) {
                    for (uint32_t i = 0; i < count; i++) {
                        memory_.Write8(edi, memory_.Read8(esi));
                        esi += 1; edi += 1;
                    }
                } else {
                    for (uint32_t i = 0; i < count; i++) {
                        memory_.Write32(edi, memory_.Read32(esi));
                        esi += 4; edi += 4;
                    }
                }
                cpu.Get(Reg32::ESI) = esi;
                cpu.Get(Reg32::EDI) = edi;
                if (instr.hasRepPrefix) cpu.Get(Reg32::ECX) = 0;
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Cmps: {
                // CMPSB/CMPSD — сравниваем [ESI] и [EDI], сдвигаем ОБА на 1
                // или 4 байта, флаги выставляются ПО ПОСЛЕДНЕМУ сравнению
                // (как обычный cmp: result=a-b). С префиксом (у нас всегда
                // REPE/REP, других не парсим) — повторяем, ПОКА ECX!=0 И
                // ZF=1 (равны); как только встретили неравенство — сразу
                // выходим, НЕ докручивая ECX до конца (в отличие от Movs/
                // Stos, которые всегда отрабатывают полный count). Без
                // префикса — ровно одно сравнение.
                uint32_t esi = cpu.Get(Reg32::ESI);
                uint32_t edi = cpu.Get(Reg32::EDI);
                uint8_t width = instr.dst.width;
                uint32_t step = (width == 8) ? 1u : 4u;
                if (instr.hasRepPrefix) {
                    uint32_t count = cpu.Get(Reg32::ECX);
                    while (count > 0) {
                        uint32_t a = (width == 8) ? memory_.Read8(esi) : memory_.Read32(esi);
                        uint32_t b = (width == 8) ? memory_.Read8(edi) : memory_.Read32(edi);
                        uint32_t result = a - b;
                        UpdateFlagsSub(cpu.flags, a, b, result, width);
                        esi += step; edi += step; count--;
                        if (!cpu.flags.zf) break; // REPE: встретили неравенство — стоп
                    }
                    cpu.Get(Reg32::ECX) = count;
                } else {
                    uint32_t a = (width == 8) ? memory_.Read8(esi) : memory_.Read32(esi);
                    uint32_t b = (width == 8) ? memory_.Read8(edi) : memory_.Read32(edi);
                    uint32_t result = a - b;
                    UpdateFlagsSub(cpu.flags, a, b, result, width);
                    esi += step; edi += step;
                }
                cpu.Get(Reg32::ESI) = esi;
                cpu.Get(Reg32::EDI) = edi;
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Nop: {
                cpu.eip += instr.length;
                return;
            }
            case Mnemonic::Halt:
            case Mnemonic::Unknown:
                // Обрабатывается на уровне Step(), сюда не должны попасть
                return;
        }
    }
};

} // namespace cpu

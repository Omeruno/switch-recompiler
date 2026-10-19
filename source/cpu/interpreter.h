#pragma once
// interpreter.h
//
// Интерпретатор IA-32 (пользовательский режим): декодирует и исполняет
// по одной инструкции настоящий машинный код гостя.
//
// Ошибки гостя (обращение к невыделенной памяти, деление на ноль,
// неизвестная инструкция, INT3, ...) не валят процесс: Step() возвращает
// StepResult::Exception, состояние CPU откатывается к началу сбойной
// инструкции, а в CpuException лежит код исключения Windows. Дальше
// решает вызывающий код: передать исключение в SEH гостя или показать
// диагностику.
//
// Корректность проверяется сверкой с настоящим процессором
// (tests/cpu_oracle): одни и те же инструкции исполняются нативно на
// хосте x86 и в интерпретаторе, состояния сравниваются.

#include <cstdint>
#include "cpu_state.h"
#include "guest_memory.h"
#include "../decoder/x86_decoder.h"

namespace cpu {

enum class StepResult {
    Continue,
    Halted,        // HLT — маркер конца программы в наших тестах
    UnknownOpcode, // неизвестная инструкция (также сообщается как Exception-код ниже)
    Exception,     // см. Interpreter::LastException()
};

// Коды исключений Windows (NTSTATUS)
constexpr uint32_t EXC_ACCESS_VIOLATION      = 0xC0000005u;
constexpr uint32_t EXC_ILLEGAL_INSTRUCTION   = 0xC000001Du;
constexpr uint32_t EXC_INT_DIVIDE_BY_ZERO    = 0xC0000094u;
constexpr uint32_t EXC_INT_OVERFLOW          = 0xC0000095u;
constexpr uint32_t EXC_PRIV_INSTRUCTION      = 0xC0000096u;
constexpr uint32_t EXC_BREAKPOINT            = 0x80000003u;
constexpr uint32_t EXC_SINGLE_STEP           = 0x80000004u;
constexpr uint32_t EXC_FLT_DIVIDE_BY_ZERO    = 0xC000008Eu;
constexpr uint32_t EXC_FLT_INVALID_OPERATION = 0xC0000090u;
constexpr uint32_t EXC_FLT_STACK_CHECK       = 0xC0000092u;
// Не исключение Windows: INT n с n != 3 (системные вызовы старых ОС и т.п.)
constexpr uint32_t EXC_SOFTWARE_INTERRUPT    = 0xE0000001u;

struct CpuException {
    uint32_t code = 0;
    uint32_t eip = 0;          // адрес сбойной инструкции
    uint32_t faultAddress = 0; // для ACCESS_VIOLATION
    bool write = false;        // для ACCESS_VIOLATION
    uint8_t vector = 0;        // для INT n
};

class Interpreter {
public:
    // fsBase/gsBase — адрес нашего "фейкового" TEB (Thread Environment Block).
    explicit Interpreter(IGuestMemory& memory, uint32_t fsBase = 0, uint32_t gsBase = 0)
        : memory_(memory), fsBase_(fsBase), gsBase_(gsBase) {}

    // Выполняет одну инструкцию по адресу cpu.eip.
    StepResult Step(CpuState& cpu, Instruction* outInstr = nullptr);

    const CpuException& LastException() const { return exception_; }

    // Совместимость со старым поведением: деление на ноль не бросает
    // исключение, а даёт 0 (так было в первой версии, и под это уже
    // отлажены Сапёр и Paint). Включено по умолчанию, пока нет SEH.
    bool lenientDivide = true;

private:
    IGuestMemory& memory_;
    uint32_t fsBase_ = 0;
    uint32_t gsBase_ = 0;
    CpuException exception_;
};

} // namespace cpu

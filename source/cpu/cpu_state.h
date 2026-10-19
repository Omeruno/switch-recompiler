#pragma once
// cpu_state.h
//
// Состояние 32-битного x86 CPU: регистры общего назначения, флаги, EIP.
// Индексы регистров совпадают с их кодировкой в ModRM/opcode x86 —
// это не совпадение, а специально сделано, чтобы decoder мог просто
// использовать 3-битное поле reg/rm напрямую как индекс массива.

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

struct EFlags {
    bool cf = false; // Carry Flag
    bool zf = false; // Zero Flag
    bool sf = false; // Sign Flag
    bool of = false; // Overflow Flag
    // PF, AF и остальные пока не нужны для нашего набора инструкций
};

struct CpuState {
    uint32_t regs[8] = {0};  // индексируется через Reg32
    uint32_t eip = 0;
    EFlags flags;

    uint32_t& Get(Reg32 r)       { return regs[static_cast<uint8_t>(r) & 7]; }
    uint32_t  Get(Reg32 r) const { return regs[static_cast<uint8_t>(r) & 7]; }

    uint32_t& ESP() { return regs[static_cast<uint8_t>(Reg32::ESP)]; }
    uint32_t& EBP() { return regs[static_cast<uint8_t>(Reg32::EBP)]; }
};

} // namespace cpu

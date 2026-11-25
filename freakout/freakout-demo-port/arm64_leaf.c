#include "cpu.h"

enum { GUEST_STACK = 0x100 };
uint8_t *guest_memory;

void L_006660D0(CPU *cpu);

uint64_t check_leaf(uint8_t *memory)
{
    guest_memory = memory;
    CPU cpu;
    cpu.esp = GUEST_STACK;
    cpu.af = 0;
    cpu.of = 0;
    L_006660D0(&cpu);
    uint32_t flags = (cpu.sf << 7) | (cpu.zf << 6) | (cpu.pf << 2) | cpu.cf;
    return ((uint64_t)flags << 32) | cpu.eax;
}

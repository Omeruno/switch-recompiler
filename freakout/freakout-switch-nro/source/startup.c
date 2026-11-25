#include "cpu.h"

enum {
    STACK = 0x90000, RESULT = 0x800, IAT_GET_MODULE_HANDLE = 0x6A5090,
    MODULE_MARKER = 0x100000, CRT_MARKER = 0x100010,
    MODULE_BASE = 0x400000, IAT_SET_APP_TYPE = 0x6A5384, CONTINUATION = 0x5AF615,
};
uint8_t *guest_memory;
uint32_t g_image_delta;

void L_005AF59E(CPU *cpu);
void L_005AF84C(CPU *cpu);

void dispatch(CPU *cpu, uint32_t address)
{
    if (address == 0x5AF84C) {
        L_005AF84C(cpu);
    } else if (address == MODULE_MARKER || address == CRT_MARKER) {
        uint32_t result = address == MODULE_MARKER ? RESULT : RESULT + 24;
        wr32(result, address);
        wr32(result + 4, rd32(cpu->esp));
        wr32(result + 8, rd32(cpu->esp + 4));
        wr32(result + 12, cpu->esp);
        wr32(result + 16, cpu->ebp);
        wr32(result + 20, rd32(cpu->fs_base));
        if (address == MODULE_MARKER) {
            if (rd32(cpu->esp + 4) != 0)
                __builtin_trap();
            cpu->eax = MODULE_BASE;
            cpu->esp += 8;
        }
    } else if (address != CONTINUATION) {
        __builtin_trap();
    }
}

void check_startup(uint8_t *memory)
{
    guest_memory = memory;
    CPU cpu = {0};
    cpu.esp = STACK;
    wr32(0, UINT32_MAX);
    wr32(IAT_GET_MODULE_HANDLE, MODULE_MARKER);
    wr32(IAT_SET_APP_TYPE, CRT_MARKER);
    L_005AF59E(&cpu);
}

void L_006660D0(CPU *cpu);

uint64_t check_leaf(uint8_t *memory, uint32_t a, uint32_t b)
{
    guest_memory = memory;
    CPU cpu = {0};
    cpu.esp = 0x100;
    wr32(0x104, 0x200);
    wr32(0x208, a);
    wr32(0x20C, b);
    L_006660D0(&cpu);
    uint32_t flags = (cpu.sf << 7) | (cpu.zf << 6) | (cpu.pf << 2) | cpu.cf;
    return ((uint64_t)flags << 32) | cpu.eax;
}

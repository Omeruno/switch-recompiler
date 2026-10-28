#include "source/cpu/cpu_state.h"
#include "source/cpu/guest_memory.h"
#include "source/cpu/interpreter.h"
#include "source/decoder/x86_decoder.h"
#include "source/win32/host_functions.h"

#include <cstdio>
#include <fstream>
#include <vector>
#include <cstring>

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

void WriteWideString(cpu::FlatGuestMemory& mem, uint32_t addr, const char* ascii) {
    size_t len = std::strlen(ascii);
    for (size_t i = 0; i < len; i++) {
        mem.Write16(addr + static_cast<uint32_t>(i) * 2, static_cast<uint16_t>(ascii[i]));
    }
    mem.Write16(addr + static_cast<uint32_t>(len) * 2, 0);
}

int main() {
    constexpr uint32_t CODE_BASE = 0x1000;
    cpu::FlatGuestMemory mem(CODE_BASE, 0x8000);

    auto code = ReadFile("test10.bin");
    mem.LoadBytes(CODE_BASE, code.data(), code.size());
    // Historical test10 requested samDesired=0 then wrote/read the value.
    // Request real QUERY_VALUE|SET_VALUE rights for that round-trip.
    mem.Write8(CODE_BASE+0x0d,3);

    // Поднимаем "IAT"
    int idxCreate = win32::FindHostFunction("ADVAPI32.dll", "RegCreateKeyExW");
    int idxSet    = win32::FindHostFunction("ADVAPI32.dll", "RegSetValueExW");
    int idxQuery  = win32::FindHostFunction("ADVAPI32.dll", "RegQueryValueExW");
    int idxClose  = win32::FindHostFunction("ADVAPI32.dll", "RegCloseKey");

    printf("Индексы: create=%d set=%d query=%d close=%d\n", idxCreate, idxSet, idxQuery, idxClose);
    if (idxCreate < 0 || idxSet < 0 || idxQuery < 0 || idxClose < 0) {
        printf("!! Не все функции нашлись\n");
        return 1;
    }

    mem.Write32(0x5000, win32::IndexToHostAddress(idxCreate));
    mem.Write32(0x5004, win32::IndexToHostAddress(idxSet));
    mem.Write32(0x5008, win32::IndexToHostAddress(idxQuery));
    mem.Write32(0x500C, win32::IndexToHostAddress(idxClose));

    // Готовим строки и данные
    WriteWideString(mem, 0x6000, "Software\\Winmine");
    WriteWideString(mem, 0x6100, "BestTime");
    mem.Write32(0x6300, 42); // значение для записи (REG_DWORD)
    mem.Write32(0x6404, 4);  // ёмкость буфера для чтения (lpcbData на входе)

    cpu::CpuState cpuState;
    cpuState.eip = CODE_BASE;
    cpuState.ESP() = CODE_BASE + 0x7800;
    uint32_t initialEsp = cpuState.ESP();

    cpu::Interpreter interp(mem);

    int steps = 0;
    while (steps < 200) {
        win32::HostCallOutcome outcome;
        if (win32::TryHandleHostCall(cpuState, mem, outcome)) {
            printf("  [host] eax=0x%x\n", outcome.returnValue);
            steps++;
            continue;
        }
        cpu::Instruction instr;
        uint32_t eipBefore = cpuState.eip;
        auto result = interp.Step(cpuState, &instr);
        printf("  [0x%04x] %s\n", eipBefore, cpu::ToString(instr.mnemonic));
        if (result == cpu::StepResult::Halted) { printf("  -> HALT\n"); break; }
        if (result == cpu::StepResult::UnknownOpcode) {
            printf("  !! Unknown opcode 0x%02x\n", instr.rawOpcodeForDebug);
            return 1;
        }
        steps++;
    }

    uint32_t ebx = cpuState.Get(cpu::Reg32::EBX); // код возврата RegCreateKeyExW
    uint32_t esi = cpuState.Get(cpu::Reg32::ESI); // код возврата RegSetValueExW
    uint32_t edi = cpuState.Get(cpu::Reg32::EDI); // код возврата RegQueryValueExW
    uint32_t edx = cpuState.Get(cpu::Reg32::EDX); // код возврата RegCloseKey
    uint32_t phkResult = mem.Read32(0x6200);
    uint32_t disposition = mem.Read32(0x6204);
    uint32_t readBackValue = mem.Read32(0x6400);
    uint32_t lpcbDataAfter = mem.Read32(0x6404);

    printf("\n=== Результаты ===\n");
    printf("RegCreateKeyExW  -> %u (ожидалось 0)\n", ebx);
    printf("phkResult        -> 0x%x (ожидалось != 0)\n", phkResult);
    printf("disposition      -> %u (ожидалось 1, REG_CREATED_NEW_KEY)\n", disposition);
    printf("RegSetValueExW   -> %u (ожидалось 0)\n", esi);
    printf("RegQueryValueExW -> %u (ожидалось 0)\n", edi);
    printf("Прочитанное значение -> %u (ожидалось 42)\n", readBackValue);
    printf("lpcbData после чтения -> %u (ожидалось 4)\n", lpcbDataAfter);
    printf("RegCloseKey      -> %u (ожидалось 0)\n", edx);
    printf("ESP финальный -> 0x%x (ожидалось 0x%x, стек не должен был съехать)\n",
           cpuState.ESP(), initialEsp);

    bool ok = (ebx == 0) && (phkResult != 0) && (disposition == 1) && (esi == 0)
              && (edi == 0) && (readBackValue == 42) && (lpcbDataAfter == 4)
              && (edx == 0) && (cpuState.ESP() == initialEsp);

    printf("\n%s\n", ok ? "[ТЕСТ РЕЕСТРА ЧЕРЕЗ X86: ПРОЙДЕН]" : "[ТЕСТ РЕЕСТРА ЧЕРЕЗ X86: ПРОВАЛЕН]");
    return ok ? 0 : 1;
}

#include "source/cpu/cpu_state.h"
#include "source/cpu/guest_memory.h"
#include "source/cpu/interpreter.h"
#include "source/decoder/x86_decoder.h"
#include "source/win32/host_functions.h"
#include "source/win32/window_manager.h"

#include <cstdio>
#include <fstream>
#include <vector>

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

int main() {
    constexpr uint32_t CODE_BASE = 0x1000;
    cpu::FlatGuestMemory mem(CODE_BASE, 0x8000);

    auto code = ReadFile("test13.bin");
    mem.LoadBytes(CODE_BASE, code.data(), code.size());

    // Строка className "MineClass" (UTF-16) в 0x6000 — пишем сами, раз в asm
    // мы этого не делали (только вручную собрали "Wrong" через mov word)
    const char* className = "MineClass";
    for (size_t i = 0; className[i]; i++) mem.Write16(0x6000 + static_cast<uint32_t>(i) * 2, className[i]);
    mem.Write16(0x6000 + 9 * 2, 0);

    int idxRegister = win32::FindHostFunction("USER32.dll", "RegisterClassW");
    int idxCreate = win32::FindHostFunction("USER32.dll", "CreateWindowExW");
    if (idxRegister < 0 || idxCreate < 0) { printf("!! функции не найдены\n"); return 1; }
    mem.Write32(0x5000, win32::IndexToHostAddress(idxRegister));
    mem.Write32(0x5004, win32::IndexToHostAddress(idxCreate));

    cpu::CpuState cpuState;
    cpuState.eip = CODE_BASE;
    cpuState.ESP() = CODE_BASE + 0x7800;
    uint32_t initialEsp = cpuState.ESP();

    cpu::Interpreter interp(mem);

    int steps = 0;
    while (steps < 300) {
        win32::HostCallOutcome outcome;
        if (win32::TryHandleHostCall(cpuState, mem, outcome)) {
            printf("  [host] eax=0x%x\n", outcome.returnValue);
            steps++;
            continue;
        }
        cpu::Instruction instr;
        uint32_t eipBefore = cpuState.eip;
        auto result = interp.Step(cpuState, &instr);
        if (result == cpu::StepResult::Halted) { printf("  -> HALT\n"); break; }
        if (result == cpu::StepResult::UnknownOpcode) {
            printf("  !! Unknown opcode 0x%02x at 0x%x\n", instr.rawOpcodeForDebug, eipBefore);
            return 1;
        }
        steps++;
    }

    uint32_t atom = cpuState.Get(cpu::Reg32::EBX);
    uint32_t hwndGood = cpuState.Get(cpu::Reg32::ECX);
    uint32_t hwndBad = cpuState.Get(cpu::Reg32::EDX);

    printf("\nRegisterClassW atom = %u (ожидалось != 0)\n", atom);
    printf("CreateWindowExW(правильный класс) = 0x%x (ожидалось != 0)\n", hwndGood);
    printf("CreateWindowExW(неверный класс)    = %u (ожидалось 0)\n", hwndBad);

    const win32::WindowInfo* winInfo = win32::g_windowManager.GetWindow(hwndGood);
    bool geomOk = winInfo && winInfo->width == 640 && winInfo->height == 360;
    if (winInfo) {
        printf("Геометрия окна: %dx%d в (%d,%d) (ожидалось 640x360 из-за CW_USEDEFAULT)\n",
               winInfo->width, winInfo->height, winInfo->x, winInfo->y);
    }

    const win32::WindowClassInfo* classInfo = win32::g_windowManager.GetWindowClass(hwndGood);
    bool wndProcOk = classInfo && classInfo->wndProc == 0x2000;
    if (classInfo) {
        printf("WndProc сохранён верно: 0x%x (ожидалось 0x2000)\n", classInfo->wndProc);
    }

    printf("ESP финальный = 0x%x (ожидалось 0x%x)\n", cpuState.ESP(), initialEsp);

    bool ok = (atom != 0) && (hwndGood != 0) && (hwndBad == 0) && geomOk && wndProcOk
              && (cpuState.ESP() == initialEsp);
    printf("\n%s\n", ok ? "[ТЕСТ ОКНА ЧЕРЕЗ X86: ПРОЙДЕН]" : "[ТЕСТ ОКНА ЧЕРЕЗ X86: ПРОВАЛЕН]");
    return ok ? 0 : 1;
}

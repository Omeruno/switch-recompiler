#include "source/cpu/cpu_state.h"
#include "source/cpu/guest_memory.h"
#include "source/cpu/interpreter.h"
#include "source/decoder/x86_decoder.h"
#include "source/win32/host_functions.h"
#include "source/win32/window_manager.h"
#include "source/win32/message_loop.h"

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

    auto code = ReadFile("test19.bin");
    mem.LoadBytes(CODE_BASE, code.data(), code.size());

    uint32_t wndProcAddr = CODE_BASE + 0x0E; // смещение wndproc внутри бинарника

    // Регистрируем окно "вручную" — как если бы уже отработали
    // RegisterClassW/CreateWindowExW (это уже отдельно проверено)
    win32::WindowClassInfo classInfo;
    classInfo.wndProc = wndProcAddr;
    win32::g_windowManager.RegisterClass("testclass", classInfo);
    uint32_t hwnd = win32::g_windowManager.CreateWindow("testclass", 0, 0, 100, 100, 0);
    printf("Тестовое окно создано: hwnd=0x%x, wndProc=0x%x\n", hwnd, wndProcAddr);

    // MSG-структуру целиком готовит C++ harness (никаких хардкодов в asm)
    mem.Write32(0x6000, hwnd);   // msg.hwnd
    mem.Write32(0x6004, 0x5555); // msg.message (тестовый код)
    mem.Write32(0x6008, 10);     // msg.wParam
    mem.Write32(0x600C, 20);     // msg.lParam

    int idxDispatch = win32::FindHostFunction("USER32.dll", "DispatchMessageW");
    if (idxDispatch < 0) { printf("!! DispatchMessageW не найдена\n"); return 1; }
    mem.Write32(0x5000, win32::IndexToHostAddress(idxDispatch));

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
            printf("  !! Unknown opcode 0x%02x at 0x%x\n", instr.rawOpcodeForDebug, eipBefore);
            return 1;
        }
        steps++;
    }

    uint32_t lresult = cpuState.Get(cpu::Reg32::EBX);
    uint32_t sideEffect = mem.Read32(0x7000);

    printf("\n=== Результаты ===\n");
    printf("LRESULT из DispatchMessage -> WndProc -> обратно: 0x%x (ожидалось 0x9999)\n", lresult);
    printf("Побочный эффект внутри WndProc (wParam+lParam): %u (ожидалось 30)\n", sideEffect);
    printf("ESP финальный: 0x%x (ожидалось 0x%x — стек не должен был съехать\n"
           "  даже после ВЛОЖЕННОГО вызова WndProc внутри DispatchMessage)\n",
           cpuState.ESP(), initialEsp);

    bool ok = (lresult == 0x9999) && (sideEffect == 30) && (cpuState.ESP() == initialEsp);
    printf("\n%s\n", ok ? "[ТЕСТ WNDPROC: ПРОЙДЕН]" : "[ТЕСТ WNDPROC: ПРОВАЛЕН]");
    return ok ? 0 : 1;
}

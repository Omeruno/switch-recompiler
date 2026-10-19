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

    auto code = ReadFile("test20.bin");
    mem.LoadBytes(CODE_BASE, code.data(), code.size());

    uint32_t wndProcAddr = CODE_BASE + 0x0E;

    win32::WindowClassInfo classInfo;
    classInfo.wndProc = wndProcAddr;
    win32::g_windowManager.RegisterClass("testclass2", classInfo);
    uint32_t hwnd = win32::g_windowManager.CreateWindow("testclass2", 0, 0, 100, 100, 0);

    mem.Write32(0x6000, hwnd);
    mem.Write32(0x6004, 0x5555);
    mem.Write32(0x6008, 77); // wParam
    mem.Write32(0x600C, 0);

    int idxDispatch = win32::FindHostFunction("USER32.dll", "DispatchMessageW");
    int idxDesktop = win32::FindHostFunction("USER32.dll", "GetDesktopWindow");
    mem.Write32(0x5000, win32::IndexToHostAddress(idxDispatch));
    mem.Write32(0x5004, win32::IndexToHostAddress(idxDesktop));

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
        if (result == cpu::StepResult::UnknownOpcode) { return 1; }
        steps++;
    }

    uint32_t lresult = cpuState.Get(cpu::Reg32::EBX);
    uint32_t desktopHwndSeenInside = mem.Read32(0x7004);

    printf("\nLRESULT (эхо wParam=77): %u (ожидалось 77)\n", lresult);
    printf("GetDesktopWindow(), вызванный ИЗНУТРИ WndProc: 0x%x (ожидалось 0xffff0001)\n", desktopHwndSeenInside);
    printf("ESP финальный: 0x%x (ожидалось 0x%x)\n", cpuState.ESP(), initialEsp);

    bool ok = (lresult == 77) && (desktopHwndSeenInside == 0xFFFF0001u) && (cpuState.ESP() == initialEsp);
    printf("\n%s\n", ok ? "[ТЕСТ ВЛОЖЕННОГО HOST-ВЫЗОВА: ПРОЙДЕН]" : "[ТЕСТ ВЛОЖЕННОГО HOST-ВЫЗОВА: ПРОВАЛЕН]");
    return ok ? 0 : 1;
}

#include "source/cpu/cpu_state.h"
#include "source/cpu/guest_memory.h"
#include "source/cpu/interpreter.h"
#include "source/decoder/x86_decoder.h"
#include "source/win32/host_functions.h"
#include "source/win32/gdi.h"

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

    auto code = ReadFile("test12.bin");
    mem.LoadBytes(CODE_BASE, code.data(), code.size());

    const char* names[] = {"GetDC", "CreateCompatibleDC", "CreateCompatibleBitmap",
                            "SelectObject", "SetPixel", "BitBlt", "DeleteDC", "DeleteObject"};
    const char* dlls[] = {"USER32.dll", "GDI32.dll", "GDI32.dll", "GDI32.dll",
                           "GDI32.dll", "GDI32.dll", "GDI32.dll", "GDI32.dll"};
    uint32_t slots[] = {0x5000, 0x5004, 0x5008, 0x500C, 0x5010, 0x5014, 0x5018, 0x501C};

    for (int i = 0; i < 8; i++) {
        int idx = win32::FindHostFunction(dlls[i], names[i]);
        if (idx < 0) { printf("!! %s не найдена в таблице\n", names[i]); return 1; }
        mem.Write32(slots[i], win32::IndexToHostAddress(idx));
    }

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

    // Проверяем: пиксель (105,105) на "экране" должен стать красным —
    // (5,5) в memDC сдвинутый BitBlt'ом на (100,100)
    uint32_t screenDc = gdi::g_gdi.GetOrCreateScreenDC(1280, 720); // тот же хендл, что уже создан
    const gdi::Bitmap* screen = gdi::g_gdi.GetBitmapForDC(screenDc);

    bool ok = false;
    if (screen) {
        uint32_t px = screen->pixels[105 * screen->width + 105];
        uint8_t r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
        printf("\nПиксель экрана (105,105) = R=%d G=%d B=%d (ожидалось R=255,G=0,B=0)\n", r, g, b);
        ok = (r == 255 && g == 0 && b == 0);
    }

    printf("ESP финальный = 0x%x (ожидалось 0x%x)\n", cpuState.ESP(), initialEsp);
    ok = ok && (cpuState.ESP() == initialEsp);

    printf("\n%s\n", ok ? "[ТЕСТ GDI ЧЕРЕЗ X86: ПРОЙДЕН]" : "[ТЕСТ GDI ЧЕРЕЗ X86: ПРОВАЛЕН]");
    return ok ? 0 : 1;
}

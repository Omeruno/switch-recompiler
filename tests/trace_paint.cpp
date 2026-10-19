#include <cstdlib>
#include <vector>
// Консольный харнесс, повторяющий ТОЧНУЮ настройку RunInterpreter() из
// main.cpp (память/TEB/CRT-глобали/куча/IAT-патчинг), но без switch.h —
// чтобы прогнать paint.exe на хосте.

#include "source/loader/pe_parser.h"
#include "source/cpu/cpu_state.h"
#include "source/cpu/guest_memory.h"
#include "source/cpu/interpreter.h"
#include "source/win32/host_functions.h"
#include "source/win32/iat_patcher.h"
#include "source/win32/crt_globals.h"
#include "source/win32/window_manager.h"
#include "source/win32/gdi.h"

#include <cstdio>
#include <cstdint>
#include <ctime>
#include <fstream>

static uint32_t FakeGetTickCount() {
    return static_cast<uint32_t>(clock() * 1000ull / CLOCKS_PER_SEC);
}

namespace win32 { extern uint32_t g_paintCDC; extern uint32_t g_requestedToolId; extern uint32_t g_requestedBackgroundColor; }

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "paint.exe";
    long maxSteps = argc > 2 ? atol(argv[2]) : 2000000;
    int maxLinesToShow = argc > 3 ? atoi(argv[3]) : 200;

    pe::PEParser parser;
    if (parser.LoadFromFile(path) != pe::LoadResult::Ok) {
        printf("[!] Failed to load %s\n", path);
        return 1;
    }

    uint32_t imageSize = parser.GetSizeOfImage();
    constexpr uint32_t STACK_SIZE = 0x10000;
    constexpr uint32_t TEB_SIZE = 0x1000;
    uint32_t imageBase = parser.GetImageBase();

    std::vector<uint8_t> image(imageSize, 0);
    parser.MapImageToMemory(image.data(), image.size());

    uint32_t stackBottom = imageBase + imageSize;
    uint32_t stackTop = stackBottom + STACK_SIZE;
    uint32_t tebAddr = stackTop;
    uint32_t crtGlobalsAddr = tebAddr + TEB_SIZE;
    uint32_t mfcStateAddr = crtGlobalsAddr + win32::CRT_GLOBALS_SIZE;
    uint32_t heapAddr = mfcStateAddr + win32::MFC_STATE_SIZE;

    win32::PatchResult patchStats = win32::PatchImportAddressTable(
        parser, image.data(), image.size(), crtGlobalsAddr);
    printf("[+] IAT patched: %d/%d imports implemented (unimplemented=%d)\n",
           patchStats.patched, patchStats.totalImports, patchStats.unimplemented);

    win32::g_imageBuffer = image.data();
    win32::g_imageBufferSize = image.size();
    win32::g_resourceDirRva = parser.GetResourceDirectoryRva();

    constexpr uint32_t DECODE_SAFETY_MARGIN = 64;
    cpu::FlatGuestMemory mem(imageBase, imageSize + STACK_SIZE + TEB_SIZE +
                             win32::CRT_GLOBALS_SIZE + win32::MFC_STATE_SIZE +
                             win32::HEAP_SIZE + DECODE_SAFETY_MARGIN);
    mem.LoadBytes(imageBase, image.data(), image.size());

    win32::InitCrtGlobals(mem, crtGlobalsAddr, path);
    win32::g_mfcStateBase = mfcStateAddr;
    win32::g_heapBase = heapAddr;
    win32::g_heapNext = heapAddr;

    mem.Write32(tebAddr + 0x00, 0xFFFFFFFF);
    mem.Write32(tebAddr + 0x04, stackTop);
    mem.Write32(tebAddr + 0x08, stackBottom);

    cpu::CpuState cpuState;
    cpuState.eip = imageBase + parser.GetEntryPointRva();
    cpuState.ESP() = stackTop - 16;

    cpu::Interpreter interp(mem, tebAddr, tebAddr);

    win32::g_getTickCountImpl = &FakeGetTickCount;
    win32::g_imageBase = imageBase;
    win32::g_tebAddr = tebAddr;

    // УНИВЕРСАЛЬНЫЙ ТЕСТ ИНСТРУМЕНТА. Инструмент задаётся 4-м аргументом
    // (hex id, например 0x280 = Кисть; по умолчанию 0x27c = Карандаш).
    // Сценарий: кадр 30 выбрать инструмент (для Пипетки ещё и закрасить
    // контрольный пиксель), кадры 35..45 провести штрих (40,50)->(90,95),
    // кадр 48 посчитать изменившиеся пиксели холста и напечатать вердикт.
    static uint32_t s_toolId = (argc > 4) ? static_cast<uint32_t>(strtoul(argv[4], nullptr, 16)) : 0x27cu;
    static cpu::IGuestMemory* s_memPtr = &mem;
    static int s_fakeFrameCount = 0;
    static std::vector<uint32_t> s_before;
    constexpr int kW = 200, kH = 160;
    constexpr uint32_t kProbe = 0x00A5B4C3u;
    win32::g_pumpHostFrame = []() -> win32::HostFrameResult {
        cpu::IGuestMemory& mem = *s_memPtr;
        s_fakeFrameCount++;
        win32::HostFrameResult r;
        uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
        uint32_t hdc = win32::g_paintCDC ? mem.Read32(win32::g_paintCDC + 0x4) : 0;
        auto lp = [](int x, int y) { return (static_cast<uint32_t>(y) << 16) | static_cast<uint32_t>(x); };
        const int f = s_fakeFrameCount;
        if (f == 30 && hdc != 0) {
            if (s_toolId == 0x27fu) gdi::g_gdi.SetPixel(hdc, 40, 50, kProbe);
            s_before.resize(kW * kH);
            for (int y = 0; y < kH; y++) for (int x = 0; x < kW; x++) s_before[y * kW + x] = gdi::g_gdi.GetPixel(hdc, x, y);
            win32::g_requestedToolId = s_toolId;
            printf("[ТЕСТ] инструмент 0x%x выбран\n", s_toolId);
        } else if (f == 35) {
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONDOWN, 1, lp(40, 50));
        } else if (f >= 36 && f <= 44 && s_toolId != 0x27fu) {
            int k = f - 35;
            win32::g_messageQueue.Post(hwnd, win32::WM_MOUSEMOVE, 1, lp(40 + k * 5, 50 + k * 5));
        } else if (f == (s_toolId == 0x27fu ? 36 : 45)) {
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONUP, 0, s_toolId == 0x27fu ? lp(40, 50) : lp(90, 95));
        } else if (f == 48 && hdc != 0) {
            int changed = 0;
            for (int y = 0; y < kH; y++) for (int x = 0; x < kW; x++)
                if (gdi::g_gdi.GetPixel(hdc, x, y) != s_before[y * kW + x]) changed++;
            uint32_t fg = mem.Read32(0x103d6a0) & 0xFFFFFFu;
            printf("[ТЕСТ] РЕЗУЛЬТАТ tool=0x%x: изменено пикселей=%d, fg=0x%06X\n", s_toolId, changed, fg);
            if (s_toolId == 0x27fu) printf("[ТЕСТ] Пипетка: %s\n", fg == kProbe ? "ЦВЕТ ПОДОБРАН ВЕРНО" : "НЕ СОВПАЛО");
            win32::g_requestedBackgroundColor = 0x00C0C0C0u; // любое действие, вызывающее перерисовку
        } else if (f == 53 && hdc != 0 && s_toolId != 0x27fu) {
            int changed = 0;
            for (int y = 0; y < kH; y++) for (int x = 0; x < kW; x++)
                if (gdi::g_gdi.GetPixel(hdc, x, y) != s_before[y * kW + x]) changed++;
            printf("[ТЕСТ] ПОСЛЕ ПЕРЕРИСОВКИ: изменено пикселей=%d (рисунок %s)\n", changed, changed > 0 ? "сохранился" : "ПРОПАЛ");
            if (const char* dump = std::getenv("TOOL_DUMP")) { // визуальная проверка: область холста в PPM
                if (FILE* fp = std::fopen(dump, "wb")) {
                    std::fprintf(fp, "P6\n%d %d\n255\n", kW, kH);
                    for (int y = 0; y < kH; y++) for (int x = 0; x < kW; x++) {
                        uint32_t c = gdi::g_gdi.GetPixel(hdc, x, y); // COLORREF 0x00BBGGRR
                        std::fputc(c & 0xFF, fp); std::fputc((c >> 8) & 0xFF, fp); std::fputc((c >> 16) & 0xFF, fp);
                    }
                    std::fclose(fp);
                }
            }
        } else if (f > 58) {
            r.shouldQuit = true;
        }
        return r;
    };

    printf("\n[+] Executing from entry point 0x%x (imageBase=0x%x, entryRva=0x%x)\n",
           cpuState.eip, imageBase, parser.GetEntryPointRva());

    long steps = 0;
    int shown = 0;
    const char* stopReason = "max steps reached";

    while (steps < maxSteps) {
        if (cpuState.eip == 0) {
            stopReason = "called unimplemented import (address 0)";
            break;
        }

        win32::HostCallOutcome outcome;
        if (win32::TryHandleHostCall(cpuState, mem, outcome)) {
            if (shown < maxLinesToShow) {
                printf(" [host eip_was_host_stub] eax=0x%x%s\n", outcome.returnValue,
                       outcome.requestExit ? " (exit requested)" : "");
                shown++;
            }
            if (outcome.requestExit) { stopReason = "guest called exit()"; break; }
            steps++;
            continue;
        }

        cpu::Instruction instr;
        uint32_t eipBefore = cpuState.eip;
        auto result = interp.Step(cpuState, &instr);

        if (shown < maxLinesToShow) {
            printf(" %06x: %s\n", eipBefore, cpu::ToString(instr.mnemonic));
            shown++;
        }

        if (result == cpu::StepResult::Exception) { stopReason = "guest exception"; std::printf("[EXC] 0x%08x at 0x%x addr 0x%x\n", interp.LastException().code, interp.LastException().eip, interp.LastException().faultAddress); break; }
        if (result == cpu::StepResult::Halted) { stopReason = "HLT"; break; }
        if (result == cpu::StepResult::UnknownOpcode) {
            printf(" !! Unknown opcode 0x%02x at 0x%x\n", instr.rawOpcodeForDebug, eipBefore);
            stopReason = "unknown opcode";
            break;
        }
        steps++;
    }

    printf("\n[+] Execution stopped: %s\n", stopReason);
    printf("    Steps executed: %ld (EIP=0x%x)\n", steps, cpuState.eip);

    uint32_t lastHwnd = win32::g_windowManager.GetLastCreatedWindow();
    if (lastHwnd != 0) {
        const win32::WindowInfo* wi = win32::g_windowManager.GetWindow(lastHwnd);
        printf("\n[+] ОКНО СОЗДАНО: hwnd=0x%x", lastHwnd);
        if (wi) printf(" класс=%s гео=%dx%d в (%d,%d)", wi->className.c_str(), wi->width, wi->height, wi->x, wi->y);
        printf("\n");
    } else {
        printf("\n[-] Окно ТАК И НЕ создано (CreateWindowExW ни разу не вызван успешно)\n");
    }

    // ПРОВЕРКА МЕНЮ (эта сессия): загрузилось ли реальное меню paint.exe?
    printf("\n[МЕНЮ] g_menuLoaded=%d\n", win32::g_menuLoaded ? 1 : 0);
    if (win32::g_menuLoaded) {
        printf("[МЕНЮ] пунктов верхнего уровня: %zu\n", win32::g_loadedMenu.topLevel.size());
        for (size_t i = 0; i < win32::g_loadedMenu.topLevel.size(); i++) {
            const auto& item = win32::g_loadedMenu.topLevel[i];
            std::string name;
            for (uint16_t c : item.text) name += static_cast<char>(c < 128 ? c : '?');
            printf("[МЕНЮ]   [%zu] \"%s\" (id=%u, isPopup=%d, subItems=%zu)\n",
                   i, name.c_str(), item.id, item.isPopup ? 1 : 0, item.subItems.size());
            for (size_t j = 0; j < item.subItems.size() && j < 20; j++) {
                const auto& sub = item.subItems[j];
                std::string subName;
                for (uint16_t c : sub.text) subName += static_cast<char>(c < 128 ? c : '?');
                printf("[МЕНЮ]       - \"%s\" (id=%u, separator=%d, isPopup=%d, subItems=%zu)\n",
                       subName.c_str(), sub.id, sub.isSeparator ? 1 : 0, sub.isPopup ? 1 : 0, sub.subItems.size());
            }
        }
    }

    // Дамп содержимого "экранного" DC — смотрим, что реально нарисовал
    // OnDraw (если вообще что-то). GetOrCreateScreenDC создаётся честным
    // прагматичным костылём в OnFileNew при вызове OnDraw.
    uint32_t screenHdc = gdi::g_gdi.GetScreenDCHandle();
    const gdi::Bitmap* bmp = screenHdc ? gdi::g_gdi.GetBitmapForDC(screenHdc) : nullptr;
    if (bmp && bmp->width > 0 && bmp->height > 0) {
        std::ofstream out("paint_output.ppm", std::ios::binary);
        out << "P6\n" << bmp->width << " " << bmp->height << "\n255\n";
        for (int y = 0; y < bmp->height; y++) {
            for (int x = 0; x < bmp->width; x++) {
                uint32_t argb = bmp->pixels[static_cast<size_t>(y) * bmp->width + x];
                uint8_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
                out.put(static_cast<char>(r));
                out.put(static_cast<char>(g));
                out.put(static_cast<char>(b));
            }
        }
        printf("\n[+] Дамп экранного DC (hdc=0x%x, %dx%d) сохранён в paint_output.ppm\n",
               screenHdc, bmp->width, bmp->height);
    } else {
        printf("\n[-] Экранный DC не создан или пуст — дампить нечего.\n");
    }

    return 0;
}

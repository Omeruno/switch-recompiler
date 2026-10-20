// Include the most common headers from the C standard library
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <cstdarg>
#include <vector>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <set>
#include <queue>
#include <cctype>

// Include the main libnx system header, for Switch development
#include <switch.h>
#include <sys/stat.h>

// Наш PE-парсер и x86-decoder
#include "loader/pe_parser.h"
#include "decoder/x86_decoder.h"
#include "cpu/cpu_state.h"
#include "cpu/guest_memory.h"
#include "cpu/interpreter.h"
#include "runtime/process.h"
#include "win32/host_functions.h"
#include "win32/bitmap_font.h"
#include "win32/iat_patcher.h"
#include "win32/crt_globals.h"
#include "loader/resource_parser.h"
#include "win32/gdi.h"
#include "win32/toolbar_bitmap_data.h"
#include "win32/palette_data.h"

// Сигнал "выбрать инструмент" (эта сессия) — реально определён в
// host_functions.cpp В НАЧАЛЕ файла (до анонимного namespace, чтобы
// линковка была корректной — на этих граблях мы уже спотыкались раньше
// в этой же сессии с диагностическими счётчиками).
namespace win32 {
    extern uint32_t g_requestedToolId;
    extern uint32_t g_requestedForegroundColor;
    extern uint32_t g_requestedBackgroundColor;
}

// Флаг переключения в графический режим — объявлен здесь (а не рядом с
// остальным вводом ниже), чтобы RunInterpreter мог его проверить перед
// своими финальными диагностическими printf: если мы уже в графике
// (консоль закрыта), печатать туда больше нельзя.
static bool g_graphicsInitialized = false;

// Путь к выбранному исполняемому файлу — заполняется PickExecutableFile()
// в main() (см. ниже) ДО создания игрового потока; объявлена здесь так рано
// специально, чтобы GameThreadFunc (которая идёт по файлу раньше, чем сам
// пикер) могла её использовать.
static std::string g_selectedExePath = "romfs:/winmine.exe"; // безопасный дефолт, если пикер не понадобится/не сработает

// НАЙДЕНО ОБРАТНОЙ СВЯЗЬЮ С РЕАЛЬНОГО ЖЕЛЕЗА: тулбар/палитра рисовались
// поверх Сапёра — оказалось, DrawToolbar/DrawPalette ошибочно
// использовали ОБЩИЙ признак "g_menuLoaded" (истинен для ЛЮБОГО
// MFC-приложения с настоящим меню, включая Сапёра — у него тоже
// честное меню Game/Help). Тулбар/палитра — специфичны именно для
// Paint, им нужен СВОЙ, более узкий признак.
static bool IsRunningPaint() {
    std::string lower = g_selectedExePath;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("paint") != std::string::npos;
}

// Безопасные обёртки: печатать/обновлять текстовую консоль имеет смысл
// ТОЛЬКО пока мы реально в текстовом режиме (g_graphicsInitialized==false).
// Вызов printf/consoleUpdate ПОСЛЕ переключения в графику — именно та
// причина краха, которую мы поймали через addr2line (падало внутри
// ConsoleSwRenderer_drawChar из libnx) — эти обёртки защищают от повторения.
static void SafePrint(const char* fmt, ...) {
    if (g_graphicsInitialized) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
static void SafeConsoleUpdate() {
    if (!g_graphicsInitialized) consoleUpdate(NULL);
}

// Настоящий источник времени для GetTickCount — не выдумка, реальные тики
// системы Switch, переведённые в миллисекунды (как это делает Windows).
static uint32_t RealGetTickCount()
{
    u64 ns = armTicksToNs(armGetSystemTick());
    return static_cast<uint32_t>(ns / 1000000ull);
}

static void PrintPEAnalysis(const char* path)
{
    pe::PEParser parser;
    pe::LoadResult result = parser.LoadFromFile(path);

    if (result != pe::LoadResult::Ok)
    {
        printf("[!] Failed to load %s: %s\n", path, pe::ToString(result));
        return;
    }

    printf("[+] PE loaded: %s\n", path);
    printf("    Entry Point RVA: 0x%x\n", parser.GetEntryPointRva());
    printf("    Image Base:      0x%x\n", parser.GetImageBase());
    printf("    Size Of Image:   0x%x\n\n", parser.GetSizeOfImage());

    printf("[+] Sections (%zu):\n", parser.GetSections().size());
    for (const auto& s : parser.GetSections())
    {
        printf("    %-8s VA=0x%-6x Size=0x%-6x\n",
               s.name.c_str(), s.virtualAddress, s.virtualSize);
    }

    printf("\n[+] Imports (%zu modules):\n", parser.GetImports().size());
    size_t totalFunctions = 0;
    for (const auto& mod : parser.GetImports())
    {
        printf("    %-14s -> %zu functions\n", mod.dllName.c_str(), mod.functions.size());
        totalFunctions += mod.functions.size();
    }
    printf("\n    Total imported functions: %zu\n", totalFunctions);
}

// Реально ДЕКОДИРУЕТ (не просто читает заголовки) байты .text секции нашим
// собственным x86-decoder'ом, идя по потоку исполнения от entry point —
// та же методика, что мы обкатали на ПК, но теперь прямо на консоли.
static void PrintDisassembly(const char* path, int maxLinesToShow)
{
    pe::PEParser parser;
    pe::LoadResult result = parser.LoadFromFile(path);
    if (result != pe::LoadResult::Ok)
    {
        printf("[!] Failed to load %s for disassembly\n", path);
        return;
    }

    uint32_t imageSize = parser.GetSizeOfImage();
    std::vector<uint8_t> image(imageSize, 0);
    parser.MapImageToMemory(image.data(), image.size());

    uint32_t imageBase = parser.GetImageBase();
    uint32_t entryVa = imageBase + parser.GetEntryPointRva();

    // Границы допустимой адресации внутри нашего образа (для проверки выхода
    // за пределы .text/вообще известной памяти — напр. переход в другую DLL)
    uint32_t imageLo = imageBase;
    uint32_t imageHi = imageBase + imageSize;

    std::set<uint32_t> visited;
    std::queue<uint32_t> worklist;
    worklist.push(entryVa);

    int totalDecoded = 0;
    int unknownCount = 0;
    int shown = 0;

    printf("\n[+] Disassembly from entry point 0x%x:\n", entryVa);

    while (!worklist.empty())
    {
        uint32_t addr = worklist.front();
        worklist.pop();
        if (visited.count(addr)) continue;
        if (addr < imageLo || addr >= imageHi) continue;

        uint32_t cursor = addr;
        while (cursor >= imageLo && cursor < imageHi && !visited.count(cursor))
        {
            visited.insert(cursor);
            uint32_t offsetInImage = cursor - imageBase;

            uint8_t safeBuf[16] = {0};
            uint32_t remaining = imageSize - offsetInImage;
            uint32_t toCopy = remaining < 16 ? remaining : 16;
            std::memcpy(safeBuf, image.data() + offsetInImage, toCopy);

            cpu::Instruction instr = cpu::Decode(safeBuf, cursor);
            totalDecoded++;

            if (shown < maxLinesToShow)
            {
                if (instr.mnemonic == cpu::Mnemonic::Unknown)
                    printf(" %06x: ??? (0x%02x)\n", cursor, instr.rawOpcodeForDebug);
                else
                    printf(" %06x: %s\n", cursor, cpu::ToString(instr.mnemonic));
                shown++;
            }

            if (instr.mnemonic == cpu::Mnemonic::Unknown)
            {
                unknownCount++;
                break;
            }
            if (instr.mnemonic == cpu::Mnemonic::Ret || instr.mnemonic == cpu::Mnemonic::Halt)
                break;
            if (instr.mnemonic == cpu::Mnemonic::Jmp)
            {
                if (!instr.indirect) worklist.push(instr.branchTarget);
                break;
            }
            if (instr.mnemonic == cpu::Mnemonic::Jcc)
            {
                worklist.push(instr.branchTarget);
                cursor += instr.length;
                continue;
            }
            if (instr.mnemonic == cpu::Mnemonic::Call)
            {
                if (!instr.indirect) worklist.push(instr.branchTarget);
                cursor += instr.length;
                continue;
            }
            cursor += instr.length;
        }
    }

    printf("\n[+] Decoded %d instructions total (%d shown above)\n", totalDecoded, shown);
    printf("    Unknown opcodes hit: %d\n", unknownCount);
    if (totalDecoded > 0)
    {
        double pct = 100.0 * (totalDecoded - unknownCount) / totalDecoded;
        printf("    Coverage on reached paths: %.1f%%\n", pct);
    }
}

// НАСТОЯЩЕЕ исполнение — не просто разбор байт, а реальный запуск через наш
// Interpreter, с перехватом вызовов Win32/CRT функций через IAT.
static void RunInterpreter(const char* path, int maxLinesToShow, int maxSteps)
{
    // Процесс гостя: EXE и его DLL по своим адресам, TEB/PEB, стек, куча
    // (rt::Process — тот же загрузчик, что в хостовых тестах). MFC пока
    // эмулируется (HLE), даже если mfc42u.dll лежит рядом с EXE.
    static cpu::PagedGuestMemory mem;
    static rt::Process proc(mem);
    rt::g_process = &proc;
    proc.ForceHle("mfc42u.dll");
    proc.SetCommandLine(g_selectedExePath);

    std::string err;
    cpu::CpuState cpuState;
    rt::Module* exe = proc.LoadMainExe(path, err);
    if (!exe || !proc.PrepareMainThread(cpuState, err))
    {
        SafePrint("[!] Failed to load %s: %s\n", path, err.c_str());
        return;
    }
    SafePrint("[+] %s loaded at 0x%x\n", path, exe->base);

    win32::g_getTickCountImpl = &RealGetTickCount;
    if (!proc.InitializeModules(cpuState, err))
    {
        SafePrint("[!] DLL initialization failed: %s\n", err.c_str());
        return;
    }

    cpu::Interpreter interp(mem, proc.TebAddress(), proc.TebAddress());

    SafePrint("\n[+] Executing from entry point 0x%x:\n", cpuState.eip);
    SafeConsoleUpdate(); // показываем прогресс сразу, на случай если что-то пойдёт не так дальше

    int shown = 0;
    int steps = 0;
    const char* stopReason = "max steps reached";

    while (steps < maxSteps)
    {
        if (cpuState.eip == 0)
        {
            stopReason = "called unimplemented import (address 0)";
            break;
        }

        win32::HostCallOutcome outcome;
        if (win32::TryHandleHostCall(cpuState, mem, outcome))
        {
            if (shown < maxLinesToShow)
            {
                SafePrint(" [host] eax=0x%x%s\n", outcome.returnValue,
                       outcome.requestExit ? " (exit requested)" : "");
                shown++;
                SafeConsoleUpdate();
            }
            if (outcome.requestExit)
            {
                stopReason = "guest called exit()";
                break;
            }
            steps++;
            continue;
        }

        cpu::Instruction instr;
        uint32_t eipBefore = cpuState.eip;
        cpu::g_lastKnownEip = eipBefore; // НАЙДЕНО ЭТИМ СИСТЕМНЫМ ПРОСМОТРОМ: раньше
                                          // обновлялось только во вложенном цикле
                                          // (message_loop.cpp) — краши в ОСНОВНОМ
                                          // цикле на реальном Switch показывали бы
                                          // устаревший/нулевой EIP в диагностике.
        auto result = interp.Step(cpuState, &instr);

        if (shown < maxLinesToShow)
        {
            SafePrint(" %06x: op %02x (len %u)\n", eipBefore, instr.rawOpcodeForDebug, instr.length);
            shown++;
            SafeConsoleUpdate(); // печатаем прогресс "в реальном времени"
        }

        if (result == cpu::StepResult::Halted) { stopReason = "HLT"; break; }
        if (result == cpu::StepResult::UnknownOpcode) { stopReason = "unknown opcode"; break; }
        if (result == cpu::StepResult::Exception)
        {
            const cpu::CpuException& e = interp.LastException();
            SafePrint("[!] Guest exception 0x%08x at EIP=0x%x (address 0x%x)\n", e.code, e.eip, e.faultAddress);
            stopReason = "guest exception";
            break;
        }
        steps++;
    }

    if (!g_graphicsInitialized) {
        SafePrint("\n[+] Execution stopped: %s\n", stopReason);
        SafePrint("    Steps executed: %d (EIP=0x%x)\n", steps, cpuState.eip);
        SafeConsoleUpdate();
    }
}

// Наш внутренний формат пикселя — 0xAARRGGBB (см. gdi.h). Framebuffer Switch
// в PIXEL_FORMAT_RGBA_8888 хранит байты в памяти как R,G,B,A по возрастанию
// адреса — как 32-битное little-endian число это R | G<<8 | B<<16 | A<<24.
// Перепутать порядок каналов тут — классическая ошибка при портировании,
// поэтому конвертация вынесена отдельно и явно прокомментирована.
static inline uint32_t ArgbToSwitchRgba(uint32_t argb)
{
    uint8_t a = (argb >> 24) & 0xFF;
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    return r
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

// Копирует наш GDI "экранный" битмап (тот самый, что заполняли BitBlt/
// SetPixel/SetDIBitsToDevice изнутри интерпретируемой игры) в один кадр
// настоящего framebuffer'а Switch с масштабированием (2x), и поверх рисует
// курсор (управляется стиком — см. PumpHostFrame ниже). cursorX/cursorY —
// в ВИРТУАЛЬНЫХ координатах (640x360), как и события мыши, которые мы шлём.
// Текущая трансформация "виртуальный экран -> реальный экран" — обновляется
// каждый раз в DrawGdiScreenIntoFramebuffer, используется и для курсора, и
// (обратным преобразованием) для тача в PumpHostFrame — чтобы палец на
// экране совпадал именно с тем местом, где реально нарисовано содержимое.
static float g_contentScale = 2.0f;
static int g_contentOffsetX = 0, g_contentOffsetY = 0;
static int g_contentBaseX = 0, g_contentBaseY = 0;

// ВРЕМЕННО (для диагностики хитбокса смайлика): простейший битмап-шрифт
// цифр 0-9, 3x5 точек каждая — у нас пока нет настоящего рендеринга текста
// (TextOutW), поэтому рисуем вручную, прямо в реальный framebuffer.
static const uint8_t kDigitFont[10][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b001,0b001,0b001}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
};

static void DrawDigit(u32* framebuf, u32 strideInPixels, int screenX, int screenY, int digit, int scale) {
    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (kDigitFont[digit][row] & (1 << (2 - col))) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px = screenX + col*scale + sx, py = screenY + row*scale + sy;
                        if (px >= 0 && px < 1280 && py >= 0 && py < 720)
                            framebuf[static_cast<size_t>(py) * strideInPixels + px] = 0xFF00FF00u; // ярко-зелёный
                    }
            }
        }
    }
}

static void DrawNumber(u32* framebuf, u32 strideInPixels, int screenX, int screenY, int number, int scale) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", number);
    int x = screenX;
    for (char* p = buf; *p; p++) {
        if (*p == '-') { x += (3*scale + scale); continue; } // минус пропускаем визуально
        DrawDigit(framebuf, strideInPixels, x, screenY, *p - '0', scale);
        x += 3*scale + scale; // ширина цифры + промежуток
    }
}

// Классический курсор-стрелка (как в Windows) — белая заливка с чёрной
// обводкой для видимости на любом фоне. '#'=чёрная обводка, 'O'=белая
// заливка, '.'=прозрачно (не трогаем фон).
static const char* kCursorArrow[17] = {
    "#...........",
    "#O..........",
    "#OO.........",
    "#OOO........",
    "#OOOO.......",
    "#OOOOO......",
    "#OOOOOO.....",
    "#OOOOOOO....",
    "#OOOOOOOO...",
    "#OOOOOOOOO..",
    "#OOOOOOOOOO.",
    "#OOOOOO#####",
    "#OOO#OO#....",
    "#O#..#OO#...",
    "##....#OO#..",
    "#......#OO#.",
    "........##..",
};

static void DrawCursor(u32* framebuf, u32 strideInPixels, int cx, int cy) {
    for (int row = 0; row < 17; row++) {
        const char* line = kCursorArrow[row];
        for (int col = 0; line[col] != '\0'; col++) {
            char c = line[col];
            if (c == '.') continue;
            int px = cx + col, py = cy + row;
            if (px < 0 || px >= 1280 || py < 0 || py >= 720) continue;
            uint32_t color = (c == '#') ? 0xFF000000u : 0xFFFFFFFFu;
            framebuf[static_cast<size_t>(py) * strideInPixels + px] = color;
        }
    }
}

// --- Меню ("Игра"/"Справка") — рисуем и обрабатываем клики сами, раз у нас
// нет системного USER32, который в реальной Windows отрисовывал бы меню за
// приложение. Прямоугольники — в РЕАЛЬНЫХ координатах экрана (не виртуальных),
// раз сама полоса меню всегда занимает фиксированную область сверху экрана,
// поверх отмасштабированного содержимого игры.
struct MenuRect { int x1, y1, x2, y2; };
static std::vector<MenuRect> g_menuBarRects;   // индекс == индекс в topLevel
static std::vector<MenuRect> g_dropdownRects;  // индекс == индекс в subItems открытого меню
static int g_openMenuIndex = -1;               // -1 = меню закрыто

constexpr int MENU_BAR_H = 30;       // реальные пиксели
constexpr int MENU_FONT_SCALE = 2;
constexpr uint32_t MENU_BG_COLOR = 0xFFC0C0C0u;    // классический серый Windows
constexpr uint32_t MENU_TEXT_COLOR = 0xFF000000u;
constexpr uint32_t MENU_HOVER_COLOR = 0xFF000080u; // тёмно-синий — выделение открытого пункта
constexpr uint32_t MENU_SEPARATOR_COLOR = 0xFF808080u;

// --- Тулбар инструментов (эта сессия) ---
// Реальный битмап (ресурс 859, RLE4, декодирован offline — см.
// toolbar_bitmap_data.h/.cpp) и настоящий порядок 16 инструментов,
// прочитанный ИЗ КОДА paint.exe (массив 0x103e388, индексируемый в
// цикле "cmp edi,0x10" — подтверждено дизассемблированием, не догадка).
// 2 колонки подтверждены явным "push 2" перед настройкой тулбара.
constexpr int TOOLBAR_COLS = 2;
constexpr int TOOLBAR_ROWS = 8;
// Размер кнопки — картинка честная (28x16, подтверждено: 448/16=28
// точно), но реальный размер КНОПКИ (с отступами вокруг картинки)
// однозначно найти в дизассемблировании не удалось (SetSizes нигде не
// вызывается явно) — используем задокументированное стандартное
// MFC-соглашение (image + 7px по ширине, + 6px по высоте) как разумное
// приближение, скорректируем по факту визуальной проверки на реальном
// прогоне при необходимости.
//
// ИСПРАВЛЕНО (эта сессия): иконки инструментов имеют РАЗНУЮ ширину (см.
// g_toolIconRects в toolbar_bitmap_data.h — точная таблица, найдена
// методичной ручной проверкой по битмапу, а не единой формулой). Ширина
// кнопки теперь берёт максимальную ширину иконки (Zoom=26px) + отступ,
// чтобы вместить любую из 16 без обрезки. TOOLBAR_ICON_X_BIAS полностью
// убран — раньше он лечил симптом (кривой X_BIAS) вместо настоящей
// причины (неверная нарезка исходника).
constexpr int TOOLBAR_BTN_W = 34; // максимальная иконка (26px) + отступы
constexpr int TOOLBAR_BTN_H = win32::TOOL_IMAGE_H + 6; // 22
constexpr int TOOLBAR_W = TOOLBAR_BTN_W * TOOLBAR_COLS; // реальные пиксели, резервируем слева
constexpr uint32_t TOOLBAR_BG_COLOR = 0xFFC0C0C0u;
constexpr uint32_t TOOLBAR_SELECTED_COLOR = 0xFF000080u; // рамка выбранного инструмента

// Порядок ID — ИЗ КОДА paint.exe (0x103e388), не догадка:
constexpr uint16_t g_toolIds[16] = {
    0x26d, 0x26c, // Своб.выделение, Прямоуг.выделение
    0x27d, 0x26f, // Ластик, Заливка
    0x27f, 0x27e, // Пипетка, Лупа
    0x27c, 0x280, // Карандаш, Кисть
    0x273, 0x26e, // Аэрограф, Текст
    0x270, 0x274, // Линия, Кривая
    0x281, 0x278, // Прямоугольник, Многоугольник
    0x283, 0x27a, // Эллипс, Скруглённый прямоугольник
};
// Индекс текущего выбранного инструмента в g_toolIds (для рамки
// выделения) — по умолчанию Карандаш (индекс 6), честный дефолт
// paint.exe (подтверждено в прошлых сессиях: [0x103d74c] изначально
// указывает именно на Карандаш).
static int g_selectedToolIndex = 6;

// --- Палитра цветов (эта сессия) ---
// Настоящие 28 цветов MS Paint — прочитаны из кода paint.exe
// (0x103d260), не воссозданы визуально (см. win32::g_paintPalette в
// palette_data.h). Классическая раскладка — 2 ряда по 14 свотчей,
// снизу экрана. ГЕОМЕТРИЯ (размер свотча, положение) — первое
// приближение, НЕ проверено на реальном железе (тот же урок, что и с
// тулбаром — легко подстроить один блок констант, если будет нужно).
constexpr int PALETTE_COLS = 14;
constexpr int PALETTE_ROWS = 2;
constexpr int PALETTE_SWATCH_W = 24;
constexpr int PALETTE_SWATCH_H = 20;
// Слева от свотчей — индикатор текущих цветов (два перекрывающихся
// квадрата, как в оригинальном Paint): передний план сверху-слева, фон
// снизу-справа.
constexpr int PALETTE_INDICATOR_W = 40;
constexpr int PALETTE_H = PALETTE_SWATCH_H * PALETTE_ROWS; // резервируем снизу экрана
constexpr int PALETTE_W = PALETTE_INDICATOR_W + PALETTE_SWATCH_W * PALETTE_COLS;
constexpr uint32_t PALETTE_BG_COLOR = 0xFFC0C0C0u;
constexpr uint32_t PALETTE_BORDER_COLOR = 0xFF000000u;

// Текущие выбранные цвета (для отрисовки индикатора) — по умолчанию
// чёрный передний план/белый фон, честный дефолт paint.exe.
static uint32_t g_currentForegroundColor = 0xFF000000u;
static uint32_t g_currentBackgroundColor = 0xFFFFFFFFu;

// Эти три пункта относятся к старой системе WinHelp (.hlp-файлы) — она не
// поддерживается нативно современным Windows и падает даже в ОРИГИНАЛЬНОЙ
// игре на актуальной системе (подтверждено тестером на реальном ПК). Это не
// наш баг — блокируем клик по ним у себя (и рисуем серым), раз и в оригинале
// это не работает.
static bool IsDisabledHelpMenuId(uint16_t id) {
    return id == 590 || id == 591 || id == 592; // "Вызов справки"/"Предметный указатель"/"Пользование справкой"
}

static void DrawMenuBar(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_menuBarRects.clear();
    if (!win32::g_menuLoaded) return;

    for (int y = 0; y < MENU_BAR_H && y < screenH; y++)
        for (int x = 0; x < screenW; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = MENU_BG_COLOR;

    int x = 12;
    for (size_t i = 0; i < win32::g_loadedMenu.topLevel.size(); i++) {
        const auto& item = win32::g_loadedMenu.topLevel[i];
        int textW = win32::MeasureTextUtf16(item.text.data(), item.text.size(), MENU_FONT_SCALE);
        constexpr int padding = 14;
        int itemX1 = x, itemX2 = x + textW + padding * 2;

        bool isOpen = (g_openMenuIndex == static_cast<int>(i));
        if (isOpen) {
            for (int yy = 0; yy < MENU_BAR_H; yy++)
                for (int xx = itemX1; xx < itemX2 && xx < screenW; xx++)
                    framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = MENU_HOVER_COLOR;
        }
        uint32_t textColor = isOpen ? 0xFFFFFFFFu : MENU_TEXT_COLOR;
        int textY = (MENU_BAR_H - 7 * MENU_FONT_SCALE) / 2;
        win32::DrawTextUtf16(framebuf, screenW, screenH, itemX1 + padding, textY,
                              item.text.data(), item.text.size(), textColor, MENU_FONT_SCALE);

        g_menuBarRects.push_back({itemX1, 0, itemX2, MENU_BAR_H});
        x = itemX2;
    }

    // Выпадающий список текущего открытого пункта (если есть)
    g_dropdownRects.clear();
    if (g_openMenuIndex < 0 || g_openMenuIndex >= static_cast<int>(win32::g_loadedMenu.topLevel.size())) return;

    const auto& menu = win32::g_loadedMenu.topLevel[g_openMenuIndex];
    int dropX = g_menuBarRects[g_openMenuIndex].x1;
    int dropY = MENU_BAR_H;

    int maxTextW = 0;
    for (const auto& sub : menu.subItems) {
        int w = win32::MeasureTextUtf16(sub.text.data(), sub.text.size(), MENU_FONT_SCALE);
        if (!sub.shortcutText.empty())
            w += 30 + win32::MeasureTextUtf16(sub.shortcutText.data(), sub.shortcutText.size(), MENU_FONT_SCALE);
        if (w > maxTextW) maxTextW = w;
    }
    int dropW = maxTextW + 40;
    constexpr int itemH = 7 * MENU_FONT_SCALE + 12;
    constexpr int sepH = 7;

    int totalH = 0;
    for (const auto& sub : menu.subItems) totalH += sub.isSeparator ? sepH : itemH;

    for (int yy = 0; yy < totalH && dropY + yy < screenH; yy++)
        for (int xx = 0; xx < dropW && dropX + xx < screenW; xx++)
            framebuf[static_cast<size_t>(dropY + yy) * strideInPixels + (dropX + xx)] = MENU_BG_COLOR;

    int curY = dropY;
    for (const auto& sub : menu.subItems) {
        int h = sub.isSeparator ? sepH : itemH;
        if (!sub.isSeparator) {
            uint32_t itemColor = IsDisabledHelpMenuId(sub.id) ? 0xFF808080u : MENU_TEXT_COLOR;
            int ty = curY + (h - 7 * MENU_FONT_SCALE) / 2;
            win32::DrawTextUtf16(framebuf, screenW, screenH, dropX + 12, ty,
                                  sub.text.data(), sub.text.size(), itemColor, MENU_FONT_SCALE);
            if (!sub.shortcutText.empty()) {
                int shortcutW = win32::MeasureTextUtf16(sub.shortcutText.data(), sub.shortcutText.size(), MENU_FONT_SCALE);
                win32::DrawTextUtf16(framebuf, screenW, screenH, dropX + dropW - shortcutW - 12, ty,
                                      sub.shortcutText.data(), sub.shortcutText.size(), itemColor, MENU_FONT_SCALE);
            }
            g_dropdownRects.push_back({dropX, curY, dropX + dropW, curY + h});
        } else {
            int midY = curY + h / 2;
            for (int xx = 4; xx < dropW - 4; xx++)
                if (dropX + xx < screenW && midY < screenH)
                    framebuf[static_cast<size_t>(midY) * strideInPixels + (dropX + xx)] = MENU_SEPARATOR_COLOR;
            g_dropdownRects.push_back({0, 0, 0, 0}); // разделитель — клик по нему никуда не попадёт
        }
        curY += h;
    }
}

// Тулбар инструментов (эта сессия) — рисуем слева, ниже полосы меню,
// вертикальной полосой TOOLBAR_W пикселей на всю оставшуюся высоту
// экрана. Реальные иконки из настоящего ресурса 859 (см.
// toolbar_bitmap_data.h), масштабируем каждую 28x16 картинку под кнопку
// целочисленным ближайшим соседом (чтобы не мылить пиксель-арт).
static std::vector<MenuRect> g_toolbarButtonRects; // индекс == индекс в g_toolIds

static void DrawToolbar(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_toolbarButtonRects.clear();
    if (!win32::g_menuLoaded || !IsRunningPaint()) return; // тулбар — специфика именно Paint, не любого MFC-приложения с меню

    int menuReserve = MENU_BAR_H;
    for (int y = menuReserve; y < screenH; y++)
        for (int x = 0; x < TOOLBAR_W && x < screenW; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = TOOLBAR_BG_COLOR;

    for (int i = 0; i < 16; i++) {
        int col = i % TOOLBAR_COLS;
        int row = i / TOOLBAR_COLS;
        int btnX = col * TOOLBAR_BTN_W;
        int btnY = menuReserve + row * TOOLBAR_BTN_H;
        g_toolbarButtonRects.push_back({btnX, btnY, btnX + TOOLBAR_BTN_W, btnY + TOOLBAR_BTN_H});

        if (i == g_selectedToolIndex) {
            uint32_t c = TOOLBAR_SELECTED_COLOR;
            for (int yy = btnY; yy < btnY + TOOLBAR_BTN_H && yy < screenH; yy++)
                for (int xx = btnX; xx < btnX + TOOLBAR_BTN_W && xx < screenW; xx++)
                    framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = c;
        }

        // Картинка инструмента — честные пиксели из встроенного массива,
        // альфа=0 означает прозрачность (цвет-маска исходного ресурса).
        // Каждая иконка теперь имеет СВОЮ ширину (см. g_toolIconRects) —
        // никакого X_BIAS, честное центрирование по РЕАЛЬНОЙ ширине
        // конкретной иконки.
        const auto& rect = win32::g_toolIconRects[i];
        int imgX = btnX + (TOOLBAR_BTN_W - rect.srcW) / 2;
        int imgY = btnY + (TOOLBAR_BTN_H - win32::TOOL_IMAGE_H) / 2;
        for (int dy = 0; dy < win32::TOOL_IMAGE_H; dy++) {
            for (int dx = 0; dx < rect.srcW; dx++) {
                uint32_t argb = win32::g_toolbarBitmap[dy * win32::TOOLBAR_BITMAP_W + (rect.srcX + dx)];
                uint8_t a = (argb >> 24) & 0xFF;
                if (a == 0) continue; // прозрачно — не трогаем фон
                int px = imgX + dx, py = imgY + dy;
                if (px >= 0 && px < screenW && py >= 0 && py < screenH)
                    framebuf[static_cast<size_t>(py) * strideInPixels + px] = 0xFF000000u | (argb & 0xFFFFFFu);
            }
        }
    }
}

// Палитра цветов (эта сессия) — снизу экрана, полоса шириной PALETTE_W:
// слева индикатор текущих fg/bg (два перекрывающихся квадрата, как в
// оригинале), справа 28 честных свотчей из настоящей палитры paint.exe.
static std::vector<MenuRect> g_paletteSwatchRects; // индекс == индекс в win32::g_paintPalette

static void DrawPalette(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_paletteSwatchRects.clear();
    if (!win32::g_menuLoaded || !IsRunningPaint()) return; // палитра — специфика именно Paint

    int paletteY = screenH - PALETTE_H;
    if (paletteY < 0) return;

    for (int y = paletteY; y < screenH; y++)
        for (int x = 0; x < PALETTE_W && x < screenW; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = PALETTE_BG_COLOR;

    // Индикатор текущих цветов — фон снизу-справа (больше), передний
    // план сверху-слева (меньше, поверх) — классическая раскладка Paint.
    // РАЗМЕР РАЗНИЦЫ УВЕЛИЧЕН (обратная связь с реального железа): при
    // разнице всего в 6px два квадрата визуально сливались в один блок,
    // создавая впечатление "перепутанных" fg/bg — само взаимное
    // расположение (что меньше и сверху — это fg) было логически
    // верным всегда, проблема была чисто в неразличимости на маленьком
    // экране.
    {
        int bgSize = PALETTE_H - 6;
        int fgSize = bgSize - 14; // было -6, теперь заметно больше разница
        int bgX = 6, bgY = paletteY + 6;
        for (int yy = bgY; yy < bgY + bgSize && yy < screenH; yy++)
            for (int xx = bgX; xx < bgX + bgSize && xx < screenW; xx++) {
                bool border = (xx == bgX || xx == bgX + bgSize - 1 || yy == bgY || yy == bgY + bgSize - 1);
                framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = border ? PALETTE_BORDER_COLOR : g_currentBackgroundColor;
            }
        int fgX = 2, fgY = paletteY + 2; // 4px отступ от bg (было 2px) — заметнее на маленьком экране
        for (int yy = fgY; yy < fgY + fgSize && yy < screenH; yy++)
            for (int xx = fgX; xx < fgX + fgSize && xx < screenW; xx++) {
                bool border = (xx == fgX || xx == fgX + fgSize - 1 || yy == fgY || yy == fgY + fgSize - 1);
                framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = border ? PALETTE_BORDER_COLOR : g_currentForegroundColor;
            }
    }

    for (int i = 0; i < win32::PALETTE_COLOR_COUNT; i++) {
        int col = i % PALETTE_COLS;
        int row = i / PALETTE_COLS;
        int swX = PALETTE_INDICATOR_W + col * PALETTE_SWATCH_W;
        int swY = paletteY + row * PALETTE_SWATCH_H;
        g_paletteSwatchRects.push_back({swX, swY, swX + PALETTE_SWATCH_W, swY + PALETTE_SWATCH_H});
        uint32_t color = win32::g_paintPalette[i];
        for (int yy = swY; yy < swY + PALETTE_SWATCH_H && yy < screenH; yy++) {
            for (int xx = swX; xx < swX + PALETTE_SWATCH_W && xx < screenW; xx++) {
                bool border = (xx == swX || yy == swY); // тонкая рамка между свотчами
                framebuf[static_cast<size_t>(yy) * strideInPixels + xx] = border ? PALETTE_BORDER_COLOR : color;
            }
        }
    }
}

// Преобразование виртуальных координат (640x360) в реальные координаты
// экрана — та же трансформация (масштаб+сдвиг), что использует отрисовка,
// нужна, чтобы проверять клик джойконом по меню (оно в реальных координатах)
static void VirtualToReal(int vx, int vy, int& rx, int& ry) {
    rx = g_contentOffsetX + static_cast<int>((vx - g_contentBaseX) * g_contentScale);
    ry = g_contentOffsetY + static_cast<int>((vy - g_contentBaseY) * g_contentScale);
}

// Проверяет клик по меню (полоса или выпадающий список) в РЕАЛЬНЫХ
// координатах экрана. Возвращает true, если клик "проглочен" меню (не нужно
// пересылать его как обычный клик по игре).
static bool HandleMenuClick(int realX, int realY) {
    if (!win32::g_menuLoaded) return false;

    if (g_openMenuIndex >= 0) {
        for (size_t i = 0; i < g_dropdownRects.size(); i++) {
            const auto& r = g_dropdownRects[i];
            if (r.x2 <= r.x1) continue; // разделитель
            if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
                const auto& sub = win32::g_loadedMenu.topLevel[g_openMenuIndex].subItems[i];
                if (!sub.isSeparator && !IsDisabledHelpMenuId(sub.id)) {
                    uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
                    win32::g_messageQueue.Post(hwnd, win32::WM_COMMAND, sub.id, 0);
                }
                g_openMenuIndex = -1;
                return true;
            }
        }
        g_openMenuIndex = -1;
        return true;
    }

    for (size_t i = 0; i < g_menuBarRects.size(); i++) {
        const auto& r = g_menuBarRects[i];
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            g_openMenuIndex = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

// Клик по кнопке тулбара (эта сессия) — та же схема, что и HandleMenuClick:
// проверяем реальные координаты против сохранённых прямоугольников кнопок,
// и если попали — сигнализируем host_functions.cpp через
// g_requestedToolId (сам вызов SelectTool происходит ТАМ — у нас нет
// прямого доступа к гостевому коду отсюда).
static bool HandleToolbarClick(int realX, int realY) {
    if (!win32::g_menuLoaded || !IsRunningPaint()) return false;
    for (size_t i = 0; i < g_toolbarButtonRects.size(); i++) {
        const auto& r = g_toolbarButtonRects[i];
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            g_selectedToolIndex = static_cast<int>(i);
            win32::g_requestedToolId = g_toolIds[i];
            return true;
        }
    }
    return false;
}

// Клик по свотчу палитры (эта сессия) — isRightClick=false (левый/A) =
// передний план, true (правый/ZR) = фон, как в оригинальном Paint.
// Сигнализируем host_functions.cpp через g_requestedForeground/
// BackgroundColor (сам вызов SetForegroundColor/SetBackgroundColor
// происходит ТАМ — у нас нет прямого доступа к гостевому коду отсюда),
// и сразу обновляем локальный индикатор для отрисовки.
static bool HandlePaletteClick(int realX, int realY, bool isRightClick) {
    if (!win32::g_menuLoaded || !IsRunningPaint()) return false;
    for (size_t i = 0; i < g_paletteSwatchRects.size(); i++) {
        const auto& r = g_paletteSwatchRects[i];
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            uint32_t argb = win32::g_paintPalette[i]; // ARGB8888 — для ОТРИСОВКИ индикатора
            // ИСПРАВЛЕНО (обратная связь с реального железа): сигнал
            // g_requestedForegroundColor/BackgroundColor должен нести
            // настоящий COLORREF (0x00BBGGRR — именно это ожидает
            // честный SetForegroundColor), а не ARGB8888 напрямую. Раньше
            // белый (0xFFFFFFFF в ARGB) совпадал с sentinel'ом "нет
            // запроса" и потому НИКОГДА не применялся, а остальные
            // несимметричные цвета (R≠B) получали перепутанные местами
            // красный/синий каналы при разборе как "чужого" формата.
            uint32_t colorref = ((argb & 0xFFu) << 16) | (argb & 0xFF00u) | ((argb >> 16) & 0xFFu);
            if (isRightClick) {
                g_currentBackgroundColor = argb; // индикатор рисуем в ARGB, тут это ок
                win32::g_requestedBackgroundColor = colorref;
            } else {
                g_currentForegroundColor = argb;
                win32::g_requestedForegroundColor = colorref;
            }
            return true;
        }
    }
    return false;
}

// --- Модальные диалоги (Чемпионы, Особые и т.п.) — рисуем и обрабатываем
// клики сами, как и меню. Диалог рисуется В РЕАЛЬНЫХ координатах экрана,
// поверх всего остального, по центру.

// Перевод диалоговых единиц в пиксели — стандартная формула Win32 для
// системного шрифта диалогов ("MS Shell Dlg", ~8pt): baseUnitX=6, baseUnitY=13
constexpr int DLG_BASE_X = 6, DLG_BASE_Y = 13;
constexpr int DlgToPixelX(int du) { return du * DLG_BASE_X / 4; }
constexpr int DlgToPixelY(int du) { return du * DLG_BASE_Y / 8; }

constexpr int DLG_FONT_SCALE = 1; // было 2 — не влезал в компактную раскладку диалога, наезжал на соседние поля
constexpr int DLG_UI_SCALE = 2; // масштаб самого окна/контролов — ОТДЕЛЬНО от масштаба шрифта (раньше были ошибочно связаны одной переменной)
constexpr uint32_t DLG_BG_COLOR = 0xFFC0C0C0u;
constexpr uint32_t DLG_TEXT_COLOR = 0xFF000000u;
constexpr uint32_t DLG_BORDER_LIGHT = 0xFFFFFFFFu;
constexpr uint32_t DLG_BORDER_DARK = 0xFF808080u;
constexpr uint32_t DLG_EDIT_BG = 0xFFFFFFFFu;
constexpr uint32_t DLG_TITLEBAR_COLOR = 0xFF000080u; // классический тёмно-синий заголовок активного окна Windows
constexpr uint32_t DLG_TITLEBAR_TEXT = 0xFFFFFFFFu;
constexpr int DLG_TITLEBAR_H = 16;

static std::vector<MenuRect> g_dialogButtonRects; // индекс == индекс в g_currentDialog.controls
static int g_dialogOffsetX = 0, g_dialogOffsetY = 0; // где диалог реально нарисован на экране

static void FillRect(u32* fb, u32 stride, int screenW, int screenH, int x1, int y1, int x2, int y2, uint32_t color) {
    for (int y = y1; y < y2 && y < screenH; y++) {
        if (y < 0) continue;
        for (int x = x1; x < x2 && x < screenW; x++) {
            if (x < 0) continue;
            fb[static_cast<size_t>(y) * stride + x] = color;
        }
    }
}

// Простая рамка "приподнято" (светлая сверху/слева, тёмная снизу/справа) —
// классический вид кнопок и панелей в Windows 9x/XP classic
static void Draw3DBorder(u32* fb, u32 stride, int screenW, int screenH, int x1, int y1, int x2, int y2, bool raised) {
    uint32_t top = raised ? DLG_BORDER_LIGHT : DLG_BORDER_DARK;
    uint32_t bottom = raised ? DLG_BORDER_DARK : DLG_BORDER_LIGHT;
    FillRect(fb, stride, screenW, screenH, x1, y1, x2, y1 + 2, top);
    FillRect(fb, stride, screenW, screenH, x1, y1, x1 + 2, y2, top);
    FillRect(fb, stride, screenW, screenH, x1, y2 - 2, x2, y2, bottom);
    FillRect(fb, stride, screenW, screenH, x2 - 2, y1, x2, y2, bottom);
}

static void DrawDialog(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    g_dialogButtonRects.clear();
    if (!win32::g_dialogActive) return;

    const auto& dlg = win32::g_currentDialog;
    int dlgW = DlgToPixelX(dlg.cx) * DLG_UI_SCALE + 20;
    int dlgH = DlgToPixelY(dlg.cy) * DLG_UI_SCALE + 20;
    int baseX = (screenW - dlgW) / 2;
    int baseY = (screenH - dlgH) / 2;
    g_dialogOffsetX = baseX;
    g_dialogOffsetY = baseY;

    // Фон + рамка диалога
    FillRect(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + dlgW, baseY + dlgH, DLG_BG_COLOR);
    Draw3DBorder(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + dlgW, baseY + dlgH, true);

    // Заголовок диалога — теперь настоящая синяя полоса, как в реальной
    // Windows (активное окно) — раньше был просто текст поверх того же
    // серого фона, что и у самой игры, и терялся на общем фоне
    FillRect(framebuf, strideInPixels, screenW, screenH, baseX + 2, baseY + 2, baseX + dlgW - 2, baseY + 2 + DLG_TITLEBAR_H, DLG_TITLEBAR_COLOR);
    win32::DrawTextUtf16(framebuf, screenW, screenH, baseX + 6, baseY + 2 + (DLG_TITLEBAR_H - 7 * DLG_FONT_SCALE) / 2,
                          dlg.title.data(), dlg.title.size(), DLG_TITLEBAR_TEXT, DLG_FONT_SCALE);

    int contentTop = baseY + DLG_TITLEBAR_H + 12;

    for (size_t i = 0; i < dlg.controls.size(); i++) {
        const auto& c = dlg.controls[i];
        int cx1 = baseX + DlgToPixelX(c.x) * DLG_UI_SCALE;
        int cy1 = contentTop + DlgToPixelY(c.y) * DLG_UI_SCALE;
        int cx2 = cx1 + DlgToPixelX(c.cx) * DLG_UI_SCALE;
        int cy2 = cy1 + DlgToPixelY(c.cy) * DLG_UI_SCALE;

        // Реальный текст — либо то, что игра установила через SetDlgItemTextW,
        // либо исходный текст из ресурса (для статичных подписей типа "Новичок:")
        const std::vector<uint16_t>* text = &c.text;
        auto it = win32::g_dialogItemText.find(c.id);
        if (it != win32::g_dialogItemText.end()) text = &it->second;

        if (c.windowClass == "button") {
            FillRect(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, DLG_BG_COLOR);
            Draw3DBorder(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, true);
            int tw = win32::MeasureTextUtf16(text->data(), text->size(), DLG_FONT_SCALE);
            win32::DrawTextUtf16(framebuf, screenW, screenH, cx1 + (cx2 - cx1 - tw) / 2, cy1 + (cy2 - cy1 - 7 * DLG_FONT_SCALE) / 2,
                                  text->data(), text->size(), DLG_TEXT_COLOR, DLG_FONT_SCALE);
            g_dialogButtonRects.push_back({cx1, cy1, cx2, cy2});
        } else if (c.windowClass == "edit") {
            FillRect(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, DLG_EDIT_BG);
            Draw3DBorder(framebuf, strideInPixels, screenW, screenH, cx1, cy1, cx2, cy2, false);
            win32::DrawTextUtf16(framebuf, screenW, screenH, cx1 + 3, cy1 + (cy2 - cy1 - 7 * DLG_FONT_SCALE) / 2,
                                  text->data(), text->size(), DLG_TEXT_COLOR, DLG_FONT_SCALE);
            g_dialogButtonRects.push_back({cx1, cy1, cx2, cy2}); // теперь кликабельно — вызывает swkbd
        } else { // static и всё прочее — просто текст
            win32::DrawTextUtf16(framebuf, screenW, screenH, cx1, cy1,
                                  text->data(), text->size(), DLG_TEXT_COLOR, DLG_FONT_SCALE);
            g_dialogButtonRects.push_back({0, 0, 0, 0});
        }
    }
}

// Вызывает системную клавиатуру Switch (swkbd) для ввода текста в поле
// EDIT. Блокирующий вызов — как и обычные модальные диалоги, но экран во
// время ввода полностью занимает сама системная клавиатура, наше собственное
// рисование в этот момент не требуется (нативная реализация всё берёт на себя).
static bool ShowKeyboardForText(const std::vector<uint16_t>& initialText, std::vector<uint16_t>& outText) {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return false;

    swkbdConfigMakePresetDefault(&kbd);

    // Начальный текст — то, что уже было в поле (например, текущее значение
    // высоты/ширины/числа мин), чтобы не печатать заново с нуля
    if (!initialText.empty()) {
        std::string initialUtf8;
        for (uint16_t ch : initialText) initialUtf8 += static_cast<char>(ch < 128 ? ch : '?');
        swkbdConfigSetInitialText(&kbd, initialUtf8.c_str());
    }

    char outStr[128] = {0};
    rc = swkbdShow(&kbd, outStr, sizeof(outStr));
    swkbdClose(&kbd);

    if (R_FAILED(rc)) return false; // пользователь отменил ввод

    outText.clear();
    for (char* p = outStr; *p; p++) outText.push_back(static_cast<uint16_t>(static_cast<unsigned char>(*p)));
    return true;
}

// Клик по диалогу — возвращает true, если клик обработан (диалог модален —
// ЛЮБОЙ клик, пока он открыт, ему и принадлежит, даже мимо контролов)
static bool HandleDialogClick(int realX, int realY) {
    if (!win32::g_dialogActive) return true; // на всякий случай съедаем клик, хотя сюда не должны попасть

    for (size_t i = 0; i < g_dialogButtonRects.size(); i++) {
        const auto& r = g_dialogButtonRects[i];
        if (r.x2 <= r.x1) continue;
        if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
            const auto& ctrl = win32::g_currentDialog.controls[i];

            if (ctrl.windowClass == "edit") {
                // Поле ввода — вызываем клавиатуру, результат сохраняем в
                // g_dialogItemText (та же структура, что заполняет игра
                // через SetDlgItemTextW — GetDlgItemTextW потом прочитает
                // ровно то, что мы сюда положим, когда нажмут OK)
                std::vector<uint16_t> current;
                auto it = win32::g_dialogItemText.find(ctrl.id);
                current = (it != win32::g_dialogItemText.end()) ? it->second : ctrl.text;
                std::vector<uint16_t> result;
                if (ShowKeyboardForText(current, result)) {
                    win32::g_dialogItemText[ctrl.id] = std::move(result);
                }
            } else {
                // Кнопка — отправляем WM_COMMAND как раньше
                win32::g_messageQueue.Post(win32::g_dialogHwnd, win32::WM_COMMAND, ctrl.id, 0);
            }
            break;
        }
    }
    return true; // модально — клик мимо контролов тоже просто "проглатывается"
}

// --- Простой about-бокс (ShellAboutW) — заголовок + несколько строк текста
// + кнопка OK. В отличие от DialogBoxParamW тут нет шаблона ресурса, весь
// текст уже готовыми строками пришёл от самой функции (см. host_functions.cpp)
static MenuRect g_aboutOkButtonRect{0, 0, 0, 0};

static void DrawAboutBox(u32* framebuf, u32 strideInPixels, int screenW, int screenH) {
    if (!win32::g_aboutBoxActive) return;

    constexpr int SCALE = 1;
    constexpr int LINE_H = 7 * SCALE + 6;
    constexpr int PAD = 16;

    int titleW = win32::MeasureTextUtf16(win32::g_aboutBoxTitle.data(), win32::g_aboutBoxTitle.size(), SCALE);
    int maxLineW = titleW;
    for (auto& line : win32::g_aboutBoxLines) {
        int w = win32::MeasureTextUtf16(line.data(), line.size(), SCALE);
        if (w > maxLineW) maxLineW = w;
    }

    int boxW = maxLineW + PAD * 2;
    if (boxW < 140) boxW = 140;
    int boxH = DLG_TITLEBAR_H + 12 + static_cast<int>(win32::g_aboutBoxLines.size()) * LINE_H + PAD + 20 /*кнопка*/;

    int baseX = (screenW - boxW) / 2;
    int baseY = (screenH - boxH) / 2;

    FillRect(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + boxW, baseY + boxH, DLG_BG_COLOR);
    Draw3DBorder(framebuf, strideInPixels, screenW, screenH, baseX, baseY, baseX + boxW, baseY + boxH, true);

    // Синяя полоса заголовка — как у настоящего активного окна Windows
    FillRect(framebuf, strideInPixels, screenW, screenH, baseX + 2, baseY + 2, baseX + boxW - 2, baseY + 2 + DLG_TITLEBAR_H, DLG_TITLEBAR_COLOR);
    win32::DrawTextUtf16(framebuf, screenW, screenH, baseX + 6, baseY + 2 + (DLG_TITLEBAR_H - 7 * SCALE) / 2,
                          win32::g_aboutBoxTitle.data(), win32::g_aboutBoxTitle.size(), DLG_TITLEBAR_TEXT, SCALE);

    int y = baseY + DLG_TITLEBAR_H + 12;

    for (auto& line : win32::g_aboutBoxLines) {
        win32::DrawTextUtf16(framebuf, screenW, screenH, baseX + PAD, y, line.data(), line.size(), DLG_TEXT_COLOR, SCALE);
        y += LINE_H;
    }

    // Кнопка OK
    int btnW = 50, btnH = 18;
    int btnX = baseX + (boxW - btnW) / 2;
    int btnY = baseY + boxH - PAD - btnH + 4;
    FillRect(framebuf, strideInPixels, screenW, screenH, btnX, btnY, btnX + btnW, btnY + btnH, DLG_BG_COLOR);
    Draw3DBorder(framebuf, strideInPixels, screenW, screenH, btnX, btnY, btnX + btnW, btnY + btnH, true);
    uint16_t okText[] = {0x041E, 0x041A}; // "ОК"
    int okW = win32::MeasureTextUtf16(okText, 2, SCALE);
    win32::DrawTextUtf16(framebuf, screenW, screenH, btnX + (btnW - okW) / 2, btnY + (btnH - 7 * SCALE) / 2,
                          okText, 2, DLG_TEXT_COLOR, SCALE);
    g_aboutOkButtonRect = {btnX, btnY, btnX + btnW, btnY + btnH};
}

static bool HandleAboutBoxClick(int realX, int realY) {
    if (!win32::g_aboutBoxActive) return true;
    const auto& r = g_aboutOkButtonRect;
    if (realX >= r.x1 && realX < r.x2 && realY >= r.y1 && realY < r.y2) {
        win32::g_aboutBoxCloseRequested = true;
    }
    return true; // модально — любой клик "проглатывается"
}

static void DrawGdiScreenIntoFramebuffer(Framebuffer& fb, int cursorX, int cursorY)
{
    // Берём уменьшенное "виртуальное" разрешение (640x360)
    uint32_t screenDc = gdi::g_gdi.GetOrCreateScreenDC(640, 360);
    const gdi::Bitmap* screen = gdi::g_gdi.GetBitmapForDC(screenDc);
    if (!screen) return;

    // Область контента — сканируем пиксели в поисках РЕАЛЬНО нарисованного
    // (не просто чистой фоновой заливки). Раньше пробовали доверять сырым
    // цифрам из MoveWindow напрямую — оказалось, что это, похоже, ВНЕШНИЙ
    // размер окна (с "накладными расходами" вроде невидимого нам заголовка),
    // а не точная область, где реально рисует GDI — отсюда и лишняя пустая
    // область снизу, и курсор, зажатый в диапазон больше настоящего.
    //
    // Ищем не "не чёрные" (это работало, пока фон был чёрным), а "не
    // совпадающие с чистой заливкой" — теперь, когда фон закрашивается
    // классическим серым (см. FillScreenDC), именно так находим границы
    // того, что игра реально нарисовала поверх фона.
    constexpr uint32_t kBackgroundFillColor = 0xFFC0C0C0u;
    int minX = screen->width, minY = screen->height, maxX = -1, maxY = -1;
    for (int y = 0; y < screen->height; y++) {
        for (int x = 0; x < screen->width; x++) {
            uint32_t px = screen->pixels[static_cast<size_t>(y) * screen->width + x];
            if (px != kBackgroundFillColor && px != 0xFF000000u) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }

    int contentX, contentY, contentW, contentH;
    if (maxX < 0) {
        // Ничего ещё не нарисовано поверх фона — используем размеры окна,
        // если уже известны, иначе весь виртуальный холст как есть
        if (win32::g_lastWindowW > 0 && win32::g_lastWindowH > 0) {
            contentX = 0; contentY = 0;
            contentW = win32::g_lastWindowW; contentH = win32::g_lastWindowH;
        } else {
            contentX = 0; contentY = 0; contentW = screen->width; contentH = screen->height;
        }
    } else {
        contentX = minX; contentY = minY;
        contentW = maxX - minX + 1; contentH = maxY - minY + 1;
    }

    // Масштаб — заполняем высоту реального экрана (с небольшим отступом
    // сверху/снизу), сохраняя пропорции; по ширине НЕ растягиваем на весь
    // экран — центрируем в ОСТАВШЕЙСЯ (за вычетом тулбара) области.
    // Резервируем сверху место под полосу меню, слева — под тулбар
    // инструментов, снизу — под палитру цветов (если меню загружено —
    // тот же признак "честный UI готов", см. DrawMenuBar/DrawToolbar/
    // DrawPalette).
    constexpr int SCREEN_W = 1280, SCREEN_H = 720;
    constexpr int MARGIN = 24;
    int menuReserve = win32::g_menuLoaded ? MENU_BAR_H : 0; // меню — общая концепция, верно для ЛЮБОГО MFC-приложения с честным меню (включая Сапёра)
    int toolbarReserve = (win32::g_menuLoaded && IsRunningPaint()) ? TOOLBAR_W : 0;
    int paletteReserve = (win32::g_menuLoaded && IsRunningPaint()) ? PALETTE_H : 0;
    int availableV = SCREEN_H - MARGIN * 2 - menuReserve - paletteReserve;
    float scale = static_cast<float>(availableV) / static_cast<float>(contentH);
    if (scale > 6.0f) scale = 6.0f; // не растягиваем чрезмерно, если контент совсем маленький

    int outW = static_cast<int>(contentW * scale);
    int outH = static_cast<int>(contentH * scale);
    int offsetX = toolbarReserve + (SCREEN_W - toolbarReserve - outW) / 2;
    int offsetY = menuReserve + (SCREEN_H - menuReserve - paletteReserve - outH) / 2;

    // Сохраняем для обратного преобразования тача (см. PumpHostFrame)
    g_contentScale = scale;
    g_contentOffsetX = offsetX;
    g_contentOffsetY = offsetY;
    g_contentBaseX = contentX;
    g_contentBaseY = contentY;

    u32 stride;
    u32* framebuf = static_cast<u32*>(framebufferBegin(&fb, &stride));
    u32 strideInPixels = stride / sizeof(u32);

    // Чистим экран в чёрный — контент теперь не покрывает всю площадь
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            framebuf[static_cast<size_t>(y) * strideInPixels + x] = 0xFF000000u;

    // Рисуем содержимое с нужным масштабом и сдвигом (центрирование)
    for (int oy = 0; oy < outH; oy++) {
        int screenY = offsetY + oy;
        if (screenY < 0 || screenY >= SCREEN_H) continue;
        int srcY = contentY + static_cast<int>(oy / scale);
        if (srcY >= screen->height) continue;
        for (int ox = 0; ox < outW; ox++) {
            int screenX = offsetX + ox;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            int srcX = contentX + static_cast<int>(ox / scale);
            if (srcX >= screen->width) continue;
            uint32_t argb = screen->pixels[static_cast<size_t>(srcY) * screen->width + srcX];
            framebuf[static_cast<size_t>(screenY) * strideInPixels + screenX] = ArgbToSwitchRgba(argb);
        }
    }

    // Меню — поверх содержимого игры
    DrawMenuBar(framebuf, strideInPixels, SCREEN_W, SCREEN_H);
    DrawToolbar(framebuf, strideInPixels, SCREEN_W, SCREEN_H);
    DrawPalette(framebuf, strideInPixels, SCREEN_W, SCREEN_H);

    // Диалог — поверх меню (модальный)
    DrawDialog(framebuf, strideInPixels, SCREEN_W, SCREEN_H);

    // About-бокс — поверх абсолютно всего, включая обычные диалоги
    DrawAboutBox(framebuf, strideInPixels, SCREEN_W, SCREEN_H);

    // Курсор — ПОСЛЕДНИМ из всех, чтобы был виден поверх абсолютно всего
    // (раньше рисовался до меню/диалога и прятался под ними)
    int cx = offsetX + static_cast<int>((cursorX - contentX) * scale);
    int cy = offsetY + static_cast<int>((cursorY - contentY) * scale);
    DrawCursor(framebuf, strideInPixels, cx, cy);

    framebufferEnd(&fb);
}

// --- Реальный ввод: стик + кнопка A (замена тача — см. историю разработки:
// hidGetTouchScreenStates уронил всю консоль целиком, а не только наше
// приложение — значит там реальная ошибка на уровне памяти, а не наш
// аккуратный abort(). Стик/кнопки — это API, что мы УЖЕ проверили на
// реальном железе много раз без единого сбоя, поэтому пока переключаемся
// на него, курсор + клик вместо прямого тача.)
static PadState g_pad;
static Framebuffer g_fb;
static int g_cursorX = 320, g_cursorY = 180; // старт — центр виртуального экрана 640x360
static float g_cursorXf = 320.0f, g_cursorYf = 180.0f; // дробная позиция — для плавного аналогового движения
static bool g_wasTouching = false;
static uint32_t g_lastTouchX = 0, g_lastTouchY = 0;

// Переключение с текстовой консоли на графику теперь происходит ОДИН РАЗ,
// НАПЕРЁД, из main() — простого, неглубокого контекста (см. подробный
// комментарий у PumpHostFrame про то, почему лениво изнутри было опасно).

// Это и есть win32::g_pumpHostFrame — GetMessage реально зовёт эту функцию
// в цикле, пока ждёт событие (см. host_functions.cpp). За один "тик":
// двигаем курсор стиком (плавно, аналогово) ИЛИ тачем, проверяем клик и
// кнопку выхода, перерисовываем текущий кадр на экран.
//
// ПРО ТАЧ: настоящая причина прошлых крашей — 10 недостающих опкодов x86
// декодера (нашли и починили отдельно), а НЕ тач-API — любой клик тогда
// падал одинаково, независимо от способа ввода. Так что тач возвращаем.
static win32::HostFrameResult PumpHostFrame()
{
    win32::HostFrameResult result;

    padUpdate(&g_pad);
    u64 kDown = padGetButtonsDown(&g_pad);

    if (kDown & HidNpadButton_Plus)
    {
        result.shouldQuit = true;
        return result;
    }

    // --- Стик: ПЛАВНОЕ (аналоговое) движение — чем сильнее отклонён стик,
    // тем быстрее курсор, а не резкий фиксированный шаг как раньше ---
    HidAnalogStickState stick = padGetStickPos(&g_pad, 0);
    constexpr int32_t DEADZONE = 4000;
    constexpr float MAX_SPEED = 0.7f; // пикселей виртуального экрана за тик при полном отклонении (было 1.1 — ещё ~37% медленнее по просьбе тестера)

    auto axisSpeed = [](int32_t v) -> float {
        int32_t av = v >= 0 ? v : -v;
        if (av <= DEADZONE) return 0.0f;
        float t = static_cast<float>(av - DEADZONE) / static_cast<float>(32767 - DEADZONE);
        float speed = t * t * MAX_SPEED; // квадратичная кривая — у центра ещё мягче, у края быстрее
        return v > 0 ? speed : -speed;
    };

    g_cursorXf += axisSpeed(stick.x);
    g_cursorYf -= axisSpeed(stick.y); // ось Y стика обычно инвертирована относительно экрана

    // Зажимаем курсор в границы РЕАЛЬНОГО игрового поля (точные размеры из
    // MoveWindow), а не жёстко в 640x360 — раньше справа оставался огромный
    // "запас хода" стика в пустоту (за пределами реального поля, но ещё в
    // пределах виртуального холста), а слева такого запаса не было вообще —
    // отсюда и ощущение "стены" именно слева, хотя формально это было просто
    // отсутствие лишнего запаса, а не баг границы как таковой.
    int maxCursorX = (win32::g_lastWindowW > 0) ? win32::g_lastWindowW - 1 : 639;
    int maxCursorY = (win32::g_lastWindowH > 0) ? win32::g_lastWindowH - 1 : 359;

    // РАСШИРЕНИЕ (эта сессия): курсор должен физически доставать до
    // полосы меню (сверху), тулбара инструментов (слева) и палитры
    // цветов (снизу) — раньше был жёстко заперт в [0, max], что делало
    // эти области недостижимыми даже при том, что они уже честно
    // рисовались. Используем те же сохранённые g_contentScale/offsetX/Y,
    // что и обратное преобразование тача — переводим "реальный (0,0)" и
    // "реальный (SCREEN_W-1,SCREEN_H-1)" обратно в виртуальные
    // координаты, это и есть минимально/максимально достижимые точки.
    float minCursorXf = 0.0f, minCursorYf = 0.0f;
    float maxCursorYf = static_cast<float>(maxCursorY);
    if (g_contentScale > 0.0f) {
        minCursorXf = static_cast<float>(g_contentBaseX) - static_cast<float>(g_contentOffsetX) / g_contentScale;
        minCursorYf = static_cast<float>(g_contentBaseY) - static_cast<float>(g_contentOffsetY) / g_contentScale;
        maxCursorYf = static_cast<float>(g_contentBaseY) + (719.0f - static_cast<float>(g_contentOffsetY)) / g_contentScale;
    }

    if (g_cursorXf < minCursorXf) g_cursorXf = minCursorXf;
    if (g_cursorXf > maxCursorX) g_cursorXf = static_cast<float>(maxCursorX);
    if (g_cursorYf < minCursorYf) g_cursorYf = minCursorYf;
    if (g_cursorYf > maxCursorYf) g_cursorYf = maxCursorYf;

    g_cursorX = static_cast<int>(g_cursorXf);
    g_cursorY = static_cast<int>(g_cursorYf);

    // Держим экран живым (не каждый тик ожидания, а раз в 10 — разумная
    // защита от лишних вызовов графики без реальной необходимости)
    static int s_frameThrottle = 0;
    if (++s_frameThrottle >= 10) {
        s_frameThrottle = 0;
        DrawGdiScreenIntoFramebuffer(g_fb, g_cursorX, g_cursorY);
    }

    // Раньше отпускание планировалось через ФИКСИРОВАННОЕ число тиков,
    // независимо от того, отпустил ли пользователь кнопку по-настоящему —
    // из-за этого удержание A вело себя не как удержание тача (тач держит
    // ЛКМ ровно пока палец на экране, а кнопка "клевала" сразу). Теперь
    // отслеживаем РЕАЛЬНОЕ состояние кнопки (padGetButtons, не только
    // "только что нажата") — полностью так же, как уже сделано для тача.
    u64 kHeld = padGetButtons(&g_pad);
    bool aHeldNow = (kHeld & HidNpadButton_A) != 0;
    bool zrHeldNow = (kHeld & HidNpadButton_ZR) != 0;

    static bool s_aGameClickActive = false; // true, если ТЕКУЩЕЕ нажатие A реально дошло до игры (не перехвачено меню)
    static bool s_zrGameClickActive = false;
    static uint32_t s_aHwnd = 0, s_aLParam = 0;
    static uint32_t s_zrHwnd = 0, s_zrLParam = 0;

    // Клик по нажатию A (джойкон) — сначала диалог (если открыт — модальный,
    // забирает ВСЕ клики), потом меню, и только если ни то ни другое не
    // сработало — обычный игровой клик.
    if (kDown & HidNpadButton_A)
    {
        int realCX, realCY;
        VirtualToReal(g_cursorX, g_cursorY, realCX, realCY);
        bool handled = win32::g_aboutBoxActive ? HandleAboutBoxClick(realCX, realCY)
                     : win32::g_dialogActive ? HandleDialogClick(realCX, realCY)
                     : HandleMenuClick(realCX, realCY)
                     || HandleToolbarClick(realCX, realCY)
                     || HandlePaletteClick(realCX, realCY, false);

        if (!handled)
        {
            uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONDOWN, 1, lParam);
            s_aHwnd = hwnd;
            s_aLParam = lParam;
            s_aGameClickActive = true;
        }
    }
    else if (!aHeldNow && s_aGameClickActive)
    {
        // Кнопку реально отпустили — только теперь шлём UP
        win32::g_messageQueue.Post(s_aHwnd, win32::WM_LBUTTONUP, 0, s_aLParam);
        s_aGameClickActive = false;
    }
    else if (aHeldNow && s_aGameClickActive)
    {
        // Кнопка ВСЁ ЕЩЁ удерживается — реальная мышь в этот момент шлёт
        // WM_MOUSEMOVE (с зажатой левой кнопкой в wParam), и, похоже, именно
        // на это реагирует визуальная обратная связь игры при удержании
        // (например, предпросмотр соседних клеток) — просто DOWN...UP без
        // этих промежуточных сообщений визуально неотличим от быстрого тапа,
        // даже если сам UP теперь реально приходит с задержкой.
        static int s_moveThrottle = 0;
        if (++s_moveThrottle >= 3) {
            s_moveThrottle = 0;
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(s_aHwnd, win32::WM_MOUSEMOVE, 1 /*MK_LBUTTON*/, lParam);
        }
    }

    // Правый клик (флажок) по ZR — та же логика удержания. Сначала
    // проверяем те же UI-зоны, что и для A (палитра — правый клик =
    // цвет фона; меню/тулбар не имеют отдельного поведения на правый
    // клик, но всё равно проверяем, чтобы клик по ним не улетал в игру
    // с координатами вне холста).
    if (kDown & HidNpadButton_ZR)
    {
        int realCX, realCY;
        VirtualToReal(g_cursorX, g_cursorY, realCX, realCY);
        bool zrHandled = win32::g_aboutBoxActive ? HandleAboutBoxClick(realCX, realCY)
                        : win32::g_dialogActive ? HandleDialogClick(realCX, realCY)
                        : HandleMenuClick(realCX, realCY)
                          || HandleToolbarClick(realCX, realCY)
                          || HandlePaletteClick(realCX, realCY, true);

        if (!zrHandled)
        {
            uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(hwnd, win32::WM_RBUTTONDOWN, 2, lParam); // MK_RBUTTON
            s_zrHwnd = hwnd;
            s_zrLParam = lParam;
            s_zrGameClickActive = true;
        }
    }
    else if (!zrHeldNow && s_zrGameClickActive)
    {
        win32::g_messageQueue.Post(s_zrHwnd, win32::WM_RBUTTONUP, 0, s_zrLParam);
        s_zrGameClickActive = false;
    }
    else if (zrHeldNow && s_zrGameClickActive)
    {
        static int s_moveThrottleZr = 0;
        if (++s_moveThrottleZr >= 3) {
            s_moveThrottleZr = 0;
            uint32_t lParam = ((static_cast<uint32_t>(g_cursorY) & 0xFFFFu) << 16) | (static_cast<uint32_t>(g_cursorX) & 0xFFFFu); // маскируем до 16 бит каждую координату — критично теперь, когда курсор может быть отрицательным (тулбар/меню слева/сверху), иначе OR ломает верхние 16 бит
            win32::g_messageQueue.Post(s_zrHwnd, win32::WM_MOUSEMOVE, 2 /*MK_RBUTTON*/, lParam);
        }
    }

    // --- Тач-экран: касание напрямую двигает курсор В ТУ ТОЧКУ и кликает ---
    HidTouchScreenState touchState = {0};
    bool isTouchingNow = false;
    uint32_t touchX = 0, touchY = 0;
    static bool s_touchStartedOnMenu = false; // весь текущий тач "принадлежит" меню?

    if (hidGetTouchScreenStates(&touchState, 1) && touchState.count > 0)
    {
        isTouchingNow = true;
        // Реальные координаты тача — в физических пикселях (1280x720 в
        // портативном режиме).
        float realX = static_cast<float>(touchState.touches[0].x);
        float realY = static_cast<float>(touchState.touches[0].y);

        if (!g_wasTouching)
        {
            // Самый первый тик касания — сначала about-бокс, потом диалог,
            // потом меню, до пересчёта в виртуальные координаты
            s_touchStartedOnMenu = win32::g_aboutBoxActive
                ? HandleAboutBoxClick(static_cast<int>(realX), static_cast<int>(realY))
                : win32::g_dialogActive
                ? HandleDialogClick(static_cast<int>(realX), static_cast<int>(realY))
                : HandleMenuClick(static_cast<int>(realX), static_cast<int>(realY))
                  || HandleToolbarClick(static_cast<int>(realX), static_cast<int>(realY))
                  || HandlePaletteClick(static_cast<int>(realX), static_cast<int>(realY), false);
        }

        if (!s_touchStartedOnMenu)
        {
            // Обратное преобразование той же трансформации (масштаб + сдвиг
            // под центрирование), что использует отрисовка — иначе палец
            // попадал бы не туда, где реально нарисовано содержимое.
            int vx = g_contentBaseX + static_cast<int>((realX - g_contentOffsetX) / g_contentScale);
            int vy = g_contentBaseY + static_cast<int>((realY - g_contentOffsetY) / g_contentScale);
            int maxVx = (win32::g_lastWindowW > 0) ? win32::g_lastWindowW - 1 : 639;
            int maxVy = (win32::g_lastWindowH > 0) ? win32::g_lastWindowH - 1 : 359;
            if (vx < 0) vx = 0;
            if (vx > maxVx) vx = maxVx;
            if (vy < 0) vy = 0;
            if (vy > maxVy) vy = maxVy;
            touchX = static_cast<uint32_t>(vx);
            touchY = static_cast<uint32_t>(vy);

            // Курсор сразу переезжает под палец — и визуально, и для следующего клика
            g_cursorXf = static_cast<float>(touchX);
            g_cursorYf = static_cast<float>(touchY);
            g_cursorX = static_cast<int>(touchX);
            g_cursorY = static_cast<int>(touchY);
        }
    }

    uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
    if (!s_touchStartedOnMenu)
    {
        if (isTouchingNow && !g_wasTouching)
        {
            // Только что коснулись — WM_LBUTTONDOWN
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONDOWN, 1,
                                        (touchY << 16) | touchX);
        }
        else if (!isTouchingNow && g_wasTouching)
        {
            // Только что отпустили — WM_LBUTTONUP по последним известным координатам
            win32::g_messageQueue.Post(hwnd, win32::WM_LBUTTONUP, 0,
                                        (g_lastTouchY << 16) | g_lastTouchX);
        }
        else if (isTouchingNow && g_wasTouching)
        {
            // НАЙДЕННЫЙ БАГ (эта сессия): палец УЖЕ на экране и просто
            // двигается — раньше здесь не было ВООБЩЕ НИКАКОГО сообщения,
            // только DOWN (первое касание) и UP (отпускание). Реальная
            // мышь в этот момент шлёт WM_MOUSEMOVE с зажатой левой
            // кнопкой в wParam — без этого Paint (и вообще любой код,
            // ожидающий WM_MOUSEMOVE во время "перетаскивания") никогда
            // не узнаёт о движении пальца. Троттлинг — как у кнопки A.
            static int s_touchMoveThrottle = 0;
            if (++s_touchMoveThrottle >= 3) {
                s_touchMoveThrottle = 0;
                win32::g_messageQueue.Post(hwnd, win32::WM_MOUSEMOVE, 1 /*MK_LBUTTON*/,
                                            (touchY << 16) | touchX);
            }
        }
    }

    g_wasTouching = isTouchingNow;
    if (isTouchingNow) { g_lastTouchX = touchX; g_lastTouchY = touchY; }

    return result;
}

// Вся "тяжёлая" логика — на ОТДЕЛЬНОМ потоке с явно большим стеком (см.
// main() ниже). Найдено через addr2line+objdump: реальный краш происходил
// на самой обычной инструкции записи в локальный стек (stp xzr,xzr,[sp,#32])
// внутри DecodeCore — это переполнение стека НАСТОЯЩЕГО ARM64-выполнения,
// не имеющее отношения к нашей эмулируемой памяти. Вложенный вызов WndProc
// (CallGuestFunctionStdcall создаёт ещё один Interpreter поверх уже
// глубокой цепочки) исчерпывал маленький стек главного потока по умолчанию.
static void GameThreadFunc(void* arg)
{
    (void)arg;

    // ПРЯМАЯ ДИАГНОСТИКА (оставляем — пригодилась): адрес стека этого потока.
    int stackMarker = 0;
    win32::g_threadStackBase = (void*)&stackMarker;

    win32::g_pumpHostFrame = &PumpHostFrame;
    // Лимит шагов теперь по сути "неограниченный" — реальный выход
    // происходит по кнопке + (через PumpHostFrame -> GetMessage -> WM_QUIT),
    // а не по счётчику инструкций.
    RunInterpreter(g_selectedExePath.c_str(), 40, 100000000);
}

// --- Звук через audout ---
// Простой пул из нескольких слотов-буферов (короткие игровые звуки, их
// редко нужно больше 2-3 одновременно). Каждый слот — отдельный выровненный
// буфер (audout требует выравнивание по 0x1000), которым мы владеем, пока
// audout не сообщит, что буфер отыгран (audoutGetReleasedAudioOutBuffer).
constexpr int kAudioSlots = 4;
struct AudioSlot {
    AudioOutBuffer outBuf;
    void* rawMem = nullptr;
    bool inUse = false;
};
static AudioSlot g_audioSlots[kAudioSlots];

static void ReclaimReleasedAudioBuffers() {
    AudioOutBuffer* released;
    u32 count;
    while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&released, &count)) && count > 0) {
        for (auto& slot : g_audioSlots) {
            if (slot.inUse && &slot.outBuf == released) slot.inUse = false;
        }
    }
}

static void PlayAudioImpl(const std::vector<int16_t>& samples) {
    if (samples.empty()) return;
    ReclaimReleasedAudioBuffers();

    int slotIdx = -1;
    for (int i = 0; i < kAudioSlots; i++) if (!g_audioSlots[i].inUse) { slotIdx = i; break; }
    if (slotIdx < 0) slotIdx = 0; // все заняты — жертвуем самым старым, короткие звуки, не страшно

    AudioSlot& slot = g_audioSlots[slotIdx];
    size_t dataSize = samples.size() * sizeof(int16_t);
    size_t alignedSize = (dataSize + 0xFFFu) & ~0xFFFu; // audout требует выравнивание по 0x1000

    if (slot.rawMem) { free(slot.rawMem); slot.rawMem = nullptr; }
    slot.rawMem = memalign(0x1000, alignedSize);
    if (!slot.rawMem) return; // не хватило памяти — просто без звука в этот раз, не крашимся

    memcpy(slot.rawMem, samples.data(), dataSize);
    if (alignedSize > dataSize) memset(static_cast<uint8_t*>(slot.rawMem) + dataSize, 0, alignedSize - dataSize);

    slot.outBuf.next = nullptr;
    slot.outBuf.buffer = slot.rawMem;
    slot.outBuf.buffer_size = alignedSize;
    slot.outBuf.data_size = static_cast<u32>(dataSize);
    slot.outBuf.data_offset = 0;
    slot.inUse = true;

    audoutAppendAudioOutBuffer(&slot.outBuf);
}

// --- Выбор исполняемого файла при старте ---
// Программы ищутся в romfs (вшиты в .nro) и на карте памяти в
// sdmc:/switch-recompiler/ — там же, где реестр. На карте программу
// можно положить и в свою подпапку вместе с её DLL и данными
// (sdmc:/switch-recompiler/Game/game.exe): так готовый .nro из CI
// запускает свои программы без пересборки.
static bool HasExeExtension(const std::string& name) {
    if (name.size() < 5) return false;
    std::string ext = name.substr(name.size() - 4);
    for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return ext == ".exe";
}

// Добавляет в result полные пути .exe из dir; depth > 0 — заходить в подпапки
static void CollectExecutables(const std::string& dir, int depth, std::vector<std::string>& result) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> subdirs;
    while (dirent* entry = readdir(d)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth > 0) subdirs.push_back(full + "/"); }
        else if (HasExeExtension(name)) result.push_back(full);
    }
    closedir(d);
    for (const std::string& sub : subdirs) CollectExecutables(sub, depth - 1, result);
}

static std::vector<std::string> ListExecutables() {
    std::vector<std::string> romfs, sd;
    CollectExecutables("romfs:/", 0, romfs);
    CollectExecutables(std::string(win32::kRegistryFolderPath) + "/", 1, sd);
    std::sort(romfs.begin(), romfs.end());
    std::sort(sd.begin(), sd.end());
    romfs.insert(romfs.end(), sd.begin(), sd.end());
    return romfs;
}

// Текстовый пикер: крестовина/стик вверх-вниз — выбор пункта, A — подтвердить.
// Если файл всего один — даже не спрашиваем, сразу его и берём.
static void PickExecutableFile() {
    std::vector<std::string> files = ListExecutables();
    if (files.empty()) {
        printf("[!] No .exe found in romfs or %s/ - using %s by default\n",
               win32::kRegistryFolderPath, g_selectedExePath.c_str());
        consoleUpdate(NULL);
        return;
    }
    if (files.size() == 1) {
        g_selectedExePath = files[0];
        return; // единственный файл — нечего выбирать
    }

    int selected = 0;
    for (;;)
    {
        consoleClear();
        printf("=== Switch Recompiler - choose a program ===\n\n");
        for (size_t i = 0; i < files.size(); i++) {
            printf(" %s %s\n", (static_cast<int>(i) == selected) ? ">" : " ", files[i].c_str());
        }
        printf("\n(Up/Down - select, A - launch)\n");
        consoleUpdate(NULL);

        bool chosen = false;
        while (!chosen)
        {
            padUpdate(&g_pad);
            u64 kDown = padGetButtonsDown(&g_pad);
            if (kDown & HidNpadButton_Up) { selected = (selected - 1 + static_cast<int>(files.size())) % static_cast<int>(files.size()); break; }
            if (kDown & HidNpadButton_Down) { selected = (selected + 1) % static_cast<int>(files.size()); break; }
            if (kDown & HidNpadButton_A) { chosen = true; break; }
            svcSleepThread(16'000'000); // ~16мс — не крутим цикл вхолостую на 100% CPU
        }
        if (chosen) {
            g_selectedExePath = files[selected];
            return;
        }
    }
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    hidInitializeTouchScreen(); // без этого тач может отдавать некорректное состояние

    // Реестр на SD: создаём папку (если её ещё нет — mkdir просто вернёт
    // ошибку, это не страшно, обрабатываем как "уже была") и загружаем
    // сохранённые ранее значения (рекорды, настройки). Если файла ещё нет
    // (самый первый запуск) — LoadFromFile честно вернёт false, ничего
    // страшного, реестр просто останется пустым, как раньше.
    mkdir(win32::kRegistryFolderPath, 0777);
    win32::g_registry.LoadFromFile(win32::kRegistryFilePath);

    // Звук: инициализируем audout один раз, заранее (главный поток, до
    // запуска интерпретатора) — та же логика, что и с графикой: простая,
    // неглубокая инициализация из безопасного места.
    Result audRc = audoutInitialize();
    if (R_SUCCEEDED(audRc)) {
        audoutStartAudioOut();
        win32::g_playAudioImpl = &PlayAudioImpl;
    }
    // Если audoutInitialize не удался — просто играем без звука (не критично,
    // win32::g_playAudioImpl останется nullptr, PlaySoundW тихо ничего не сыграет)

    Result rc = romfsInit();
    if (R_FAILED(rc))
    {
        // Ошибка ДО того, как мы вообще начали — остаёмся в текстовом
        // режиме консоли целиком, графику даже не трогаем.
        printf("[!] romfsInit failed: 0x%x\n", rc);
        printf("\nPress PLUS to exit.\n");
        consoleUpdate(NULL);
        while (appletMainLoop())
        {
            padUpdate(&g_pad);
            if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 0;
    }

    printf("=== Switch Recompiler ===\n");
    consoleUpdate(NULL);

    // Выбор исполняемого файла — до переключения в графику, пока консоль
    // ещё доступна (проще всего показать текстовый список именно тут)
    PickExecutableFile();
    printf("Launching: %s\n", g_selectedExePath.c_str());
    consoleUpdate(NULL);

    // Переключаемся в графику ЗАРАНЕЕ, ОДИН РАЗ, из простого неглубокого
    // верхнеуровневого кода на ГЛАВНОМ потоке — НОВАЯ гипотеза (после того,
    // как выяснилось, что без графики вообще краха нет): framebufferCreate
    // мог иметь скрытую привязку к потоку, который его создал. Раньше мы
    // создавали его ВНУТРИ игрового потока — теперь создаём здесь, на
    // главном, а рисовать (framebufferBegin/End) будет уже игровой поток.
    printf("[+] Switching to graphics output - press PLUS to exit.\n");
    consoleUpdate(NULL);
    consoleExit(NULL);
    cpu::g_consoleAvailable = false;
    framebufferCreate(&g_fb, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&g_fb);
    g_graphicsInitialized = true;

    // Запускаем всю игровую логику на отдельном потоке с большим стеком
    // (2 МБ — щедрый запас).
    Thread gameThread;
    constexpr size_t GAME_THREAD_STACK_SIZE = 2 * 1024 * 1024;
    Result threadRc = threadCreate(&gameThread, GameThreadFunc, nullptr, nullptr,
                                     GAME_THREAD_STACK_SIZE, 0x2C, -2);
    if (R_FAILED(threadRc))
    {
        // Тут консоль уже закрыта — просто закрываем framebuffer и выходим
        framebufferClose(&g_fb);
        romfsExit();
        return 1;
    }

    threadStart(&gameThread);
    threadWaitForExit(&gameThread);
    threadClose(&gameThread);

    romfsExit();
    framebufferClose(&g_fb);
    if (win32::g_playAudioImpl) audoutExit();
    return 0;
}

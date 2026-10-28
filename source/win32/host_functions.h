#pragma once
// host_functions.h
//
// Механизм подмены импортированных функций (Win32 API / CRT) на настоящие
// C++ реализации поверх Switch/libnx. Это НЕ фейковые пустышки — это
// честная переимплементация того же контракта под другую ОС (тот же
// принцип, что использует Wine на Linux).
//
// Как это работает:
// 1. При загрузке PE каждый слот IAT (Import Address Table) для функции,
//    которую мы умеем реализовать, заполняется не реальным адресом (которого
//    и не существует), а "фальшивым" host-адресом из специального диапазона.
// 2. Interpreter, прежде чем декодировать байты по EIP, проверяет — не один
//    ли это из наших host-адресов. Если да — вызывает C++ реализацию
//    напрямую, а не пытается исполнять несуществующий машинный код.
// 3. После — сами эмулируем "возврат из функции", соблюдая нужное
//    соглашение о вызовах (stdcall/cdecl).

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <map>
#include "../cpu/cpu_state.h"
#include "../cpu/guest_memory.h"
#include "crt_globals.h"
#include "../loader/resource_parser.h"
#include "menu_parser.h"
#include "dialog_parser.h"
#include "virtual_registry.h"
#include "gdi.h"
#include "window_manager.h"
#include "message_loop.h"

namespace win32 {

// Реальный x86-код почти гарантированно никогда не сгенерирует такой адрес
// сам по себе (это далеко за пределами обычного 32-битного адресного
// пространства процесса, которое обычно живёт в районе 0x00400000-0x7FFFFFFF)
constexpr uint32_t HOST_FUNCTION_BASE = 0xF0000000u;
constexpr uint32_t HOST_FUNCTION_STRIDE = 0x10u;

struct HostCallOutcome {
    uint32_t returnValue = 0;   // кладём в EAX, как будто функция реально это вернула
    bool requestExit = false;   // true — гостевая программа попросила завершиться
    int32_t exitCode = 0;
    // true — функция сама выставила EIP/ESP/EAX (longjmp и т.п.): диспетчер
    // не эмулирует возврат и не трогает регистры.
    bool customReturn = false;
};

using HostFn = HostCallOutcome (*)(cpu::CpuState& cpu, cpu::IGuestMemory& mem);

// Соглашение о вызовах — определяет, кто чистит стек от аргументов
enum class CallConv {
    Stdcall, // callee сам чистит стек (стандарт для чистого Win32 API)
    Cdecl,   // чистит вызывающий код (стандарт для CRT-функций типа exit/rand)
};

struct HostFunctionEntry {
    const char* dllName;
    const char* funcName; // пусто, если функция регистрируется ПО ОРДИНАЛУ (см. ordinal ниже)
    uint32_t argBytes;   // суммарный размер аргументов на стеке, в байтах
    CallConv callConv;   // от этого зависит, чистим ли мы стек сами при "возврате"
    HostFn fn;
    uint16_t ordinal = 0; // валиден только если funcName пуст (MFC и подобные экспортируют без имён)
};

extern const HostFunctionEntry g_hostFunctionTable[];
extern const size_t g_hostFunctionTableSize;

// --- Динамический пул заглушек для импортов ПО ОРДИНАЛУ (MFC и подобные
// библиотеки, у которых нет символьных имён экспортов) ---
//
// Основная таблица выше — СТАТИЧЕСКАЯ (constexpr-массив), туда добавляются
// только функции с ИЗВЕСТНОЙ, реально написанной реализацией. Для MFC (614
// функций по номеру, без имён) выделять статический слот под каждую заранее
// нецелесообразно — вместо этого ординалы регистрируются ДИНАМИЧЕСКИ, по
// мере встречи в импортах конкретного .exe, все указывая на ОДНУ общую
// диагностическую заглушку (см. Impl_MfcOrdinalStub в .cpp): она печатает,
// какой именно ординал был вызван (и с каким состоянием регистров), и
// возвращает безопасное значение по умолчанию — так можно за один прогон
// увидеть всю цепочку нужных вызовов, а не одну функцию за раз.
constexpr uint32_t MFC_STUB_BASE = HOST_FUNCTION_BASE + 0x100000u; // с большим запасом после основной таблицы

// --- Простая "куча" в гостевой памяти (bump-аллокатор) ---
//
// До сих пор ни одной нашей функции не требовалось реально ВЫДЕЛЯТЬ память
// в гостевом адресном пространстве (только читать/писать по уже готовым
// адресам). _wcsdup — первая такая функция. Полноценный malloc/free с
// переиспользованием освобождённых блоков нам пока не нужен — берём
// САМЫЙ простой вариант: постоянно растущий указатель, ничего не
// освобождаем. Для наших целей (несколько десятков-сотен небольших
// аллокаций за время работы программы) этого с огромным запасом достаточно.
constexpr uint32_t HEAP_SIZE = 0x200000u; // 2 МиБ
extern uint32_t g_heapBase; // начало кучи — выставляется вместе с g_crtGlobalsBase при старте
extern uint32_t g_heapNext; // текущая позиция аллокации (растёт только вверх)

// Выделяет `bytes` байт (округляя вверх до кратного 8) и возвращает адрес.
uint32_t SimpleGuestAlloc(uint32_t bytes);

// Слот страховочной заглушки. Два вида:
//  - MFC по ординалу (funcName пуст): очистка стека из mfc42u_abi_table.h;
//  - Win32 по имени (funcName задан): очистка из win32_api_abi_table.h.
struct MfcStubSlot {
    std::string dllName;
    uint16_t ordinal;
    std::string funcName;      // пусто для заглушек по ординалу
    uint16_t cleanupBytes = 0; // только для заглушек по имени
    bool abiKnown = true;      // false: размер аргументов неизвестен, вызов = громкий FatalError с именем
};
extern std::vector<MfcStubSlot> g_mfcStubSlots;

// Регион памяти под "внутреннее состояние" MFC (thread-state/module-state
// и подобные структуры, к которым MFC обращается через один и тот же
// указатель отовсюду). У нас нет исходников MFC, чтобы знать точную
// раскладку такой структуры — вместо этого выделяем один достаточно большой
// постоянный буфер и всегда возвращаем указатель на него; конкретные поля
// (по смещениям, которые встретим по ходу трассировки) будут просто читаться/
// писаться как обычная память — этого достаточно, чтобы не падать.
constexpr uint32_t MFC_STATE_SIZE = 0x4000;
extern uint32_t g_mfcStateBase; // выставляется вместе с g_crtGlobalsBase при старте

// Возвращает host-адрес для (dllName, ordinal) — переиспользует уже
// выделенный слот, если такой ординал уже встречался, иначе выделяет новый.
uint32_t AllocateMfcStubSlot(const std::string& dllName, uint16_t ordinal);

// То же для импорта ПО ИМЕНИ, у которого нет реализации (раньше IAT-слот
// оставался 0 и вызов был безымянным крашем "адрес 0").
uint32_t AllocateNamedStubSlot(const std::string& dllName, const std::string& funcName);

// Внешние источники платформенных данных — на Switch подключаются в main.cpp
// через реальные libnx-вызовы; в хостовых тестах можно подставить моки.
extern uint32_t (*g_getTickCountImpl)();

// Audio device ownership and checked submission: runtime/audio_output.h.

// Вывод консольных программ гостя (WriteFile/WriteConsole в stdout/stderr).
// nullptr — в stdout/stderr хоста, если консоль доступна.
extern void (*g_consoleWriteImpl)(const char* data, size_t len, bool isStderr);

// Разобранное меню игры (см. menu_parser.h) — заполняется при вызове
// LoadMenuW, main.cpp сам рисует полосу меню/выпадающие списки и обрабатывает
// по ним клики (в реальной Windows это делает системный USER32, у нас его нет).
extern ParsedMenu g_loadedMenu;
extern bool g_menuLoaded;

extern int32_t g_lastWindowW;
extern int32_t g_lastWindowH;

// Состояние ТЕКУЩЕГО открытого модального диалога (см. dialog_parser.h) —
// main.cpp рисует его сам (как и меню) и кладёт в очередь WM_COMMAND при
// клике по кнопкам. У нас нет настоящего системного USER32-диалога, но
// DialogBoxParamW теперь реально блокирует выполнение (свой мини message
// loop) и вызывает DlgProc гостевого кода — как и положено по-настоящему.
extern uint32_t g_requestedToolId,g_requestedForegroundColor,g_requestedBackgroundColor;
extern uint32_t g_paintCDC,g_paintView;
extern bool g_dialogEndRequested;
extern int32_t g_dialogEndResult;
extern bool g_dialogActive;
extern ParsedDialog g_currentDialog;
extern uint32_t g_dialogHwnd;
// Текст, реально установленный игрой через SetDlgItemTextW (id контрола ->
// UTF-16 текст) — перекрывает пустой текст из ресурса при отрисовке.
extern std::map<uint16_t, std::vector<uint16_t>> g_dialogItemText;

// Состояние простого "about-бокса" (ShellAboutW) — отдельно от DialogBoxParamW,
// потому что тут НЕТ шаблона ресурса: текст приходит прямо аргументами
// функции (имя приложения + многострочное описание). main.cpp рисует
// заголовок + несколько строк текста + кнопку OK.
extern bool g_aboutBoxActive;
extern std::vector<uint16_t> g_aboutBoxTitle;
extern std::vector<std::vector<uint16_t>> g_aboutBoxLines; // уже разбито по строкам
extern uint32_t g_aboutBoxHwnd;
// main.cpp выставляет true при клике по кнопке OK в about-боксе
extern bool g_aboutBoxCloseRequested;
extern uint32_t g_imageBase;

// Доступ к разложенному по RVA образу и корню дерева ресурсов — нужен для
// LoadStringW и (в будущем) диалогов/меню. Указывает на буфер, который
// main.cpp держит живым на всё время исполнения (не владеем им сами).
extern const uint8_t* g_imageBuffer;
extern size_t g_imageBufferSize;
extern uint32_t g_resourceDirRva;

// Адрес нашего фейкового TEB (см. main.cpp) — нужен для вложенных вызовов
// гостевого кода (WndProc и т.п.), чтобы fs:/gs: адресация внутри них
// работала так же, как в основном потоке исполнения.
extern uint32_t g_tebAddr;

// То, что реальное железо (Switch) сообщает нам за один "тик" ожидания
// внутри GetMessage. x,y — уже в НАШИХ виртуальных координатах (640x360),
// конвертацию из реальных координат тача делает вызывающая сторона (main.cpp).
// Гость завершает процесс (ExitProcess/exit), возможно из вложенного
// вызова: циклы исполнения всех уровней останавливаются.
extern bool g_guestExitRequested;
// Печатать каждый вызов host-функции (отладка: winrun -t)
extern bool g_traceHostCalls;
extern int32_t g_guestExitCode;

struct HostFrameResult {
    bool shouldQuit = false;    // пользователь запросил выход (кнопка +, HOME и т.п.)
    bool hasTouchEvent = false; // появилось новое событие тача с прошлого опроса
    uint32_t message = 0;       // WM_LBUTTONDOWN / WM_LBUTTONUP / WM_MOUSEMOVE
    uint32_t x = 0, y = 0;      // координаты события (виртуальные 640x360)
};

// GetMessage реально БЛОКИРУЕТСЯ (как и настоящая функция), опрашивая этот
// колбэк, пока не появится либо тач-событие, либо запрос на выход. Внутри
// колбэка (реализуется в main.cpp) — обновление pad/touch, живая перерисовка
// текущего кадра на реальный экран. Если колбэк не установлен (например, в
// хостовых тестах) — GetMessage ведёт себя как раньше: синтезирует WM_QUIT.
extern HostFrameResult (*g_pumpHostFrame)();

// Дополнительные таблицы реализаций из других файлов. Регистрация —
// статическим инициализатором в файле реализации:
//   static const bool kRegistered = RegisterHostFunctions(kTable, sizeof(kTable) / sizeof(kTable[0]));
// Записи, зарегистрированные позже, переопределяют более ранние и
// статическую таблицу host_functions.cpp. Имена DLL — без учёта регистра.
bool RegisterHostFunctions(const HostFunctionEntry* entries, size_t count);
// Записи слоя действуют, только пока слой включён (EnableHostLayer). Слой
// "ui2" — новые USER32/GDI32 (win32/user32_*.cpp, gdi32_module.cpp); без
// него работает прежний HLE окон и GDI, на котором держатся Сапёр и Paint.
bool RegisterHostFunctionsLayer(const HostFunctionEntry* entries, size_t count, const char* layer);
void EnableHostLayer(const char* layer, bool on);
bool IsHostLayerEnabled(const char* layer);
constexpr const char* kUiLayer = "ui2";

size_t HostFunctionCount();
const HostFunctionEntry& GetHostFunction(int index);

int FindHostFunction(const char* dllName, const char* funcName);
// Как FindHostFunction, но для функций, зарегистрированных ПО ОРДИНАЛУ
// (см. HostFunctionEntry::ordinal) — так экспортируется MFC и подобное.
int FindHostFunctionByOrdinal(const char* dllName, uint16_t ordinal);

inline uint32_t IndexToHostAddress(int index) {
    return HOST_FUNCTION_BASE + static_cast<uint32_t>(index) * HOST_FUNCTION_STRIDE;
}

// -1, если addr не похож на наш host-адрес
int HostAddressToIndex(uint32_t addr);

// Если cpu.eip указывает на host-функцию — выполняет её и эмулирует возврат
// (с учётом cdecl/stdcall), возвращает true. Если eip обычный — false,
// ничего не трогает, вызывающий код должен продолжить как обычно (Decode+Execute).
bool TryHandleHostCall(cpu::CpuState& cpu, cpu::IGuestMemory& mem, HostCallOutcome& outOutcome);

// Сколько импортированных функций реально нашли реализацию в таблице —
// удобно для диагностики ("покрытие Win32 API")
struct PatchStats {
    int totalImports = 0;
    int patched = 0;
};

} // namespace win32

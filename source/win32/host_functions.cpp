#include <strings.h>
#include "mfc42u_abi_table.h"
#include "win32_api_abi_table.h"
#include "host_functions.h"
#include "seh_dispatch.h"
#include "menu_parser.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <string>
#include <cstdlib> // настоящие rand()/srand() из toolchain'а — не выдумываем свой PRNG

namespace win32 {

uint32_t (*g_getTickCountImpl)() = nullptr;
uint32_t g_imageBase = 0; // выставляется один раз перед стартом исполнения
const uint8_t* g_imageBuffer = nullptr;
size_t g_imageBufferSize = 0;
uint32_t g_resourceDirRva = 0;
uint32_t g_tebAddr = 0;
HostFrameResult (*g_pumpHostFrame)() = nullptr;
bool g_guestExitRequested = false;
bool g_traceHostCalls = false;
int32_t g_guestExitCode = 0;
void (*g_playAudioImpl)(const std::vector<int16_t>& samples) = nullptr;

// Сигнал "выбрать инструмент" (эта сессия) — main.cpp выставляет ID
// инструмента при клике по тулбару, а мы (там, где есть честный доступ
// к cpu/mem — внутри Impl_Mfc5710_Run) реально находим объект
// инструмента в реестре [0x103e900] и вызываем настоящий SelectTool
// (0x1015199). main.cpp НЕ может звать гостевой код напрямую — только
// сигнализировать через эту переменную (тот же паттерн, что и
// g_paintCDC/g_paintView).
uint32_t g_requestedToolId = 0;

// Сигналы "установить цвет" (эта сессия) — main.cpp выставляет их при
// клике по свотчу палитры (левый клик/A = передний план, правый/ZR =
// фон), а здесь (где есть честный доступ к cpu/mem) реально вызываем
// настоящие SetForegroundColor(0x100c30f)/SetBackgroundColor(0x100c255).
// 0xFFFFFFFF — "нет запроса" (настоящий COLORREF всегда имеет старший
// байт 0x00, так что это безопасный сентинел, не может совпасть с
// реальным цветом).
uint32_t g_requestedForegroundColor = 0xFFFFFFFFu;

// Перенесены сюда из середины файла (эта сессия) — жили внутри
// анонимного namespace, что не ломало сборку раньше только потому, что
// ничего СНАРУЖИ файла к ним напрямую не обращалось (main.cpp общается
// только через сигнальные переменные). Понадобилась внешняя видимость
// для честного сквозного теста Пипетки (напрямую красим пиксель через
// g_paintCDC перед симуляцией клика).
uint32_t g_paintCDC = 0;  // честный CDC для повторных OnDraw, создаётся в OnFileNew
uint32_t g_paintView = 0; // pView — ИЗВЕСТНЫЙ рабочий view, используем его для
                          // рисования НЕЗАВИСИМО от того, какой именно hwnd
                          // формально получил сообщение (найдено при тесте на
                          // реальном железе: сообщение может приходить на hwnd
                          // фрейма, а не view — у фрейма +0x40 никогда не
                          // устанавливался).
uint32_t g_requestedBackgroundColor = 0xFFFFFFFFu;

// Разобранное меню — main.cpp рисует полосу меню и выпадающие списки сам
// (у нас нет системного слоя USER32, который в реальной Windows отрисовывал
// бы меню за приложение) и обрабатывает клики по нему.
ParsedMenu g_loadedMenu;
bool g_menuLoaded = false;

// Точные размеры игрового окна — из последнего вызова MoveWindow. main.cpp
// использует их вместо ненадёжного сканирования "не чёрных" пикселей для
// определения области контента (масштаб/центрирование/курсор).
int32_t g_lastWindowW = 0;
int32_t g_lastWindowH = 0;

bool g_dialogActive = false;
ParsedDialog g_currentDialog;
uint32_t g_dialogHwnd = 0;
std::map<uint16_t, std::vector<uint16_t>> g_dialogItemText;
bool g_dialogEndRequested = false;
int32_t g_dialogEndResult = 0;

bool g_aboutBoxActive = false;
std::vector<uint16_t> g_aboutBoxTitle;
std::vector<std::vector<uint16_t>> g_aboutBoxLines;
uint32_t g_aboutBoxHwnd = 0;
bool g_aboutBoxCloseRequested = false;

uint32_t g_heapBase = 0;
uint32_t g_heapNext = 0;

uint32_t SimpleGuestAlloc(uint32_t bytes) {
    bytes = (bytes + 7u) & ~7u; // выравнивание по 8 байт — безопасный дефолт
    uint32_t addr = g_heapNext;
    g_heapNext += bytes;
    return addr;
}

namespace {

// stdcall/cdecl на входе в host-функцию выглядят одинаково: ESP указывает на
// адрес возврата, [esp+4] — первый аргумент, [esp+8] — второй, и т.д.
// Разница проявляется только при "возврате" (см. TryHandleHostCall).
uint32_t ReadArg(cpu::CpuState& cpu, cpu::IGuestMemory& mem, int argIndex) {
    return mem.Read32(cpu.ESP() + 4 + static_cast<uint32_t>(argIndex) * 4);
}

// Читает ASCII-строку из гостевой памяти (с ограничением длины на случай
// повреждённых данных — не даём себе уйти в бесконечный цикл)
std::string ReadCString(cpu::IGuestMemory& mem, uint32_t addr, size_t maxLen = 256) {
    std::string result;
    if (addr == 0) return result;
    for (size_t i = 0; i < maxLen; i++) {
        uint8_t c = mem.Read8(addr + static_cast<uint32_t>(i));
        if (c == 0) break;
        result.push_back(static_cast<char>(c));
    }
    return result;
}

// --- msvcrt.dll (CRT, cdecl) ---

HostCallOutcome Impl_Exit(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome result;
    result.requestExit = true;
    result.exitCode = static_cast<int32_t>(ReadArg(cpu, mem, 0));
    return result;
}

HostCallOutcome Impl_Rand(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    // Настоящий rand() из нашего же рантайма — честная реализация того же
    // контракта (псевдослучайное число 0..RAND_MAX), не имитация
    result.returnValue = static_cast<uint32_t>(std::rand());
    return result;
}

HostCallOutcome Impl_Srand(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome result;
    std::srand(ReadArg(cpu, mem, 0));
    return result;
}

// unsigned int _controlfp(unsigned int newControl, unsigned int mask) —
// управление флагами FPU (округление, точность, маскирование исключений).
// Возвращаем правдоподобное дефолтное значение control word — для обычного
// геймплея точные биты почти никогда не важны.
HostCallOutcome Impl_ControlFp(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0x9001F; // стандартный дефолт MSVC control word
    return result;
}

// void __set_app_type(int) — сообщает CRT, что мы GUI/консольное приложение.
// Чисто внутренний учёт CRT, на геймплей не влияет.
HostCallOutcome Impl_SetAppType(cpu::CpuState&, cpu::IGuestMemory&) {
    return HostCallOutcome{};
}

// int* __p__fmode(void) — возвращает АДРЕС глобальной переменной _fmode.
// Настоящая реализация того же контракта: указываем на настоящую ячейку
// памяти в нашей CRT-globals области, а не на выдуманный адрес в никуда.
HostCallOutcome Impl_PFmode(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_crtGlobalsBase + CRT_OFFSET_FMODE;
    return result;
}

HostCallOutcome Impl_PCommode(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_crtGlobalsBase + CRT_OFFSET_COMMODE;
    return result;
}

// void __setusermatherr(handler) — регистрирует кастомный обработчик
// математических ошибок (domain error и т.п.). Мы такие ошибки пока не
// эмулируем в принципе, так что честный no-op — не заглушка "на отвяжись",
// а корректное поведение при отсутствии самого механизма math error.
HostCallOutcome Impl_SetUserMatherr(cpu::CpuState&, cpu::IGuestMemory&) {
    return HostCallOutcome{};
}

// void _initterm(_PVFV* start, _PVFV* end) — вызывает таблицу
// C-инициализаторов (обычно статические C++ объекты) между двумя указателями.
// Сознательно НЕ реализовано по-настоящему: чтобы сделать это честно,
// пришлось бы уметь вызывать гостевой код ИЗНУТРИ host-функции (рекурсивный
// заход в сам интерпретатор) — механизм, которого пока нет. Для простых
// C-программ вроде сапёра эта таблица почти наверняка пуста или
// несущественна. Если в будущей игре это окажется критично — сюда нужно
// будет вернуться и добавить настоящий вызов через интерпретатор.
// void _initterm(PVFV* pfbegin, PVFV* pfend) — КРИТИЧНО для C++/MFC-программ:
// перебирает массив указателей на функции в диапазоне [pfbegin, pfend) и
// вызывает каждую ненулевую. Это ровно тот механизм, которым MSVC-рантайм
// вызывает конструкторы ГЛОБАЛЬНЫХ C++-объектов ДО main/WinMain — включая,
// например, глобальный объект `theApp` в MFC-приложениях. Раньше эта
// функция была пустышкой — значит, глобальные объекты вообще не
// конструировались, и весь код, ищущий theApp, находил мусор вместо
// настоящего, проинициализированного объекта.
HostCallOutcome Impl_InitTerm(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pfbegin = ReadArg(cpu, mem, 0);
    uint32_t pfend = ReadArg(cpu, mem, 1);

    for (uint32_t addr = pfbegin; addr < pfend; addr += 4) {
        uint32_t funcPtr = mem.Read32(addr);
        // 0 и 0xFFFFFFFF — обе стандартные "пустые" метки в таких таблицах
        // (используются компилятором/линкером как заполнители)
        if (funcPtr != 0 && funcPtr != 0xFFFFFFFFu) {
            CallGuestFunctionStdcall(cpu, mem, funcPtr, {}, g_tebAddr, g_tebAddr);
        }
    }

    return HostCallOutcome{};
}

// int __getmainargs(int* argc, char*** argv, char*** envp, int, void*) —
// заполняет argc/argv/envp для main(). Реальная работа: пишем в гостевую
// память по адресам, которые нам передали как аргументы.
HostCallOutcome Impl_GetMainArgs(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pArgc = ReadArg(cpu, mem, 0);
    uint32_t pArgv = ReadArg(cpu, mem, 1);
    uint32_t pEnv  = ReadArg(cpu, mem, 2);
    // args 3 (_DoWildCard) и 4 (_StartupInfo) сознательно игнорируем —
    // не влияют на геймплей-корректность в нашем случае

    mem.Write32(pArgc, 1); // argc = 1 (только имя программы, без аргументов)
    mem.Write32(pArgv, g_crtGlobalsBase + CRT_OFFSET_ARGV);
    mem.Write32(pEnv, g_crtGlobalsBase + CRT_OFFSET_ENV);

    HostCallOutcome result;
    result.returnValue = 0; // success
    return result;
}

// int __wgetmainargs(int* argc, wchar_t*** wargv, wchar_t*** wenv, int, void*) —
// широкая версия __getmainargs, тот же контракт, просто пишет в WARGV/WENV
// (заполненные InitCrtGlobals). Нужна MFC-приложениям (paint.exe и
// подобным) — они стартуют через wmain-стиль, а не обычный main.
HostCallOutcome Impl_WGetMainArgs(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pArgc = ReadArg(cpu, mem, 0);
    uint32_t pArgv = ReadArg(cpu, mem, 1);
    uint32_t pEnv  = ReadArg(cpu, mem, 2);

    mem.Write32(pArgc, 1); // argc = 1 (только имя программы)
    mem.Write32(pArgv, g_crtGlobalsBase + CRT_OFFSET_WARGV);
    mem.Write32(pEnv, g_crtGlobalsBase + CRT_OFFSET_WENV);

    HostCallOutcome result;
    result.returnValue = 0; // success
    return result;
}

// void _cexit(void) / void _c_exit(void) — C-level часть завершения
// программы (flush потоков, atexit-хендлеры). Честный no-op: реальное
// завершение процесса уже обрабатывает exit()/_exit(), эти — просто часть
// внутренней процедуры выхода CRT, которая обычно вызывается ПОСЛЕ основной
// логики exit(), так что для нас несущественны.
// void _cexit(void) / void _c_exit(void) — выполняет C-уровневую очистку
// (закрытие потоков вывода, atexit-обработчики) БЕЗ завершения процесса на
// уровне ОС (в отличие от exit()/_exit()) — в реальном Windows это важное
// различие для смешанных native/managed сценариев. Для НАШЕЙ эмуляции
// этого различия эффективно не существует: как только программа доходит
// до _cexit/_c_exit, реальной дальнейшей полезной работы уже не будет
// (сценарий именно такой — финальный шаг перед возвратом из mainCRTStartup
// к несуществующему "вызывающему"). Раньше это была пустышка, ничего не
// делающая — из-за чего выполнение проваливалось ЕЩЁ ДАЛЬШЕ, до самого
// верха стека вызовов, где мы физически не готовили осмысленный "адрес
// возврата" (в реальной Windows точка входа не возвращается через
// обычный ret — процесс завершается через ExitProcess). Теперь — как и
// exit() — честно останавливаем интерпретацию.
HostCallOutcome Impl_CExit(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.requestExit = true;
    result.exitCode = 0;
    return result;
}

// int _XcptFilter(int xcptnum, void* pxcptinfo) — используется внутри
// __except()-фильтров при реальной структурной обработке исключений.
// У нас пока нет диспетчера настоящих аппаратных исключений (это отдельная,
// более поздняя фаза), так что этот путь кодом винmine.exe в норме не
// достигается — на всякий случай возвращаем "продолжить поиск обработчика".
HostCallOutcome Impl_XcptFilter(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0; // EXCEPTION_CONTINUE_SEARCH
    return result;
}

// _except_handler3 — сам обработчик SEH-фрейма. Как и _XcptFilter, реально
// вызывается только диспетчером аппаратных исключений, которого у нас пока
// нет. Регистрируем как честную, вызываемую (хоть и минимальную) функцию —
// а не оставляем как "нереализовано", чтобы код, который просто ЧИТАЕТ её
// адрес (для установки SEH-фрейма), получал валидный, а не нулевой адрес.
HostCallOutcome Impl_ExceptHandler3(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1; // ExceptionContinueSearch
    return result;
}

// --- KERNEL32.dll (Win32, stdcall) ---

HostCallOutcome Impl_GetTickCount(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_getTickCountImpl ? g_getTickCountImpl() : 0;
    return result;
}

// void GetSystemTimeAsFileTime(LPFILETIME lpTime) — записывает 8-байтовое
// время (100-нс интервалы с 1601 года). Нам не нужна настоящая точность —
// это чаще всего используется просто как источник "случайности" (например,
// для генерации стековой cookie /GS — именно так впервые встретили эту
// функцию в paint.exe). Берём что-то правдоподобное и меняющееся, на основе
// уже имеющегося тика — этого достаточно для честного контракта функции.
HostCallOutcome Impl_GetSystemTimeAsFileTime(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pTime = ReadArg(cpu, mem, 0);
    uint32_t tick = g_getTickCountImpl ? g_getTickCountImpl() : 0;
    // Псевдо-FILETIME: не претендует на реальную дату, просто правдоподобно
    // большое и меняющееся 64-битное число (единицы 100нс с 1601 — реальные
    // значения такого порядка величины дают "разумные" непустые данные)
    uint64_t fakeFileTime = 0x01D00000'00000000ull + (static_cast<uint64_t>(tick) * 10000ull);
    if (pTime != 0) {
        mem.Write32(pTime, static_cast<uint32_t>(fakeFileTime & 0xFFFFFFFFu));
        mem.Write32(pTime + 4, static_cast<uint32_t>(fakeFileTime >> 32));
    }
    HostCallOutcome result;
    return result;
}

// DWORD GetCurrentProcessId(void) — правдоподобный, стабильный "номер
// процесса" (реальное значение не важно ни для чего в нашей эмуляции —
// один процесс, один "PID")
HostCallOutcome Impl_GetCurrentProcessId(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1234; // произвольный, но стабильный правдоподобный PID
    return result;
}

// DWORD GetCurrentThreadId(void) — аналогично, стабильный правдоподобный ID
HostCallOutcome Impl_GetCurrentThreadId(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 5678;
    return result;
}

// MFC42u.DLL ординал 1165 — судя по трассировке (вызывающий код пишет байт
// на смещении +0x14 и DWORD на смещении +0x2040 от результата), возвращает
// указатель на некую внутреннюю структуру состояния MFC (thread-state/
// module-state). Настоящей раскладки мы не знаем — возвращаем указатель на
// наш зарезервированный буфер (см. MFC_STATE_SIZE/g_mfcStateBase), этого
// достаточно, чтобы чтения/записи полей просто работали как обычная память.
HostCallOutcome Impl_Mfc1165_GetState(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_mfcStateBase;
    return result;
}

// MFC42u.DLL ординал 415 — судя по трассировке, это __thiscall-конструктор
// (ECX=this) с ОДНИМ аргументом на стеке (в наблюдаемом случае — 0xa).
// КРИТИЧНО: в отличие от общей заглушки (которая предполагает 0 байт
// аргументов на стеке — вызывающий код сам чистит), эта функция реально
// чистит СВОЙ аргумент сама (как stdcall) — иначе стек рассинхронизируется
// на 4 байта, что при дальнейших pop esi/pop edi в вызывающем коде читает
// не те значения (именно так мы и нашли эту проблему — esi неожиданно
// становился 0). Возвращаем ECX по конструкторной конвенции MSVC.
HostCallOutcome Impl_Mfc415_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = cpu.Get(cpu::Reg32::ECX);
    return result;
}

// MFC42u.DLL ординал 561 — та же история, что и с 415: __thiscall с ОДНИМ
// аргументом на стеке (в наблюдаемом случае — 0, через "xor esi,esi; push
// esi"). Обнаружено через реальный баг: без явной очистки этого байта стек
// рассинхронизировался на 4 байта, и в итоге "pop ebx" в куда более позднем
// коде читал не то значение — приводило к вечному циклу в СОВЕРШЕННО другом
// месте программы (парсинг командной строки). Возвращаем ECX (конструктор).
// Указатель на глобальный объект приложения (theApp, CWinApp-производный) —
// НЕ хардкодим адрес под конкретную сборку paint.exe. Вместо этого
// перехватываем его ДИНАМИЧЕСКИ, ровно в момент вызова ординала 561 (тот
// самый базовый конструктор — подтверждено дизассемблированием: вызывается
// РОВНО ОДИН раз за всю программу, и ровно ДО того, как конструктор
// theApp/CWinApp перезаписывает vtable своим собственным значением —
// классический паттерн "сначала базовый конструктор"). Такой подход
// переносим на ЛЮБОЕ MFC-приложение той же версии MFC42u.DLL, а не только
// на этот конкретный скомпилированный paint.exe.
uint32_t g_theAppPtr = 0;
uint32_t g_emptyCStringData = 0; // общий буфер под "пустой" CString — см. Impl_Mfc561_Ctor

HostCallOutcome Impl_Mfc561_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    // Захватываем "this" ДИНАМИЧЕСКИ — это и есть theApp (см. комментарий
    // у g_theAppPtr выше). Перезаписываем при каждом вызове (на случай,
    // если в будущем встретим MFC-приложение с несколькими подобными
    // объектами — последний вызов должен быть самым надёжным кандидатом,
    // т.к. в подавляющем большинстве MFC-программ theApp конструируется
    // только один раз, ровно как мы и подтвердили для paint.exe).
    g_theAppPtr = cpu.Get(cpu::Reg32::ECX);

    // ЭКСПЕРИМЕНТ (эта сессия, продолжение): дизассемблирование показало, что
    // `theApp+0x168` guest-код сам умеет безопасно проверить на NULL
    // (`cmp [ebx+0x168],edi; je ...`) — оставляем честным нулём (см. ниже).
    // Но при этом experimente вскрылся ЕЩЁ один непроверенный CString —
    // `theApp+0x158` (похоже на m_lpCmdLine/CCommandLineInfo-подобное поле),
    // который разыменовывается БЕЗ null-проверки (`mov eax,[esi]; cmp
    // [eax-8],edi` — крэш `addr=0xfffffff8`, т.е. чтение [NULL-8]). Ему
    // нужен тот же "пустой, но валидный" CString, что и 0x16c/0x170.
    if (g_emptyCStringData == 0) {
        uint32_t buf = SimpleGuestAlloc(16);
        g_emptyCStringData = buf + 8;
    }
    mem.Write32(g_theAppPtr + 0x168, 0); // честный NULL — guest-код сам проверяет
    for (uint32_t off : {0x158u, 0x16cu, 0x170u}) {
        mem.Write32(g_theAppPtr + off, g_emptyCStringData);
    }

    HostCallOutcome result;
    result.returnValue = cpu.Get(cpu::Reg32::ECX);
    return result;
}

// MFC42u.DLL ординал 1634 — судя по десяткам мест вызова по всей программе
// с паттерном "push значение; mov ecx,адрес-члена; call 1634" — похоже на
// CGdiObject::Attach(handle) или аналогичный обобщённый сеттер "присвоить
// сырой хендл GDI-объекту-обёртке". Как и с 415/561: __thiscall с ОДНИМ
// аргументом на стеке, который нужно честно чистить самой функции — иначе
// та же болезнь со сдвигом стека на 4 байта. Возвращаем TRUE (успех) —
// несколько мест кода делают "test eax,eax; je ..." после вызова, что
// соответствует булевой проверке успеха операции.
HostCallOutcome Impl_Mfc1634_Attach(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// MFC42u.DLL ординал 823 — судя по дизассемблированию (`push 0x5c; call
// [thunk]`, ECX нигде явно не выставляется перед вызовом) — это
// `operator new(size_t size)`: обычная глобальная функция аллокации, а не
// __thiscall-метод. НАША ОБЩАЯ ЗАГЛУШКА до этого возвращала ECX, что здесь
// было бы КАТАСТРОФОЙ (не сразу заметной!) — вызывающий код думал бы, что
// получил свежую память, а на самом деле получал адрес УЖЕ
// СУЩЕСТВУЮЩЕГО объекта (то самое "this" из внешнего контекста) и писал бы
// поверх него, тихо портя данные, а не создавая новый документ/окно.
// Настоящая аллокация через уже готовый SimpleGuestAlloc (тот же, что и у
// _wcsdup).
HostCallOutcome Impl_Mfc823_OperatorNew(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t size = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = SimpleGuestAlloc(size);
    return result;
}

// MFC42u.DLL ординалы 520, 6133, 986, 2028 — цепочка вызовов сразу после
// operator new (823) при завершении InitInstance(), похоже на построение
// шаблона документа/окна для нового пустого документа при старте (частый
// SDI-паттерн). Все четыре — __thiscall с несколькими аргументами на
// стеке, которые сами чистят (тот же паттерн, что 415/561/1634).
// Возвращаем ECX (конструкторная конвенция MSVC). Открытый вопрос: приведёт
// ли это к реальному CreateWindowExW дальше по цепочке — проверяем.
// MFC42u.DLL ординал 520 — ПОДТВЕРЖДЕНО дизассемблированием и целенаправленным
// поиском (спасибо за идею искать "push 2 перед __thiscall"!): это
// CSingleDocTemplate::CSingleDocTemplate(nIDResource, pDocClass,
// pFrameClass, pViewClass) — все 4 значения на стеке ПОДТВЕРЖДЁННО
// константы (nIDResource=2 совпадает с реальной ресурсной строкой
// "Paint|untitled|Bitmap Image|..."). Раньше просто "эхо ECX" — теряло ВСЕ
// 4 значения, из-за чего дальнейший код (ProcessShellCommand/OnFileNew) не
// мог понять, какие классы документа/фрейма/представления использовать.
// Сохраняем по типичной раскладке CDocTemplate (сразу после vtable) — не
// можем быть на 100% уверены в точных смещениях без исходников MFC, но это
// стандартная, разумная раскладка для базового класса с 4 членами.
HostCallOutcome Impl_Mfc520_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t thisPtr = cpu.Get(cpu::Reg32::ECX);
    uint32_t nIDResource  = ReadArg(cpu, mem, 0);
    uint32_t pDocClass    = ReadArg(cpu, mem, 1);
    uint32_t pFrameClass  = ReadArg(cpu, mem, 2);
    uint32_t pViewClass   = ReadArg(cpu, mem, 3);

    mem.Write32(thisPtr + 0x4,  nIDResource);
    mem.Write32(thisPtr + 0x8,  pDocClass);
    mem.Write32(thisPtr + 0xC,  pFrameClass);
    mem.Write32(thisPtr + 0x10, pViewClass);

    HostCallOutcome result;
    result.returnValue = thisPtr;
    return result;
}
HostCallOutcome Impl_Mfc6133_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = cpu.Get(cpu::Reg32::ECX);
    return result;
}
// MFC42u.DLL ординал 986 — судя по контексту вызова (this=theApp,
// единственный аргумент = только что построенный CSingleDocTemplate) —
// похоже на CWinApp::AddDocTemplate(pTemplate). Сохраняем указатель на
// шаблон в theApp+0xC4 — то самое поле, куда ординал 2028 чуть позже что-то
// пишет (список шаблонов?), и откуда ординал 5713 (см. ниже) должен его
// потом прочитать. Раньше эта связь была разорвана — 5713 просто
// возвращал захардкоженное значение, а не читал реально сохранённое.
HostCallOutcome Impl_Mfc986_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t theApp = cpu.Get(cpu::Reg32::ECX);
    uint32_t pTemplate = ReadArg(cpu, mem, 0);
    mem.Write32(theApp + 0xC4, pTemplate);
    HostCallOutcome result;
    result.returnValue = cpu.Get(cpu::Reg32::ECX);
    return result;
}
// MFC42u.DLL ординал 2028 — ТОЧНО ИДЕНТИФИЦИРОВАН (официальная таблица
// экспортов): "?ConnectTemplate@COleTemplateServer@@QAEXABU_GUID@@PAVCDocTemplate@@H@Z"
// = COleTemplateServer::ConnectTemplate(const GUID& clsid, CDocTemplate*
// pDocTemplate, BOOL bAutoRegister), this = &theApp[0xC4] (встроенный
// COleTemplateServer). Первый аргумент — указатель на НАСТОЯЩИЙ GUID,
// зашитый в бинарнике (подтверждено чтением 16 байт по этому адресу —
// это честный, валидный CLSID, не мусор). ЧЕСТНАЯ реализация (эта
// сессия, была заглушкой "return ECX"): форматируем этот GUID как
// "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" и сохраняем как настоящий
// CString (с заголовком CStringData) в theApp+0x168 — это то самое поле,
// которое InitInstance проверяет через CLSIDFromString перед
// ProcessShellCommand (см. HANDOFF/историю чата). Раньше поле было либо
// "пустым, но валидным" плейсхолдером (не крэшит, но правит FALSE-выход),
// либо NULL (крэш в другом месте) — теперь там РЕАЛЬНЫЙ CLSID из
// бинарника, а не выдуманный контент.
HostCallOutcome Impl_Mfc2028_ConnectTemplate(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t oleTemplateServerThis = cpu.Get(cpu::Reg32::ECX);
    uint32_t pGuid = ReadArg(cpu, mem, 0);

    uint8_t guidBytes[16];
    for (int i = 0; i < 16; i++) guidBytes[i] = mem.Read8(pGuid + static_cast<uint32_t>(i));
    uint32_t d1 = guidBytes[0] | (guidBytes[1] << 8) | (guidBytes[2] << 16) | (guidBytes[3] << 24);
    uint16_t d2 = static_cast<uint16_t>(guidBytes[4] | (guidBytes[5] << 8));
    uint16_t d3 = static_cast<uint16_t>(guidBytes[6] | (guidBytes[7] << 8));
    char clsidStr[40];
    std::snprintf(clsidStr, sizeof(clsidStr),
                  "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                  d1, d2, d3, guidBytes[8], guidBytes[9], guidBytes[10], guidBytes[11],
                  guidBytes[12], guidBytes[13], guidBytes[14], guidBytes[15]);
    uint32_t len = static_cast<uint32_t>(std::strlen(clsidStr));

    uint32_t buf = SimpleGuestAlloc(12 + (len + 1) * 2);
    mem.Write32(buf + 0, 1);
    mem.Write32(buf + 4, len);
    mem.Write32(buf + 8, len);
    uint32_t dataPtr = buf + 12;
    for (uint32_t i = 0; i < len; i++) mem.Write16(dataPtr + i * 2, static_cast<uint16_t>(clsidStr[i]));
    mem.Write16(dataPtr + len * 2, 0);

    mem.Write32(oleTemplateServerThis + 0x168 - 0xC4, 0); // ВРЕМЕННО NULL для диагностики точного места крэша
    mem.Write32(oleTemplateServerThis + 0x170 - 0xC4, dataPtr); // ЭТО поле реально передаётся в CLSIDFromString дальше по коду
    mem.Write32(oleTemplateServerThis + 0x16C - 0xC4, dataPtr); // тоже проверяется на непустоту (переиспользуем ту же честную строку — другого источника данных для него у нас нет)

    HostCallOutcome result;
    result.returnValue = oleTemplateServerThis;
    return result;
}

// MFC42u.DLL ординал 5713 — вызывается с this=theApp, БЕЗ аргументов, сразу
// после регистрации шаблона документа. ИСПРАВЛЕНО: эмпирически выяснили —
// проверка `cmp eax,edi; jne пропустить` сравнивает результат НЕ с нулём, а
// с EDI, который на деле хранит указатель на уже зарегистрированный шаблон
// документа (theApp+0xC4 — то самое поле, куда конструктор писал через
// ординал 2028 чуть раньше). Значит, эта функция должна возвращать ЭТОТ ЖЕ
// указатель (что-то вроде "получить первый/текущий шаблон документа") —
// возврат 0 был НЕПРАВИЛЬНОЙ гипотезой, хоть и заставлял код идти по
// другому пути без краха.
// MFC42u.DLL ординал 4604 — ТОЧНО ИДЕНТИФИЦИРОВАН (официальная таблица
// экспортов): "?OnFileNew@CWinApp@@IAEXXZ" = CWinApp::OnFileNew(void).
// Это и есть последний шаг перед реальным созданием окна (см. HANDOFF/
// историю чата). Настоящая реализация в MFC делегирует в
// CDocManager::OnFileNew() -> CDocTemplate::OpenDocumentFile(NULL), что
// внутри создаёт документ+фрейм через CRuntimeClass::CreateObject() и
// вызывает CFrameWnd::LoadFrame()->Create()->CreateWindowExW(). Мы не
// реализуем ВСЮ эту цепочку по отдельным ординалам (это была бы работа
// на несколько сессий) — вместо этого ЧЕСТНО оркеструем главные шаги
// прямо здесь, в хосте:
//   1) вызываем НАСТОЯЩИЕ конструкторы CPaintDoc/CMainFrame — это
//      РЕАЛЬНЫЙ гостевой код paint.exe (m_pfnCreateObject из RUNTIME_CLASS,
//      прочитанного из pTemplate — см. HANDOFF про структуру RUNTIME_CLASS);
//   2) сами регистрируем оконный класс и вызываем CreateWindowExW
//      (через уже существующий win32::g_windowManager) — упрощённая
//      версия LoadFrame/Create, БЕЗ полной эмуляции AfxWndProc/message-map
//      диспетчеризации (это следующий шаг, если понадобится реальная
//      обработка сообщений окна);
//   3) пишем результат в CWnd::m_hWnd — подтверждено оригинальным
//      комментарием в исходниках MFC (afxwin.h): "HWND m_hWnd; // must
//      be first data member" — то есть сразу после vtable, смещение 0x4
//      (ГИПОТЕЗА, требует опытной проверки — CCmdTarget в MFC42 (1998)
//      скорее всего не добавлял данных перед CWnd, в отличие от куда более
//      поздних версий с автоматизацией/connection points инлайн).
// Forward declaration — реализация ниже, но используется в OnFileNew
// (порядок определений в файле исторически сложился по мере находок за
// сессию, не всегда снизу вверх).
HostCallOutcome Impl_Mfc1090_AddView(cpu::CpuState& cpu, cpu::IGuestMemory& mem);
HostCallOutcome Impl_AfxWndProc(cpu::CpuState& cpu, cpu::IGuestMemory& mem);
extern std::unordered_map<uint32_t, uint32_t> g_hwndToObject;
void UpdateCurrentMessage(cpu::IGuestMemory& mem, uint32_t hwnd, uint32_t message,
                           uint32_t wParam, uint32_t lParam);
extern std::unordered_map<uint32_t, uint32_t> g_objectToMsgMapEntries;
extern uint32_t g_captureWindow;
// (extern-объявления g_paintCDC/g_paintView отсюда убраны — настоящие
// определения теперь ВЫШЕ по файлу, до анонимного namespace, и уже
// видны здесь напрямую; оставлять их здесь было ошибкой — создавало
// ОТДЕЛЬНУЮ, анонимно-областную декларацию, конфликтующую с внешней)
bool LoadMenuById(uint16_t menuId);

HostCallOutcome Impl_Mfc4604_OnFileNew(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t theApp = cpu.Get(cpu::Reg32::ECX);
    HostCallOutcome result;

    uint32_t pTemplate = mem.Read32(theApp + 0xC4);
    if (pTemplate == 0) return result;

    uint32_t nIDResource = mem.Read32(pTemplate + 0x4);
    uint32_t pDocClass   = mem.Read32(pTemplate + 0x8);
    uint32_t pFrameClass = mem.Read32(pTemplate + 0xC);
    uint32_t pViewClass  = mem.Read32(pTemplate + 0x10);

    // ПОРЯДОК СООТВЕТСТВУЕТ РЕАЛЬНОМУ MFC (уточнено внешней подсказкой +
    // проверкой реальной цепочки в этой сессии):
    //   CreateNewDocument -> CreateNewFrame -> LoadFrame -> Create(окно
    //   фрейма) -> [WM_CREATE] OnCreateClient -> CreateView ->
    //   CView::Create(окно view, child от фрейма) -> [WM_CREATE view]
    //   CView::OnCreate -> CDocument::AddView(this) -> ... -> OnNewDocument
    // Раньше у нас порядок был "все C++-объекты, потом ВСЕ окна, потом
    // AddView/OnNewDocument" — что НЕ соответствует реальной MFC (там окна
    // фрейма/view создаются РАНЬШЕ AddView/OnNewDocument, как часть
    // CreateNewFrame). Переставили, чтобы соответствовать честной
    // последовательности, а не просто вызывать те же функции в удобном
    // для нас порядке.
    uint32_t pDocument = 0, pFrame = 0, pView = 0;
    if (pDocClass != 0) {
        uint32_t pfnCreateDoc = mem.Read32(pDocClass + 0xC);
        if (pfnCreateDoc != 0) {
            pDocument = win32::CallGuestFunctionStdcall(cpu, mem, pfnCreateDoc, {}, g_tebAddr, g_tebAddr);
        }
    }
    if (pFrameClass != 0) {
        uint32_t pfnCreateFrame = mem.Read32(pFrameClass + 0xC);
        if (pfnCreateFrame != 0) {
            pFrame = win32::CallGuestFunctionStdcall(cpu, mem, pfnCreateFrame, {}, g_tebAddr, g_tebAddr);
        }
    }

    // Заголовок окна — реальная строка ресурса nIDResource (формат MFC
    // DocTemplate: "имя|заголовок-нового|тип-документа|расширение|..." —
    // берём часть до первого '|', как это делает сам MFC). Часть LoadFrame.
    std::string title = "Paint";
    if (g_imageBuffer && g_resourceDirRva != 0) {
        std::vector<uint16_t> str = pe::FindStringResource(
            g_imageBuffer, g_imageBufferSize, g_resourceDirRva, static_cast<uint16_t>(nIDResource));
        if (!str.empty()) {
            std::string full;
            for (uint16_t c : str) {
                if (c == '|') break;
                full += static_cast<char>(c < 128 ? c : '?');
            }
            if (!full.empty()) title = full;
        }
    }
    (void)title; // WindowManager пока не хранит заголовок отдельно — оставляем для будущего

    // Загрузка меню — часть настоящего LoadFrame (ординал 4146), который
    // мы обходим собственной последовательностью создания окон (см.
    // комментарий выше). По конвенции MFC один и тот же nIDResource
    // используется и для строки заголовка (уже читали выше), и для
    // ресурса меню — честно переиспользуем.
    LoadMenuById(static_cast<uint16_t>(nIDResource));

    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        win32::WindowClassInfo info;
        // Настоящий AfxWndProc-эквивалент (эта сессия) — маршрутизирует
        // через message map, а не заглушка. Зарегистрирован как обычная
        // host-функция ("INTERNAL"/"AfxWndProc"), адрес получаем тем же
        // механизмом, что и для любого другого импорта.
        int idx = win32::FindHostFunction("INTERNAL", "AfxWndProc");
        info.wndProc = (idx >= 0) ? win32::IndexToHostAddress(idx) : 0;
        win32::g_windowManager.RegisterClass("MfcFrameOrView", info);
        s_classRegistered = true;
    }

    // Create(окно фрейма) — часть LoadFrame, ДО OnCreateClient/CreateView.
    uint32_t hwnd = win32::g_windowManager.CreateWindow("MfcFrameOrView", 0, 0, 640, 360, theApp);
    if (hwnd != 0 && pFrame != 0) {
        mem.Write32(pFrame + 0x4, hwnd); // CWnd::m_hWnd — подтверждено экспериментально (DragAcceptFiles)
        g_hwndToObject[hwnd] = pFrame;

        // Message map CMainFrame — тем же честным способом, что и для view:
        // реальный вызов GetMessageMap() (0x101e2d5, найден в vtable
        // CMainFrame), не хардкод адреса.
        uint32_t ecxSaveF = cpu.Get(cpu::Reg32::ECX);
        cpu.Get(cpu::Reg32::ECX) = pFrame;
        uint32_t frameMsgMapPtr = win32::CallGuestFunctionStdcall(cpu, mem, 0x101e2d5u, {}, g_tebAddr, g_tebAddr, 100);
        cpu.Get(cpu::Reg32::ECX) = ecxSaveF;
        if (frameMsgMapPtr != 0) {
            g_objectToMsgMapEntries[pFrame] = mem.Read32(frameMsgMapPtr + 4);
        }

        // Реальная отправка WM_CREATE/WM_SIZE фрейму — тем же путём, что и
        // для view (см. ниже), через готовый AfxWndProc-диспетчер.
        uint32_t frameCS = SimpleGuestAlloc(48);
        mem.Write32(frameCS + 0, 0);
        mem.Write32(frameCS + 4, theApp);
        mem.Write32(frameCS + 8, 0);
        mem.Write32(frameCS + 12, 0);
        mem.Write32(frameCS + 16, 360);
        mem.Write32(frameCS + 20, 640);
        mem.Write32(frameCS + 24, 0);
        mem.Write32(frameCS + 28, 0);
        mem.Write32(frameCS + 32, 0x10CF0000u); // style (WS_OVERLAPPEDWINDOW примерно)
        mem.Write32(frameCS + 36, 0);
        mem.Write32(frameCS + 40, 0);
        mem.Write32(frameCS + 44, 0);

        uint32_t ecxSaveFM = cpu.Get(cpu::Reg32::ECX);
        uint32_t espSaveFM = cpu.ESP();

        cpu.ESP() -= 20;
        mem.Write32(cpu.ESP() + 4, hwnd);
        mem.Write32(cpu.ESP() + 8, 0x0001); // WM_CREATE
        mem.Write32(cpu.ESP() + 12, 0);
        mem.Write32(cpu.ESP() + 16, frameCS);
        Impl_AfxWndProc(cpu, mem);

        cpu.ESP() -= 20;
        mem.Write32(cpu.ESP() + 4, hwnd);
        mem.Write32(cpu.ESP() + 8, 0x0005); // WM_SIZE
        mem.Write32(cpu.ESP() + 12, 0);
        mem.Write32(cpu.ESP() + 16, (360u << 16) | 640u);
        Impl_AfxWndProc(cpu, mem);

        cpu.ESP() = espSaveFM;
        cpu.Get(cpu::Reg32::ECX) = ecxSaveFM;
    }
    if (hwnd != 0) {
        mem.Write32(theApp + 0x20, pFrame); // theApp->m_pMainWnd = pFrame
    }

    // OnCreateClient -> CreateView: конструируем pView и создаём его окно
    // КАК CHILD фрейма (это то самое место в реальном MFC, где
    // CFrameWnd::OnCreateClient делегирует в CreateView — CMainFrame эту
    // виртуальную функцию НЕ переопределяет, подтверждено дампом реальной
    // vtable CMainFrame в этой сессии, так что базовое поведение верно).
    if (pViewClass != 0) {
        uint32_t pfnCreateView = mem.Read32(pViewClass + 0xC);
        if (pfnCreateView != 0) {
            pView = win32::CallGuestFunctionStdcall(cpu, mem, pfnCreateView, {}, g_tebAddr, g_tebAddr);
        }
    }
    uint32_t viewHwnd = win32::g_windowManager.CreateWindow("MfcFrameOrView", 0, 0, 640, 360, theApp, hwnd);
    if (viewHwnd != 0 && pView != 0) {
        mem.Write32(pView + 0x4, viewHwnd); // CWnd::m_hWnd для view
        g_hwndToObject[viewHwnd] = pView;

        // Message map view — получаем ЧЕСТНЫМ вызовом реального
        // GetMessageMap() (0x1020b03, найден в vtable CPaintView), а не
        // хардкодом адреса, который мы статически прочитали при анализе.
        // AFX_MSGMAP = {pBaseMap; pEntries} — нам нужен pEntries (+4).
        uint32_t ecxSave2 = cpu.Get(cpu::Reg32::ECX);
        cpu.Get(cpu::Reg32::ECX) = pView;
        uint32_t msgMapPtr = win32::CallGuestFunctionStdcall(cpu, mem, 0x1020b03u, {}, g_tebAddr, g_tebAddr, 100);
        cpu.Get(cpu::Reg32::ECX) = ecxSave2;
        if (msgMapPtr != 0) {
            g_objectToMsgMapEntries[pView] = mem.Read32(msgMapPtr + 4);
        }

        // Реальная отправка WM_CREATE и WM_SIZE через уже готовый
        // AfxWndProc-диспетчер — раньше эти окна просто "существовали",
        // но CPaintView::OnCreate/OnSize ни разу не вызывались. Теперь
        // вызываются по-настоящему, тем же путём, что и любое другое
        // сообщение (через message map, не напрямую по адресу).
        uint32_t createStruct = SimpleGuestAlloc(48); // CREATESTRUCTW, 12 полей по 4 байта
        mem.Write32(createStruct + 0, 0);        // lpCreateParams
        mem.Write32(createStruct + 4, theApp);    // hInstance
        mem.Write32(createStruct + 8, 0);        // hMenu
        mem.Write32(createStruct + 12, hwnd);     // hwndParent
        mem.Write32(createStruct + 16, 360);      // cy
        mem.Write32(createStruct + 20, 640);      // cx
        mem.Write32(createStruct + 24, 0);        // y
        mem.Write32(createStruct + 28, 0);        // x
        mem.Write32(createStruct + 32, 0x50000000u); // style (WS_CHILD|WS_VISIBLE)
        mem.Write32(createStruct + 36, 0);        // lpszName
        mem.Write32(createStruct + 40, 0);        // lpszClass
        mem.Write32(createStruct + 44, 0);        // dwExStyle

        uint32_t ecxSaveMsg = cpu.Get(cpu::Reg32::ECX);
        uint32_t espSaveMsg = cpu.ESP();

        cpu.ESP() -= 20;
        mem.Write32(cpu.ESP() + 4, viewHwnd);
        mem.Write32(cpu.ESP() + 8, 0x0001);       // WM_CREATE
        mem.Write32(cpu.ESP() + 12, 0);
        mem.Write32(cpu.ESP() + 16, createStruct);
        Impl_AfxWndProc(cpu, mem);

        cpu.ESP() -= 20;
        mem.Write32(cpu.ESP() + 4, viewHwnd);
        mem.Write32(cpu.ESP() + 8, 0x0005);       // WM_SIZE
        mem.Write32(cpu.ESP() + 12, 0);           // SIZE_RESTORED
        mem.Write32(cpu.ESP() + 16, (360u << 16) | 640u); // cx=640,cy=360
        Impl_AfxWndProc(cpu, mem);

        cpu.ESP() = espSaveMsg;
        cpu.Get(cpu::Reg32::ECX) = ecxSaveMsg;
    }

    // CView::OnCreate -> CDocument::AddView(this) — вызываем ЧЕСТНУЮ
    // реализацию (ординал 1090), а не подделываем поле напрямую. Момент
    // вызова теперь СРАЗУ после создания окна view, как в реальном MFC
    // (а не в конце функции, как было раньше).
    if (pView != 0 && pDocument != 0) {
        uint32_t ecxSave = cpu.Get(cpu::Reg32::ECX);
        uint32_t espSave = cpu.ESP();
        cpu.Get(cpu::Reg32::ECX) = pDocument;
        cpu.ESP() -= 8;
        mem.Write32(cpu.ESP() + 4, pView);
        Impl_Mfc1090_AddView(cpu, mem);
        cpu.ESP() = espSave;
        cpu.Get(cpu::Reg32::ECX) = ecxSave;
    }

    // CDocument::OnNewDocument() — реальный вызов (this=pDocument), после
    // того как frame/view с их окнами уже существуют (соответствует
    // реальному порядку OpenDocumentFile). Кандидат 0x101e030 найден по
    // паттерну "if (!Base::OnNewDocument()) return FALSE;" внутри vtable
    // CPaintDoc (0x100406c). Ограничиваем maxSteps разумным потолком.
    if (pDocument != 0) {
        uint32_t ecxSave = cpu.Get(cpu::Reg32::ECX);
        cpu.Get(cpu::Reg32::ECX) = pDocument;
        uint32_t r = win32::CallGuestFunctionStdcall(cpu, mem, 0x101e030u, {}, g_tebAddr, g_tebAddr, 200000);
        cpu.Get(cpu::Reg32::ECX) = ecxSave;
        if (cpu::g_consoleAvailable) {
            std::printf("[OnFileNew] OnNewDocument() -> %u\n", r);
        }
    }

    result.returnValue = 1;

    // Прагматичный, честно задокументированный костыль (не имитация
    // реального MFC-механизма — тот целиком внутри MFC42u.dll и
    // недоступен нам для дизассемблирования, подтверждено трижды разными
    // методами в течение расследования). Подставляем в pDocument+0x40
    // минимальную структуру ожидаемой формы (ширина/высота на +0x2c/+0x30
    // — подтверждено разбором вызова SetRect внутри реального кода
    // paint.exe), что позволяет OnDraw реально завершиться, вместо падения
    // на разыменовании NULL. С этой подстановкой и честной поддержкой
    // OLEAUT32/BSTR (см. выше) — OnDraw доходит до конца, InitInstance
    // корректно возвращает 1, весь дальнейший прогон чист.
    if (pView != 0 && pDocument != 0) {
        // Источник для BitBlt (см. ниже) — обычный совместимый DC,
        // залитый белым: именно так выглядит НАСТОЯЩИЙ новый документ в
        // Paint при открытии (пустой белый холст). Это по-прежнему
        // прагматичный костыль (не настоящий битмап документа — тот
        // должен создаваться через CPaintDoc::OnGetEmbeddedItem или
        // аналог, недоступный нам в MFC42u.dll), но теперь честно похож
        // на то, что видит пользователь, а не на тестовый узор.
        uint32_t srcDc = gdi::g_gdi.CreateCompatibleDC();
        uint32_t srcBitmap = gdi::g_gdi.CreateCompatibleBitmapObj(640, 360);
        gdi::g_gdi.SelectObject(srcDc, srcBitmap);
        for (int y = 0; y < 360; y++) {
            for (int x = 0; x < 640; x++) {
                gdi::g_gdi.SetPixel(srcDc, x, y, 0xFFFFFF); // белый (COLORREF)
            }
        }

        uint32_t fakeItem = SimpleGuestAlloc(64);
        mem.Write32(fakeItem + 0x0, 0);        // vtable — не эмулируем (не нашли необходимости)
        mem.Write32(fakeItem + 0xc, srcDc);    // ИСТОЧНИК для BitBlt (0x101a5ac, найдено в этой сессии)
        mem.Write32(fakeItem + 0x2c, 640);     // ширина (подтверждено через SetRect)
        mem.Write32(fakeItem + 0x30, 360);     // высота (подтверждено через SetRect)
        mem.Write32(fakeItem + 0x34, 640);     // right (по форме rect)
        mem.Write32(fakeItem + 0x38, 360);     // bottom (по форме rect)
        mem.Write32(pDocument + 0x40, fakeItem);
        mem.Write32(pDocument + 0x48, 1);      // включает БЫСТРЫЙ путь прямо к BitBlt (найдено в этой сессии)
        // CView::+0x40 (найдено в этой сессии, из мышиного обработчика
        // 0x1012a88) ожидает СТРУКТУРУ ТОЙ ЖЕ ФОРМЫ (+0xc DC, +0x2c/+0x30
        // ширина/высота) — переиспользуем ту же самую, раз назначение
        // похоже (кэш холста на view, а не отдельный объект).
        mem.Write32(pView + 0x40, fakeItem);
        // CView::+0x48 — подтверждённый зум-фактор (idiv/imul в обработке
        // мыши), НИКОГДА не устанавливался — вызывал деление на ноль.
        // Честное значение "масштаб 1:1" — по подсказке из списка
        // "неудовлетворённых зависимостей" (эта сессия).
        mem.Write32(pView + 0x48, 1);

        uint32_t realHdc = gdi::g_gdi.GetOrCreateScreenDC(640, 360);
        uint32_t pCDC = SimpleGuestAlloc(64);
        mem.Write32(pCDC + 0x4, realHdc); // CDC::m_hDC — подтверждено экспериментально
        mem.Write32(pCDC + 0x8, realHdc); // CDC::m_hAttribDC
        // [pCDC+0x1c] — область перерисовки (найдено в этой сессии: OnDraw
        // копирует её через CopyRect в свой локальный rect, который течёт
        // ДАЛЬШЕ через всю цепочку вычислений размеров до самого BitBlt;
        // без этого размер получается 0, и функция выходит рано).
        mem.Write32(pCDC + 0x1c, 0);    // left
        mem.Write32(pCDC + 0x20, 0);    // top
        mem.Write32(pCDC + 0x24, 640);  // right
        mem.Write32(pCDC + 0x28, 360);  // bottom

        // ВАЖНО (найдено в этой сессии): сохраняем ВСЕ callee-saved
        // регистры (EBX/ESI/EDI), не только ECX — иначе вложенный вызов
        // может испортить EBX (=theApp), который используется дальше в
        // хвосте InitInstance, и мы получим крэш, который на самом деле —
        // артефакт неаккуратного вызова, а не реальная проблема
        // гостевого кода (именно так и было при первой попытке).
        uint32_t ecxSaveOD = cpu.Get(cpu::Reg32::ECX);
        uint32_t ebxSaveOD = cpu.Get(cpu::Reg32::EBX);
        uint32_t esiSaveOD = cpu.Get(cpu::Reg32::ESI);
        uint32_t ediSaveOD = cpu.Get(cpu::Reg32::EDI);
        cpu.Get(cpu::Reg32::ECX) = pView;
        win32::CallGuestFunctionStdcall(cpu, mem, 0x1021493u, {pCDC}, g_tebAddr, g_tebAddr, 20000);
        cpu.Get(cpu::Reg32::ECX) = ecxSaveOD;
        cpu.Get(cpu::Reg32::EBX) = ebxSaveOD;
        cpu.Get(cpu::Reg32::ESI) = esiSaveOD;
        cpu.Get(cpu::Reg32::EDI) = ediSaveOD;

        // g_paintCDC — делаем CDC доступным ГЛОБАЛЬНО, чтобы честный
        // "живой" механизм рисования мышью (в Impl_AfxWndProc) мог
        // вызывать OnDraw+BitBlt после каждого сегмента штриха, не
        // пересоздавая CDC каждый раз. ЭТО НЕ ТЕСТОВЫЙ КОД — нужен для
        // реальной работы на железе (Switch), где WM_MOUSEMOVE/
        // WM_LBUTTONDOWN/WM_RBUTTONDOWN будут приходить от настоящего
        // ввода (соска+сенсор → координаты, A → ЛКМ, правый курок → ПКМ).
        // Синтетический тест (LBUTTONDOWN→MOUSEMOVE×N→LBUTTONUP через
        // sendMsg) был здесь для проверки в прошлой сессии — подтверждено
        // пиксельно, что работает; убран перед тестом на реальном
        // железе, чтобы не рисовать непрошеный зигзаг при каждом запуске.
        g_paintCDC = pCDC;
        g_paintView = pView;
    }

    return result;
}

// MFC42u.DLL ординал 5713 — теперь ИСПРАВЛЕНО по-настоящему (было
// захардкожено 0x103ddb8 как временный эксперимент). Раз ординал 986
// (AddDocTemplate) теперь честно СОХРАНЯЕТ указатель на шаблон в
// theApp+0xC4 — эта функция просто ЧЕСТНО ЧИТАЕТ его оттуда, а не
// подделывает значение. Похоже на GetFirstDocTemplate()-подобный геттер.
// MFC42u.DLL ординал 5713 — ТОЧНО ИДЕНТИФИЦИРОВАН (эта сессия, через
// официальную таблицу экспортов mfc42u.dll с demangled-именами):
// "?RunEmbedded@CWinApp@@QAEHXZ" = CWinApp::RunEmbedded(void) — проверка
// "запущены ли мы как встроенный OLE-сервер" (командная строка вида
// "-Embedding"). Прошлая сессия ОШИБОЧНО считала это "геттером шаблона
// документа" (типа GetFirstDocTemplate) и возвращала theApp+0xC4 (что
// технически ненулевое, но НЕ потому что "шаблон уже есть" — там на
// самом деле лежит встроенный объект COleTemplateServer, который
// заполняют ординалы 6133/2028 для OLE-интеграции, а не CDocTemplate*).
// Ненулевой возврат отсюда заставлял guest-код думать, что приложение
// запущено embedded, и пропускать ProcessShellCommand/создание окна —
// см. HANDOFF. При нормальном запуске (без "-Embedding" в командной
// строке) это ВСЕГДА должно быть FALSE — ровно как уже правильно
// реализовано для соседа RunAutomated (5712) ниже.
HostCallOutcome Impl_Mfc5713_ParseCmdLine(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// MFC42u.DLL ординал 5712 — сосед 5713 по номеру и по роли: сразу после
// (this=theApp, без аргументов), с ТЕМ ЖЕ паттерном "test eax,eax; jne
// пропустить" сразу после. Тот же фикс — честный 0 вместо ECX.
HostCallOutcome Impl_Mfc5712_Check(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// MFC42u.DLL ординал 4154 — НАЙДЕН КРИТИЧЕСКИЙ БАГ (эта сессия): вызывается
// в самом начале функции 0x10241cb (LoadStdProfileSettings-подобная логика)
// как "push 4; mov ecx,theApp; call [thunk]" — __thiscall с ОДНИМ аргументом
// на стеке (похоже на nMaxMRU=4 для CRecentFileList), который ДОЛЖНА чистить
// сама функция (подтверждено дизассемблированием: сразу после call идёт
// "xor edi,edi", БЕЗ "add esp,4" со стороны вызывающего). Раньше этот
// ординал не был в таблице и шёл через общую диагностическую заглушку
// (AllocateMfcStubSlot), которая чистит СТРОГО 4 байта (только адрес
// возврата) вне зависимости от реальных аргументов — из-за чего ESP
// оставался на 4 байта БОЛЬШЕ, чем нужно, и это тихо портило все
// последующие pop'ы в эпилоге функции 0x10241cb (включая pop ebx —
// экспериментально подтверждено: EBX после возврата из 0x10241cb внезапно
// становился 0x103defc вместо theApp, ломая ВСЮ последующую логику
// InitInstance, включая AddDocTemplate/GetFirstDocTemplate и проверку
// m_pMainWnd). Возвращаем ECX (this) — стандартная конвенция для
// __thiscall-конструкторов/сеттеров в этом кодовом пути.
HostCallOutcome Impl_Mfc4154_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = cpu.Get(cpu::Reg32::ECX);
    return result;
}

// MFC42u.DLL ординал 4155 — ТОЧНО ИДЕНТИФИЦИРОВАН (официальная таблица
// экспортов): "?LoadStringW@CString@@QAEHI@Z" = CString::LoadStringW(UINT
// nID), this=ecx (CString*), 1 stack-арг (nID), возвращает BOOL. ЧЕСТНАЯ
// реализация (эта сессия, была заглушкой "return ECX"): реально читаем
// строковый ресурс через тот же resource_parser.h, что и Win32 LoadStringW
// (см. Impl_LoadStringW выше), и создаём НАСТОЯЩИЙ CString-буфер с
// заголовком CStringData (nRefs, nDataLength, nAllocLength — по 4 байта,
// итого 12 байт ПЕРЕД данными; [ptr-8] — это nDataLength, ровно то поле,
// которое весь остальной код проверяет на "пусто/не пусто" через паттерн
// "cmp dword ptr [ptr-8],0"). Раньше заглушка оставляла CString "пустым"
// (по workaround'у из ординала 561), из-за чего падали проверки вида
// "OLE CLSID-строка не должна быть пустой" внутри InitInstance.
HostCallOutcome Impl_Mfc4155_LoadStringW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t thisCString = cpu.Get(cpu::Reg32::ECX);
    uint32_t nId = ReadArg(cpu, mem, 0);

    HostCallOutcome result;
    std::vector<uint16_t> str;
    if (g_imageBuffer && g_resourceDirRva != 0) {
        str = pe::FindStringResource(g_imageBuffer, g_imageBufferSize, g_resourceDirRva,
                                       static_cast<uint16_t>(nId));
    }

    uint32_t len = static_cast<uint32_t>(str.size());
    uint32_t buf = SimpleGuestAlloc(12 + (len + 1) * 2);
    mem.Write32(buf + 0, 1);   // nRefs
    mem.Write32(buf + 4, len); // nDataLength
    mem.Write32(buf + 8, len); // nAllocLength
    uint32_t dataPtr = buf + 12;
    for (uint32_t i = 0; i < len; i++) mem.Write16(dataPtr + i * 2, str[i]);
    mem.Write16(dataPtr + len * 2, 0);

    mem.Write32(thisCString, dataPtr); // CString сам по себе — это m_pchData

    result.returnValue = (len > 0) ? 1u : 0u;
    return result;
}

// MFC42u.DLL ординалы 3517 и 858 — тот же класс бага, что и 4154 (эта
// сессия): вызываются внутри 0x10241cb как __thiscall с аргументами на
// стеке без "add esp" со стороны вызывающего — то есть CALLEE должен
// чистить сам. Раньше шли через общую заглушку (только 4 байта очистки).
// argBytes для 3517 ПЕРЕСЧИТАН (эта же сессия): изначально по беглому
// чтению дизассемблирования казалось 8 байт (2 push: edi, eax) — но
// точный подсчёт ESP-дельты по всей функции (скриптом, не на глаз)
// показал 16 байт (4 push: 0, 0x103df08, edi, eax) — первые два push
// ошибочно приписывались ПРЕДЫДУЩЕМУ вызову (ординал 3516), хотя на
// самом деле относятся к этому. Урок: даже "уже проверенное" стоит
// пересчитывать скриптом, а не полагаться на однократный ручной подсчёт.
HostCallOutcome Impl_Mfc3517_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = cpu.Get(cpu::Reg32::ECX);
    return result;
}
// MFC42u.DLL ординал 858 — ТОЧНО ИДЕНТИФИЦИРОВАН (официальная таблица
// экспортов): "??4CString@@QAEABV0@ABV0@@Z" = CString::operator=(const
// CString&), this=ecx (приёмник), 1 stack-арг = указатель на CString-
// источник (по ссылке), возвращает *this (то есть просто this же). ЧЕСТНАЯ
// реализация (эта сессия, была заглушкой "return ECX" — то есть no-op):
// раз CString сам по себе — это ОДИН указатель (m_pchData), присваивание
// это просто копирование этого указателя из источника в приёмник (без
// честного refcounting — у нас и деструктор, и этот оператор упрощены, но
// раз мы никогда не "освобождаем" по refcount, поверхностной копии
// указателя достаточно). Это КРИТИЧНО для паттерна
// "CString temp; temp.LoadStringW(id); m_pszX = temp;" — без настоящего
// operator= загруженная LoadStringW строка так и оставалась бы только во
// временной переменной, а theApp-поле продолжало указывать на старый
// "пустой" workaround-буфер.
HostCallOutcome Impl_Mfc858_CopyAssign(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t dest = cpu.Get(cpu::Reg32::ECX);
    uint32_t srcRef = ReadArg(cpu, mem, 0);
    uint32_t srcData = mem.Read32(srcRef);
    mem.Write32(dest, srcData);

    HostCallOutcome result;
    result.returnValue = dest;
    return result;
}

// HGLOBAL GlobalFree(HGLOBAL hMem) — у нас простой bump-аллокатор без
// реального освобождения (см. SimpleGuestAlloc) — безопасный no-op,
// та же логика, что и у DeleteObject для GDI-хендлов. Возвращает NULL при
// успехе (таков контракт).
HostCallOutcome Impl_GlobalFree(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// HRESULT CLSIDFromString(LPCOLESTR lpsz, LPCLSID pclsid) — у нас нет
// настоящей OLE/COM-инфраструктуры. Судя по контексту вызова (проверка
// "похож ли аргумент командной строки на CLSID для OLE-встраивания") — в
// нашем сценарии (пустая командная строка) это ВСЕГДА должно честно
// провалиться. Возвращаем CO_E_CLASSSTRING (0x800401F3) — стандартный код
// ошибки "строка не является правильным CLSID".
// HRESULT CLSIDFromString(LPCOLESTR lpsz, LPCLSID pclsid) — ЧЕСТНАЯ
// реализация (эта сессия, была захардкожена на CO_E_CLASSSTRING всегда):
// реально парсим формат "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" (UTF-16,
// 38 символов) и пишем 16 байт GUID по pclsid. Возвращаем S_OK (0) при
// успехе, CO_E_CLASSSTRING (0x800401F3) если формат не совпал — как и
// положено настоящей функции.
namespace {
int HexDigit(uint16_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
bool ParseHexN(cpu::IGuestMemory& mem, uint32_t addr, int nChars, uint32_t& outVal) {
    outVal = 0;
    for (int i = 0; i < nChars; i++) {
        int d = HexDigit(mem.Read16(addr + static_cast<uint32_t>(i) * 2));
        if (d < 0) return false;
        outVal = (outVal << 4) | static_cast<uint32_t>(d);
    }
    return true;
}
} // namespace

HostCallOutcome Impl_CLSIDFromString(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpsz = ReadArg(cpu, mem, 0);
    uint32_t pclsid = ReadArg(cpu, mem, 1);
    HostCallOutcome result;

    // Ожидаемый формат ровно 38 символов: { 8 - 4 - 4 - 4 - 12 }
    if (lpsz == 0 || pclsid == 0 || mem.Read16(lpsz) != '{') {
        result.returnValue = 0x800401F3u;
        return result;
    }
    uint32_t pos = lpsz + 2; // после '{'
    uint32_t d1, d2, d3, d4a;
    uint8_t last6[6];
    bool ok = ParseHexN(mem, pos, 8, d1); pos += 8 * 2;
    ok = ok && (mem.Read16(pos) == '-'); pos += 2;
    ok = ok && ParseHexN(mem, pos, 4, d2); pos += 4 * 2;
    ok = ok && (mem.Read16(pos) == '-'); pos += 2;
    ok = ok && ParseHexN(mem, pos, 4, d3); pos += 4 * 2;
    ok = ok && (mem.Read16(pos) == '-'); pos += 2;
    ok = ok && ParseHexN(mem, pos, 4, d4a); pos += 4 * 2;
    ok = ok && (mem.Read16(pos) == '-'); pos += 2;
    for (int i = 0; ok && i < 6; i++) {
        uint32_t byteVal;
        ok = ParseHexN(mem, pos, 2, byteVal);
        last6[i] = static_cast<uint8_t>(byteVal);
        pos += 2 * 2;
    }
    ok = ok && (mem.Read16(pos) == '}');

    if (!ok) {
        result.returnValue = 0x800401F3u;
        return result;
    }

    mem.Write32(pclsid + 0, d1);
    mem.Write16(pclsid + 4, static_cast<uint16_t>(d2));
    mem.Write16(pclsid + 6, static_cast<uint16_t>(d3));
    mem.Write8(pclsid + 8,  static_cast<uint8_t>(d4a >> 8));
    mem.Write8(pclsid + 9,  static_cast<uint8_t>(d4a & 0xFF));
    for (int i = 0; i < 6; i++) mem.Write8(pclsid + 10 + static_cast<uint32_t>(i), last6[i]);

    result.returnValue = 0; // S_OK
    return result;
}

// LPWSTR GetCommandLineW(void) — уже готовая строка (см. InitCrtGlobals /
// CRT_OFFSET_WCMDLINE_STR), просто возвращаем на неё указатель.
HostCallOutcome Impl_GetCommandLineW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_crtGlobalsBase + CRT_OFFSET_WCMDLINE_STR;
    return result;
}

// LPWSTR* CommandLineToArgvW(LPCWSTR lpCmdLine, int* pNumArgs) — у нас
// командная строка всегда простая: одно "слово" (имя программы), без
// пробелов/кавычек — упрощённый, но честный разбор именно этого случая:
// argv[0] = сама строка целиком, argc=1. Выделяем через уже готовый
// SimpleGuestAlloc (тот же, что и для _wcsdup) — и саму копию строки, и
// массив указателей.
HostCallOutcome Impl_CommandLineToArgvW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pCmdLine = ReadArg(cpu, mem, 0);
    uint32_t pNumArgs = ReadArg(cpu, mem, 1);

    uint32_t len = 0;
    if (pCmdLine != 0) while (mem.Read16(pCmdLine + len * 2) != 0) len++;

    uint32_t pArgStr = SimpleGuestAlloc((len + 1) * 2);
    for (uint32_t i = 0; i <= len; i++) mem.Write16(pArgStr + i * 2, mem.Read16(pCmdLine + i * 2));

    uint32_t pArgv = SimpleGuestAlloc(8); // char*[2] = { &строка, NULL }
    mem.Write32(pArgv, pArgStr);
    mem.Write32(pArgv + 4, 0);

    if (pNumArgs != 0) mem.Write32(pNumArgs, 1);

    HostCallOutcome result;
    result.returnValue = pArgv;
    return result;
}

// MFC42u.DLL ординал 3516 — судя по контексту вызова (аргументы похожи на
// "секция/ключ/значение по умолчанию", а РЕЗУЛЬТАТ используется как ГРАНИЦА
// ЦИКЛА в вызывающем коде) — похоже на что-то вроде GetProfileInt/чтение
// счётчика записей MRU-списка (недавние файлы) из реестра. Наша ОБЩАЯ
// заглушка возвращает ECX (указатель!), что в роли "количества элементов"
// даёт ОГРОМНОЕ число — вызывающий код зацикливается на десятки тысяч
// итераций и в итоге падает на плохом адресе. Возвращаем 0 — безопасное
// "пусто/не найдено" (разумно для первого запуска без сохранённой истории).
HostCallOutcome Impl_Mfc3516_ReadProfileCount(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// Обрабатывает сигнал g_requestedToolId (эта сессия) — main.cpp
// выставляет его при клике по кнопке тулбара, здесь мы честно находим
// объект инструмента в реестре [0x103e900] (связный список, id на
// +0x30, next на +0x34 — подтверждено в прошлых сессиях).
//
// ВАЖНО (найдено этой сессией через сквозной тест + дизассемблирование):
// НЕ вызываем настоящий SelectTool (0x1015199) целиком — он в хвосте
// пытается обновить состояние СОБСТВЕННОГО внутреннего CToolBar/
// CStatusBar paint.exe (проверить/снять галочку на кнопке, обновить
// текст статус-бара с именем инструмента), а мы честно НИКОГДА не
// создавали эти внутренние MFC control-объекты — рисуем СВОЙ тулбар и
// СВОЮ рамку выделения поверх фреймбуфера. Попытка использовать
// несуществующий внутренний объект приводит к цепочке крашей (сначала
// CDC::FromHandle с мусорным HDC, затем неисправленный IAT-слот дальше
// по тому же пути) — оба подтверждены реальным сквозным тестом.
//
// Вместо этого реализуем ТОЛЬКО функционально необходимую часть
// SelectTool напрямую (деактивация старого + активация нового через
// vtable+0x3c(bool) — подтверждено дизассемблированием самого
// SelectTool), пропуская хвост с обновлением несуществующего UI. Наша
// собственная рамка выделения в DrawToolbar() уже даёт пользователю
// визуальную обратную связь — то же самое, что дал бы настоящий хвост,
// просто через другой, честно работающий у нас механизм.
void ProcessToolSelectionRequest(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    if (g_requestedToolId == 0) return;
    uint32_t targetId = g_requestedToolId;
    g_requestedToolId = 0;

    uint32_t node = mem.Read32(0x103e900);
    uint32_t newTool = 0;
    int guard = 0;
    while (node != 0 && guard++ < 50) {
        if (mem.Read32(node + 0x30) == targetId) { newTool = node; break; }
        node = mem.Read32(node + 0x34);
    }
    if (newTool == 0) return; // честный ID должен находиться всегда; тихо игнорируем на случай сбоя

    uint32_t oldTool = mem.Read32(0x103d74c);
    if (oldTool == newTool) return; // уже выбран — ничего делать не нужно

    uint32_t ecxSave = cpu.Get(cpu::Reg32::ECX);
    uint32_t ebxSave = cpu.Get(cpu::Reg32::EBX);
    uint32_t esiSave = cpu.Get(cpu::Reg32::ESI);
    uint32_t ediSave = cpu.Get(cpu::Reg32::EDI);

    if (oldTool != 0) {
        uint32_t oldVtable = mem.Read32(oldTool);
        uint32_t oldSlot3c = (oldVtable != 0) ? mem.Read32(oldVtable + 0x3c) : 0;
        if (oldSlot3c != 0) {
            cpu.Get(cpu::Reg32::ECX) = oldTool;
            win32::CallGuestFunctionStdcall(cpu, mem, oldSlot3c, {0u}, g_tebAddr, g_tebAddr, 20000);
        }
    }

    mem.Write32(0x103d74c, newTool);

    uint32_t newVtable = mem.Read32(newTool);
    uint32_t newSlot3c = (newVtable != 0) ? mem.Read32(newVtable + 0x3c) : 0;
    if (newSlot3c != 0) {
        cpu.Get(cpu::Reg32::ECX) = newTool;
        win32::CallGuestFunctionStdcall(cpu, mem, newSlot3c, {1u}, g_tebAddr, g_tebAddr, 20000);
    }

    cpu.Get(cpu::Reg32::ECX) = ecxSave;
    cpu.Get(cpu::Reg32::EBX) = ebxSave;
    cpu.Get(cpu::Reg32::ESI) = esiSave;
    cpu.Get(cpu::Reg32::EDI) = ediSave;
}

// Обрабатывает сигналы g_requestedForegroundColor/BackgroundColor (эта
// сессия) — main.cpp выставляет их при клике по свотчу палитры. Вызывает
// настоящие SetForegroundColor(0x100c30f)/SetBackgroundColor(0x100c255)
// — обе честные простые сеттеры, подтверждено дизассемблированием:
// читают ОДИН параметр со стека (mov eax,[ebp+8]; mov [глобал],eax),
// "this" не используется внутри функции.
void ProcessColorSelectionRequest(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    auto callSetter = [&](uint32_t funcAddr, uint32_t color) {
        uint32_t ecxSave = cpu.Get(cpu::Reg32::ECX);
        uint32_t ebxSave = cpu.Get(cpu::Reg32::EBX);
        uint32_t esiSave = cpu.Get(cpu::Reg32::ESI);
        uint32_t ediSave = cpu.Get(cpu::Reg32::EDI);
        win32::CallGuestFunctionStdcall(cpu, mem, funcAddr, {color}, g_tebAddr, g_tebAddr, 20000);
        cpu.Get(cpu::Reg32::ECX) = ecxSave;
        cpu.Get(cpu::Reg32::EBX) = ebxSave;
        cpu.Get(cpu::Reg32::ESI) = esiSave;
        cpu.Get(cpu::Reg32::EDI) = ediSave;
    };

    if (g_requestedForegroundColor != 0xFFFFFFFFu) {
        uint32_t color = g_requestedForegroundColor;
        g_requestedForegroundColor = 0xFFFFFFFFu;
        callSetter(0x100c30fu, color);
    }
    if (g_requestedBackgroundColor != 0xFFFFFFFFu) {
        uint32_t color = g_requestedBackgroundColor;
        g_requestedBackgroundColor = 0xFFFFFFFFu;
        callSetter(0x100c255u, color);
    }
}

// MFC42u.DLL ординал 5710 — CWinThread::Run() (приложение не переопределяет,
// vtable-слот сразу после InitInstance на +0x5C — прямой thunk на эту
// функцию, подтверждено дизассемблированием). Это НАСТОЯЩАЯ реализация
// цикла сообщений — переиспользуем уже готовую инфраструктуру очереди
// сообщений (win32::g_messageQueue).
//
// ИСПРАВЛЕНО (найдено критичным в сессии реального теста на Switch):
// раньше эта функция честно ИЗВЛЕКАЛА сообщение из очереди, но потом
// его ВЫБРАСЫВАЛА — комментарий гласил "настоящей MFC-диспетчеризации
// (AfxWndProc, карты сообщений) у нас пока нет". Это было написано ДО
// того, как был построен весь механизм Impl_AfxWndProc/DispatchViaMessageMap/
// живого рисования мышью в этой же сессии — и с тех пор Run() просто
// никогда не обновлялся, чтобы использовать эту готовую инфраструктуру!
// Именно поэтому реальные WM_MOUSEMOVE/WM_LBUTTONDOWN честно попадали в
// очередь (Post работал), честно из неё извлекались (отсюда мерцание
// диагностических квадратов при удержании кнопки), но НИКОГДА не
// доходили до реальной отрисовки — они буквально выбрасывались на
// следующей же строке. Теперь каждое извлечённое сообщение реально
// диспетчеризуется через Impl_AfxWndProc — тем же способом (настройка
// стека вручную), что мы использовали для тестовой отправки сообщений
// внутри OnFileNew в предыдущих сессиях.
HostCallOutcome Impl_Mfc5710_Run(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    constexpr int kMaxIterations = 10'000'000; // защита от настоящего бесконечного цикла
    int guard = 0;
    while (guard++ < kMaxIterations) {
        if (win32::g_pumpHostFrame) {
            win32::HostFrameResult frame = win32::g_pumpHostFrame();
            if (frame.shouldQuit) break;
        }
        ProcessToolSelectionRequest(cpu, mem);
        ProcessColorSelectionRequest(cpu, mem);
        if (win32::g_messageQueue.QuitRequested()) break;

        win32::QueuedMessage msg;
        if (win32::g_messageQueue.Pop(msg)) {
            if (msg.message == win32::WM_QUIT) break;

            // Настоящая диспетчеризация: вызываем Impl_AfxWndProc
            // напрямую (та же единица трансляции), вручную настроив
            // стек под её stdcall-контракт (hwnd, message, wParam,
            // lParam) — идентично тому, как мы вручную тестировали
            // отправку сообщений ранее.
            uint32_t espSave = cpu.ESP();
            cpu.ESP() -= 20;
            mem.Write32(cpu.ESP() + 4, msg.hwnd);
            mem.Write32(cpu.ESP() + 8, msg.message);
            mem.Write32(cpu.ESP() + 12, msg.wParam);
            mem.Write32(cpu.ESP() + 16, msg.lParam);
            Impl_AfxWndProc(cpu, mem);
            cpu.ESP() = espSave;
        }
    }
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// _onexit_t _onexit(_onexit_t func) — регистрирует обработчик, вызываемый
// при завершении программы. У нас нет настоящего "списка обработчиков
// выхода" (наш exit() просто останавливает интерпретацию) — честно
// возвращаем ТОТ ЖЕ указатель (контракт: успех = переданный указатель,
// NULL = ошибка), не потому что мы реально что-то регистрируем, а чтобы
// вызывающий код корректно считал регистрацию успешной.
HostCallOutcome Impl_OnExit(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t func = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = func;
    return result;
}

// COLORREF GetSysColor(int nIndex) — системные цвета интерфейса. Значения —
// классическая цветовая схема Windows 9x/XP classic (тот же стиль, что мы
// уже используем во всём собственном UI — меню, диалоги). COLORREF — формат
// 0x00BBGGRR (не RGB!).
// Status GdiplusStartup(ULONG_PTR* token, const GdiplusStartupInput* input,
//                        GdiplusStartupOutput* output)
//
// У нас нет настоящего GDI+ — но вместо того чтобы честно сообщать "не
// найдено" (что уводит MFC по цепочке "необязательная возможность
// недоступна -> бросаем C++ исключение -> ищем обработчика" — а полноценный
// SEH у нас пока не реализован), подделываем УСПЕШНУЮ инициализацию. Это
// не настоящий GDI+ (никакие реальные функции рисования через него работать
// не будут), но позволяет MFC пройти мимо этой развилки без исключений —
// что для нас сейчас гораздо ценнее.
HostCallOutcome Impl_GdiplusStartup(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pToken = ReadArg(cpu, mem, 0);
    if (pToken != 0) mem.Write32(pToken, 0xD1D1D1D1u); // произвольный, но стабильный "токен"
    HostCallOutcome result;
    result.returnValue = 0; // Status::Ok
    return result;
}

// void GdiplusShutdown(ULONG_PTR token) — парная к GdiplusStartup; MFC
// вызывает её из atexit-обработчика при выходе
HostCallOutcome Impl_GdiplusShutdown(cpu::CpuState&, cpu::IGuestMemory&) {
    return HostCallOutcome{};
}

// BOOL SetRectEmpty(LPRECT lprc) — обнуляет все 4 поля RECT
HostCallOutcome Impl_SetRectEmpty(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pRect = ReadArg(cpu, mem, 0);
    if (pRect != 0) {
        mem.Write32(pRect + 0, 0);
        mem.Write32(pRect + 4, 0);
        mem.Write32(pRect + 8, 0);
        mem.Write32(pRect + 12, 0);
    }
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// wchar_t* _wcsdup(const wchar_t* strSource) — дублирует строку в новой
// памяти (см. SimpleGuestAlloc выше — первая функция, которой реально
// понадобилось выделение памяти в госте). NULL на входе -> NULL на выходе,
// как и положено по контракту.
HostCallOutcome Impl_WcsDup(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pSrc = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    if (pSrc == 0) {
        result.returnValue = 0;
        return result;
    }
    uint32_t len = 0;
    while (mem.Read16(pSrc + len * 2) != 0) len++;

    uint32_t pDst = SimpleGuestAlloc((len + 1) * 2);
    for (uint32_t i = 0; i <= len; i++) { // включая завершающий 0
        mem.Write16(pDst + i * 2, mem.Read16(pSrc + i * 2));
    }
    result.returnValue = pDst;
    return result;
}

// BOOL SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam,
//                             UINT fWinIni)
//
// Много разных подкоманд — реализуем то, что реально встретили: запрос
// метрик шрифтов интерфейса (SPI_GETNONCLIENTMETRICS=0x29). ВАЖНО: просто
// занулить структуру — плохая идея (это прошлая версия этой функции) —
// MFC использует эти значения для расчёта масштаба интерфейса, и деление
// на ноль/нулевые размеры окна — гарантированный путь к "невидимому окну
// 0x0" гораздо позже по трассировке. Вместо этого честно заполняем
// разумными, ненулевыми значениями по ТОЧНО ПРОВЕРЕННЫМ смещениям
// NONCLIENTMETRICSW (32-бит, версия для cbSize=500 — без iPaddedBorderWidth,
// появившегося только в Vista+): смещения посчитаны арифметически из
// документированной раскладки структуры (LOGFONTW = 92 байта) и сходятся
// ровно в 500 байт — не гадание, а факт из публичного Win32 API.
HostCallOutcome Impl_SystemParametersInfoW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t uiAction = ReadArg(cpu, mem, 0);
    uint32_t pvParam = ReadArg(cpu, mem, 2);

    HostCallOutcome result;
    result.returnValue = 1; // TRUE — по умолчанию "успех"

    constexpr uint32_t SPI_GETNONCLIENTMETRICS = 0x29;
    if (uiAction == SPI_GETNONCLIENTMETRICS && pvParam != 0) {
        uint32_t cbSize = mem.Read32(pvParam); // caller уже заполнил cbSize
        if (cbSize > 0 && cbSize < 0x10000) { // разумная защита от мусора
            // 1. Сначала честно зануляем всё (безопасная база — особенно
            // важно для lfFaceName-полей, которые мы не заполняем текстом)
            for (uint32_t i = 0; i < cbSize; i += 4) mem.Write32(pvParam + i, 0);
            mem.Write32(pvParam, cbSize);

            // 2. Базовые размеры интерфейса (iBorderWidth..iCaptionHeight) —
            // разумные ненулевые пиксельные значения классической темы
            if (cbSize > 20) {
                mem.Write32(pvParam + 4,  1);  // iBorderWidth
                mem.Write32(pvParam + 8,  16); // iScrollWidth
                mem.Write32(pvParam + 12, 16); // iScrollHeight
                mem.Write32(pvParam + 16, 18); // iCaptionWidth
                mem.Write32(pvParam + 20, 18); // iCaptionHeight
            }

            // 3. Точно посчитанные смещения LOGFONTW-полей (проверено
            // арифметически: 24 + 92 = 116(+8=124) + 92 = 216(+8=224) +
            // 92 = 316 + 92 = 408 + 92 = 500 — ровно сходится с cbSize)
            constexpr uint32_t LOGFONTW_SIZE = 92;
            constexpr uint32_t OFF_LF_CAPTION   = 24;
            constexpr uint32_t OFF_LF_SMCAPTION = 124;
            constexpr uint32_t OFF_LF_MENU      = 224;
            constexpr uint32_t OFF_LF_STATUS    = 316;
            constexpr uint32_t OFF_LF_MESSAGE   = 408;

            if (cbSize >= OFF_LF_MESSAGE + LOGFONTW_SIZE) {
                // lfHeight = -12 (классический размер шрифта интерфейса,
                // отрицательный — "по высоте символа", как обычно у Windows)
                for (uint32_t base : {OFF_LF_CAPTION, OFF_LF_SMCAPTION, OFF_LF_MENU, OFF_LF_STATUS, OFF_LF_MESSAGE}) {
                    mem.Write32(pvParam + base + 0, static_cast<uint32_t>(-12)); // lfHeight
                    mem.Write32(pvParam + base + 16, 400);                        // lfWeight (FW_NORMAL)
                }
                // iSmCaptionWidth/Height (между lfCaptionFont и lfSmCaptionFont)
                mem.Write32(pvParam + 116, 14);
                mem.Write32(pvParam + 120, 18);
                // iMenuWidth/Height (между lfSmCaptionFont и lfMenuFont)
                mem.Write32(pvParam + 216, 18);
                mem.Write32(pvParam + 220, 18);
            }

            if (cpu::g_consoleAvailable) {
                std::printf("[SystemParametersInfoW] SPI_GETNONCLIENTMETRICS заполнены разумными значениями (cbSize=%u)\n", cbSize);
            }
        }
    }
    return result;
}

// HFONT CreateFontIndirectW(const LOGFONTW* lplf) — у нас нет настоящего
// рендеринга через GDI-шрифты (используем свой битмап-шрифт для всего
// собственного UI), поэтому содержимое LOGFONTW не имеет значения — просто
// возвращаем стабильный, уникальный фейковый хендл (тот же принцип, что и
// для фейковых хендлов диалоговых контролов в другом месте проекта).
// DeleteObject на таком хендле — безопасный no-op (см. существующую
// реализацию DeleteObject, она уже терпимо относится к неизвестным хендлам).
uint32_t g_nextFakeFontHandle = 0xF0170000u; // "FONT" в адресе, для узнаваемости в отладке
HostCallOutcome Impl_CreateFontIndirectW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_nextFakeFontHandle++;
    return result;
}

// UINT SetErrorMode(UINT uMode) — управляет отображением системных
// диалогов ошибок. У нас таких диалогов нет и быть не может — просто
// честно храним значение и возвращаем ПРЕДЫДУЩЕЕ (таков контракт функции).
uint32_t g_errorMode = 0;
HostCallOutcome Impl_SetErrorMode(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t newMode = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = g_errorMode;
    g_errorMode = newMode;
    return result;
}

// HBRUSH CreateSolidBrush(COLORREF crColor) — сплошная кисть, честная
// реализация (ColorRefToArgb уже используется CreatePen — та же конвертация)
HostCallOutcome Impl_CreateSolidBrush(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t colorref = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.CreateBrushObj(gdi::ColorRefToArgb(colorref));
    return result;
}

// HBRUSH CreatePatternBrush(HBITMAP hbmp) — у нас нет настоящей заливки
// узором через PatBlt/FillRect (только сплошным цветом), поэтому берём
// РАЗУМНЫЙ компромисс: усредняем пиксели переданного битмапа в один
// сплошной цвет — для классических чёрно-белых 8x8 паттернов (штриховка,
// dither) это даёт средне-серый, визуально близкий к оригиналу издалека.
HostCallOutcome Impl_CreatePatternBrush(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hBitmap = ReadArg(cpu, mem, 0);
    uint32_t colorArgb = 0xFF808080u; // серый по умолчанию, если не найдём битмап
    const gdi::Bitmap* bmp = gdi::g_gdi.GetBitmapObjectMutable(hBitmap);
    if (bmp && !bmp->pixels.empty()) {
        uint64_t sumR = 0, sumG = 0, sumB = 0;
        for (uint32_t px : bmp->pixels) {
            sumR += (px >> 16) & 0xFF;
            sumG += (px >> 8) & 0xFF;
            sumB += px & 0xFF;
        }
        size_t n = bmp->pixels.size();
        colorArgb = 0xFF000000u | ((sumR/n) << 16) | ((sumG/n) << 8) | (sumB/n);
    }
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.CreateBrushObj(colorArgb);
    return result;
}

// HBITMAP CreateBitmap(int nWidth, int nHeight, UINT cPlanes,
//                       UINT cBitsPerPixel, const void* lpvBits)
//
// Переиспользуем ту же инфраструктуру, что и CreateCompatibleBitmap.
// Честно распаковываем самый частый случай — монохромный битмап (1
// плоскость, 1 бит на пиксель, как классический паттерн выделения/штриховки):
// каждая строка выровнена по WORD (кратно 2 байтам), бит=1 -> белый,
// бит=0 -> чёрный. Для других форматов — просто создаём пустой битмап
// нужного размера (у нас и так нет настоящего рендеринга произвольных
// цветовых форматов через GDI помимо DIB, которые уже отдельно
// поддержаны).
HostCallOutcome Impl_CreateBitmap(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    int32_t width = static_cast<int32_t>(ReadArg(cpu, mem, 0));
    int32_t height = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    uint32_t cPlanes = ReadArg(cpu, mem, 2);
    uint32_t cBitsPerPixel = ReadArg(cpu, mem, 3);
    uint32_t lpvBits = ReadArg(cpu, mem, 4);

    uint32_t handle = gdi::g_gdi.CreateCompatibleBitmapObj(width, height);

    if (lpvBits != 0 && cPlanes == 1 && cBitsPerPixel == 1 && width > 0 && height > 0) {
        gdi::Bitmap* bmp = gdi::g_gdi.GetBitmapObjectMutable(handle);
        if (bmp) {
            uint32_t rowBytes = ((static_cast<uint32_t>(width) + 15u) / 16u) * 2u; // выравнивание по WORD
            for (int32_t y = 0; y < height; y++) {
                for (int32_t x = 0; x < width; x++) {
                    uint32_t byteOffset = y * rowBytes + (x / 8);
                    uint8_t byteVal = mem.Read8(lpvBits + byteOffset);
                    bool bit = (byteVal >> (7 - (x % 8))) & 1;
                    bmp->pixels[static_cast<size_t>(y) * width + x] = bit ? 0xFFFFFFFFu : 0xFF000000u;
                }
            }
        }
    }

    HostCallOutcome result;
    result.returnValue = handle;
    return result;
}

// wchar_t* _itow(int value, wchar_t* str, int radix) — конвертирует число в
// строку в буфере, который предоставляет вызывающий код (сам буфер не
// выделяем — просто пишем в уже готовую память). Поддерживаем самые
// частые основания (10 — почти всегда, 16 — иногда для отладочного вывода).
HostCallOutcome Impl_Itow(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    int32_t value = static_cast<int32_t>(ReadArg(cpu, mem, 0));
    uint32_t pStr = ReadArg(cpu, mem, 1);
    int32_t radix = static_cast<int32_t>(ReadArg(cpu, mem, 2));

    HostCallOutcome result;
    result.returnValue = pStr;
    if (pStr == 0) return result;

    char buf[16];
    if (radix == 10) {
        snprintf(buf, sizeof(buf), "%d", value);
    } else if (radix == 16) {
        snprintf(buf, sizeof(buf), "%x", static_cast<uint32_t>(value));
    } else {
        snprintf(buf, sizeof(buf), "%d", value); // разумный дефолт для непредвиденных оснований
    }

    uint32_t i = 0;
    for (; buf[i] != '\0'; i++) mem.Write16(pStr + i * 2, static_cast<uint16_t>(buf[i]));
    mem.Write16(pStr + i * 2, 0);
    return result;
}

// COLORREF GetSysColor(int nIndex) — системные цвета интерфейса. Значения —
// классическая цветовая схема Windows 9x/XP classic (тот же стиль, что мы
// уже используем во всём собственном UI — меню, диалоги). COLORREF — формат
// 0x00BBGGRR (не RGB!).
uint32_t SysColorRef(uint32_t idx) {
    switch (idx) {
        case 4:  return 0x00C0C0C0; // COLOR_MENU
        case 5:  return 0x00FFFFFF; // COLOR_WINDOW
        case 7:  return 0x00000000; // COLOR_MENUTEXT
        case 8:  return 0x00000000; // COLOR_WINDOWTEXT
        case 13: return 0x00800000; // COLOR_HIGHLIGHT (тёмно-синий в BGR)
        case 14: return 0x00FFFFFF; // COLOR_HIGHLIGHTTEXT
        case 15: return 0x00C0C0C0; // COLOR_BTNFACE
        case 16: return 0x00808080; // COLOR_BTNSHADOW
        case 17: return 0x00808080; // COLOR_GRAYTEXT
        case 18: return 0x00000000; // COLOR_BTNTEXT
        case 20: return 0x00FFFFFF; // COLOR_BTNHIGHLIGHT
        default: return 0x00C0C0C0; // разумный дефолт — тот же классический серый
    }
}

HostCallOutcome Impl_GetSysColor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome result;
    result.returnValue = SysColorRef(ReadArg(cpu, mem, 0));
    return result;
}

// ---------- GDI: фигуры и состояние DC (эта сессия; все __stdcall) ----------
// Нужны инструментам Paint: Кисть рисует форму через Polygon, фигуры через
// Rectangle/Ellipse, заливки фона через FillRect. Раньше все они были
// нереализованными импортами (IAT = 0).

namespace {
std::vector<gdi::GdiPoint> ReadPoints(cpu::IGuestMemory& mem, uint32_t lp, int count) {
    std::vector<gdi::GdiPoint> pts;
    if (count <= 0 || count > 65536) return pts;
    pts.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        pts.push_back({ static_cast<int32_t>(mem.Read32(lp + i * 8)),
                        static_cast<int32_t>(mem.Read32(lp + i * 8 + 4)) });
    }
    return pts;
}
int32_t ArgI(cpu::CpuState& cpu, cpu::IGuestMemory& mem, int i) { return static_cast<int32_t>(ReadArg(cpu, mem, i)); }
} // namespace

HostCallOutcome Impl_Polygon(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {        // (hdc, lppt, count)
    auto pts = ReadPoints(mem, ReadArg(cpu, mem, 1), ArgI(cpu, mem, 2));
    if (!pts.empty()) gdi::g_gdi.Polygon(ReadArg(cpu, mem, 0), pts.data(), static_cast<int>(pts.size()));
    HostCallOutcome r; r.returnValue = pts.size() >= 2 ? 1 : 0; return r;
}
HostCallOutcome Impl_Polyline(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {       // (hdc, lppt, count)
    auto pts = ReadPoints(mem, ReadArg(cpu, mem, 1), ArgI(cpu, mem, 2));
    if (!pts.empty()) gdi::g_gdi.Polyline(ReadArg(cpu, mem, 0), pts.data(), static_cast<int>(pts.size()));
    HostCallOutcome r; r.returnValue = pts.size() >= 2 ? 1 : 0; return r;
}
HostCallOutcome Impl_Rectangle(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {      // (hdc, l, t, r, b)
    gdi::g_gdi.Rectangle(ReadArg(cpu, mem, 0), ArgI(cpu, mem, 1), ArgI(cpu, mem, 2), ArgI(cpu, mem, 3), ArgI(cpu, mem, 4));
    HostCallOutcome r; r.returnValue = 1; return r;
}
HostCallOutcome Impl_Ellipse(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {        // (hdc, l, t, r, b)
    gdi::g_gdi.Ellipse(ReadArg(cpu, mem, 0), ArgI(cpu, mem, 1), ArgI(cpu, mem, 2), ArgI(cpu, mem, 3), ArgI(cpu, mem, 4));
    HostCallOutcome r; r.returnValue = 1; return r;
}
HostCallOutcome Impl_FillRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {       // (hdc, lprc, hbr)
    uint32_t hdc = ReadArg(cpu, mem, 0), lprc = ReadArg(cpu, mem, 1), hbr = ReadArg(cpu, mem, 2);
    // Win32: hbr может быть не хендлом, а "COLOR_xxx + 1" (значения 1..31)
    if (hbr != 0 && hbr <= 31) {
        static std::unordered_map<uint32_t, uint32_t> s_sysBrushes;
        auto it = s_sysBrushes.find(hbr);
        if (it == s_sysBrushes.end())
            it = s_sysBrushes.emplace(hbr, gdi::g_gdi.CreateBrushObj(gdi::ColorRefToArgb(SysColorRef(hbr - 1)))).first;
        hbr = it->second;
    }
    gdi::g_gdi.FillRectWithBrush(hdc, static_cast<int32_t>(mem.Read32(lprc)), static_cast<int32_t>(mem.Read32(lprc + 4)),
                                 static_cast<int32_t>(mem.Read32(lprc + 8)), static_cast<int32_t>(mem.Read32(lprc + 12)), hbr);
    HostCallOutcome r; r.returnValue = 1; return r;
}
HostCallOutcome Impl_SetTextColor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome r; r.returnValue = gdi::g_gdi.SetTextColor(ReadArg(cpu, mem, 0), ReadArg(cpu, mem, 1)); return r;
}
HostCallOutcome Impl_SetBkColor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome r; r.returnValue = gdi::g_gdi.SetBkColor(ReadArg(cpu, mem, 0), ReadArg(cpu, mem, 1)); return r;
}
HostCallOutcome Impl_SetBkMode(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome r; r.returnValue = static_cast<uint32_t>(gdi::g_gdi.SetBkMode(ReadArg(cpu, mem, 0), ArgI(cpu, mem, 1))); return r;
}
HostCallOutcome Impl_GetTextColor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome r; r.returnValue = gdi::g_gdi.GetTextColor(ReadArg(cpu, mem, 0)); return r;
}
HostCallOutcome Impl_GetBkMode(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome r; r.returnValue = static_cast<uint32_t>(gdi::g_gdi.GetBkMode(ReadArg(cpu, mem, 0))); return r;
}
HostCallOutcome Impl_GetPixel(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {       // (hdc, x, y)
    HostCallOutcome r; r.returnValue = gdi::g_gdi.GetPixel(ReadArg(cpu, mem, 0), ArgI(cpu, mem, 1), ArgI(cpu, mem, 2)); return r;
}

// HPALETTE SelectPalette(HDC, HPALETTE, BOOL) / UINT RealizePalette(HDC) —
// НАЙДЕНО КАК НАСТОЯЩИЙ КРАШ (эта сессия): вообще не были реализованы
// (IAT-слот оставался непропатченным), падало при вызове через
// "mov ebx,[iat]; ...; call ebx" — попытка перейти по мусорному адресу.
// Путь Карандаша их никогда не вызывает (простая заливка сплошным
// цветом), а путь Кисти — вызывает (реализация палитры перед
// рисованием паттерном, стандартная практика для палитровых
// устройств). Честный no-op: наш рендеринг работает напрямую с ARGB,
// без индексированной палитры устройства — реальная семантика этих
// функций нам не нужна для корректного визуального результата.
HostCallOutcome Impl_SelectPalette(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome result;
    result.returnValue = 0; // NULL — "нет предыдущей палитры", безопасно
    return result;
}

HostCallOutcome Impl_RealizePalette(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0; // 0 изменённых записей — безопасно, ничего не сломает
    return result;
}

// LONG InterlockedExchange(LONG volatile *Target, LONG Value) — атомарно
// (у нас единственный поток исполнения — реальная атомарность не нужна,
// честно реализуем сам контракт): пишем новое значение, возвращаем старое.
HostCallOutcome Impl_InterlockedExchange(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pTarget = ReadArg(cpu, mem, 0);
    uint32_t newValue = ReadArg(cpu, mem, 1);
    uint32_t oldValue = mem.Read32(pTarget);
    mem.Write32(pTarget, newValue);
    HostCallOutcome result;
    result.returnValue = oldValue;
    return result;
}

// DWORD GetLastError(void) — простое глобальное состояние. Большинство наших
// реализаций и так "успешны" (не эмулируем намеренные отказы), поэтому 0
// (ERROR_SUCCESS) по умолчанию — честный, безопасный дефолт. Если в
// будущем какому-то коду понадобится конкретный код ошибки от конкретного
// вызова — добавим SetLastError в нужные места по мере необходимости.
uint32_t g_lastError = 0;

HostCallOutcome Impl_GetLastError(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_lastError;
    return result;
}

// Win32 RaiseException is noreturn unless a real SEH handler processes it.
// Silently returning was incorrect: the MSVC delay-load helper then attempts
// to jump through unresolved pointers (often jmp eax with EAX==0).
// Until guest SEH dispatch is implemented, fail explicitly with exception
// metadata instead of disguising a missing import as a CPU instruction bug.
HostCallOutcome Impl_RaiseException_Experimental(cpu::CpuState& c, cpu::IGuestMemory& mem) {
    cpu::g_sehRaised = true;
    cpu::g_sehExceptionCode = ReadArg(c, mem, 0);
    cpu::g_sehExceptionFlags = ReadArg(c, mem, 1);
    uint32_t count = ReadArg(c, mem, 2);
    uint32_t args = ReadArg(c, mem, 3);
    cpu::g_sehExceptionArgumentCount = count;
    for (unsigned i = 0; i < 4; ++i)
        cpu::g_sehExceptionArguments[i] =
            i < count && args && mem.IsRangeMapped(args + i * 4, 4) ?
            mem.Read32(args + i * 4) : 0;
    // RaiseException is a stdcall function with 16 bytes of arguments.
    // The saved guest CONTEXT describes the point to resume if a registered
    // handler returns ExceptionContinueExecution.
    uint32_t callerEip = mem.IsRangeMapped(c.ESP(),4) ? mem.Read32(c.ESP()) : c.eip;
    cpu::CpuState resume = c;
    resume.eip = callerEip;
    resume.ESP() = c.ESP() + 20;
    const uint32_t paramCount = count < 4 ? count : 4;
    win32::seh::DispatchResult result = win32::seh::Dispatch(
        resume, mem, g_tebAddr, cpu::g_sehExceptionCode,
        cpu::g_sehExceptionFlags, callerEip,
        cpu::g_sehExceptionArguments, paramCount);
    if (result == win32::seh::DispatchResult::Handled) {
        c = resume;
        HostCallOutcome r;
        r.customReturn = true;
        return r;
    }
    cpu::g_lastKnownEip = callerEip;
    cpu::FatalError(result == win32::seh::DispatchResult::Unhandled
        ? "Unhandled Win32 SEH exception"
        : "Unsupported or invalid Win32 SEH exception handler chain");
}

// BOOL QueryPerformanceCounter(LARGE_INTEGER* lpCount) — высокоточный
// счётчик тиков. Как и с GetSystemTimeAsFileTime — реальная точность нам не
// важна (чаще всего это опять же источник "энтропии" для security cookie,
// как и в нашем случае), берём что-то меняющееся на основе GetTickCount.
HostCallOutcome Impl_QueryPerformanceCounter(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pCount = ReadArg(cpu, mem, 0);
    uint32_t tick = g_getTickCountImpl ? g_getTickCountImpl() : 0;
    uint64_t fakeCounter = static_cast<uint64_t>(tick) * 1000ull; // "мс -> примерно тики"
    if (pCount != 0) {
        mem.Write32(pCount, static_cast<uint32_t>(fakeCounter & 0xFFFFFFFFu));
        mem.Write32(pCount + 4, static_cast<uint32_t>(fakeCounter >> 32));
    }
    HostCallOutcome result;
    result.returnValue = 1; // TRUE — оборудование "поддерживает" высокоточный счётчик
    return result;
}

// void OutputDebugStringA(LPCSTR) — осознанный no-op: геймплей не зависит
// от того, что происходит с debug-выводом
HostCallOutcome Impl_OutputDebugStringA(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// int GetSystemMetrics(int nIndex) — запрашивает системные параметры экрана/
// элементов интерфейса. Отдаём правдоподобные значения классического
// маленького Windows-рабочего стола (а не наобум придуманные числа) —
// разрешение возьмём наш уменьшенный виртуальный экран (640x360), который потом масштабируется x2 в main.cpp при выводе на реальный экран.
HostCallOutcome Impl_GetSystemMetrics(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    int32_t index = static_cast<int32_t>(ReadArg(cpu, mem, 0));
    HostCallOutcome result;
    switch (index) {
        case 0:  result.returnValue = 640; break; // SM_CXSCREEN
        case 1:  result.returnValue = 360;  break; // SM_CYSCREEN
        case 2:  result.returnValue = 16;   break; // SM_CXVSCROLL
        case 3:  result.returnValue = 16;   break; // SM_CYHSCROLL
        case 4:  result.returnValue = 19;   break; // SM_CYCAPTION
        case 5:  result.returnValue = 1;    break; // SM_CXBORDER
        case 6:  result.returnValue = 1;    break; // SM_CYBORDER
        case 7:  result.returnValue = 3;    break; // SM_CXFIXEDFRAME (он же CXDLGFRAME)
        case 8:  result.returnValue = 3;    break; // SM_CYFIXEDFRAME
        case 11: result.returnValue = 32;   break; // SM_CXICON
        case 12: result.returnValue = 32;   break; // SM_CYICON
        case 13: result.returnValue = 32;   break; // SM_CXCURSOR
        case 14: result.returnValue = 32;   break; // SM_CYCURSOR
        case 15: result.returnValue = 19;   break; // SM_CYMENU
        case 16: result.returnValue = 640; break; // SM_CXFULLSCREEN
        case 17: result.returnValue = 360;  break; // SM_CYFULLSCREEN
        default: result.returnValue = 0;    break; // неизвестная метрика — честный 0
    }
    return result;
}

// HMODULE GetModuleHandleA(LPCSTR lpModuleName) — в реальной Windows HMODULE
// текущего процесса буквально РАВЕН его базовому адресу загрузки. Мы это
// знаем совершенно точно (g_imageBase) — значит можем вернуть по-настоящему
// правильное значение, а не выдумку. Игнорируем сам lpModuleName: почти
// всегда это NULL (запрос "хендл самого себя"), а поддержку поиска ДРУГИХ
// загруженных модулей у нас пока в принципе нет.
HostCallOutcome Impl_GetModuleHandleA(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_imageBase;
    return result;
}

// BOOL GetStartupInfoA(LPSTARTUPINFOA lpStartupInfo) — заполняет структуру
// параметров запуска. Настоящая Windows-структура пришла бы от процесса
// создателя (explorer.exe и т.п.) — у нас такого контекста нет, поэтому
// честно обнуляем всю структуру (это ЗНАЧИТ "никаких специальных флагов
// запуска не задано" — корректное, предсказуемое поведение по умолчанию,
// а не как попало). Размер структуры берём из её же поля cb, которое
// вызывающий код обязан выставить перед вызовом (стандартная практика Win32).
HostCallOutcome Impl_GetStartupInfoA(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pInfo = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    if (pInfo != 0) {
        uint32_t cb = mem.Read32(pInfo); // структура сама указывает свой размер
        if (cb == 0 || cb > 256) cb = 68; // разумный дефолт (реальный sizeof(STARTUPINFOA))
        for (uint32_t i = 0; i < cb; i++) {
            mem.Write8(pInfo + i, 0);
        }
    }
    return result;
}

// FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName) — runtime-аналог
// того, что iat_patcher.cpp делает при загрузке PE. Ищем имя функции в нашей
// же таблице host-функций (по всем известным DLL, т.к. мы не различаем
// отдельные "загруженные модули" по-настоящему) — честная переимплементация
// того же контракта, просто резолвим на этапе исполнения, а не заранее.
HostCallOutcome Impl_GetProcAddress(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t procNameArg = ReadArg(cpu, mem, 1);
    HostCallOutcome result;

    if (procNameArg < 0x10000) {
        // HIWORD==0 означает "это ординал, а не указатель на строку" —
        // резолвинг по ординалу мы пока не поддерживаем
        result.returnValue = 0;
        return result;
    }

    std::string name = ReadCString(mem, procNameArg);
    for (size_t i = 0; i < g_hostFunctionTableSize; i++) {
        if (name == g_hostFunctionTable[i].funcName) {
            result.returnValue = IndexToHostAddress(static_cast<int>(i));
            return result;
        }
    }
    result.returnValue = 0; // не нашли — честно говорим "нет такой функции"
    return result;
}

// int LoadStringW(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax) —
// НАСТОЯЩАЯ реализация: реально парсим дерево ресурсов .rsrc (см.
// resource_parser.h) и копируем найденную строку в гостевую память.
// hInstance игнорируем — у нас пока концептуально один "модуль" (сама игра).
HostCallOutcome Impl_LoadStringW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t uId = ReadArg(cpu, mem, 1);
    uint32_t lpBuffer = ReadArg(cpu, mem, 2);
    int32_t cchBufferMax = static_cast<int32_t>(ReadArg(cpu, mem, 3));

    HostCallOutcome result;

    if (!g_imageBuffer || g_resourceDirRva == 0 || lpBuffer == 0 || cchBufferMax <= 0) {
        result.returnValue = 0;
        return result;
    }

    std::vector<uint16_t> str = pe::FindStringResource(
        g_imageBuffer, g_imageBufferSize, g_resourceDirRva, static_cast<uint16_t>(uId));

    if (str.empty()) {
        result.returnValue = 0;
        return result;
    }

    // Копируем не больше cchBufferMax-1 символов + null-терминатор — как и
    // положено настоящей LoadStringW при переполнении буфера
    int32_t toCopy = static_cast<int32_t>(str.size());
    if (toCopy > cchBufferMax - 1) toCopy = cchBufferMax - 1;
    if (toCopy < 0) toCopy = 0;

    for (int32_t i = 0; i < toCopy; i++) {
        mem.Write16(lpBuffer + static_cast<uint32_t>(i) * 2, str[static_cast<size_t>(i)]);
    }
    mem.Write16(lpBuffer + static_cast<uint32_t>(toCopy) * 2, 0); // null-терминатор

    result.returnValue = static_cast<uint32_t>(toCopy);
    return result;
}

// --- ADVAPI32.dll (Win32, stdcall) — виртуальный реестр ---

// LONG RegCreateKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved, LPWSTR lpClass,
//                       DWORD dwOptions, REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSA,
//                       PHKEY phkResult, LPDWORD lpdwDisposition)
HostCallOutcome Impl_RegCreateKeyExW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hKeyParent    = ReadArg(cpu, mem, 0);
    uint32_t subKeyPtr     = ReadArg(cpu, mem, 1);
    uint32_t phkResult     = ReadArg(cpu, mem, 7);
    uint32_t lpDisposition = ReadArg(cpu, mem, 8);

    std::string subKey = ReadWideCStringNarrow(mem, subKeyPtr);

    bool existed = false;
    uint32_t newHandle = g_registry.CreateOrOpenKey(hKeyParent, subKey, existed);

    if (phkResult != 0) mem.Write32(phkResult, newHandle);
    if (lpDisposition != 0) {
        mem.Write32(lpDisposition, existed ? 2u /*REG_OPENED_EXISTING_KEY*/ : 1u /*REG_CREATED_NEW_KEY*/);
    }

    HostCallOutcome result;
    result.returnValue = ERROR_SUCCESS_;
    return result;
}

// LONG RegOpenKeyW(HKEY hKey, LPCWSTR lpSubKey, PHKEY phkResult) — более
// простой (не-Ex) вариант, только 3 аргумента, без ulOptions/samDesired.
// Семантика открытия — та же (не создаёт ключ, если его нет), просто
// широкая строка вместо ANSI.
HostCallOutcome Impl_RegOpenKeyW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hKeyParent = ReadArg(cpu, mem, 0);
    uint32_t subKeyPtr  = ReadArg(cpu, mem, 1);
    uint32_t phkResult  = ReadArg(cpu, mem, 2);

    std::string subKey = ReadWideCStringNarrow(mem, subKeyPtr);

    bool found = false;
    uint32_t handle = g_registry.OpenExistingKey(hKeyParent, subKey, found);

    HostCallOutcome result;
    if (!found) {
        result.returnValue = ERROR_FILE_NOT_FOUND_;
        return result;
    }
    if (phkResult != 0) mem.Write32(phkResult, handle);
    result.returnValue = ERROR_SUCCESS_;
    return result;
}
HostCallOutcome Impl_RegOpenKeyExA(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hKeyParent = ReadArg(cpu, mem, 0);
    uint32_t subKeyPtr  = ReadArg(cpu, mem, 1);
    uint32_t phkResult  = ReadArg(cpu, mem, 4);

    std::string subKey = ReadCString(mem, subKeyPtr); // именно ANSI-версия

    bool found = false;
    uint32_t handle = g_registry.OpenExistingKey(hKeyParent, subKey, found);

    HostCallOutcome result;
    if (!found) {
        result.returnValue = ERROR_FILE_NOT_FOUND_;
        return result;
    }
    if (phkResult != 0) mem.Write32(phkResult, handle);
    result.returnValue = ERROR_SUCCESS_;
    return result;
}

// LONG RegOpenKeyExW(hKey, LPCWSTR subKey, options, sam, PHKEY)
HostCallOutcome Impl_RegOpenKeyExW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t phkResult = ReadArg(cpu, mem, 4);
    bool found = false;
    uint32_t handle = g_registry.OpenExistingKey(ReadArg(cpu, mem, 0), ReadWideCStringNarrow(mem, ReadArg(cpu, mem, 1)), found);
    HostCallOutcome result;
    result.returnValue = found ? ERROR_SUCCESS_ : ERROR_FILE_NOT_FOUND_;
    if (found && phkResult != 0) mem.Write32(phkResult, handle);
    return result;
}
// Перечисление подключей и удаление: виртуальный реестр подключи не
// перечисляет, удалённых значений программы потом не ищут
HostCallOutcome Impl_RegEnumKeyW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 259; // ERROR_NO_MORE_ITEMS
    return result;
}
HostCallOutcome Impl_RegDeleteW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = ERROR_SUCCESS_;
    return result;
}

// Общая логика для RegQueryValueExW/A — отличаются только тем, как читается имя значения
HostCallOutcome QueryValueCommon(cpu::CpuState& cpu, cpu::IGuestMemory& mem, const std::string& valueName) {
    uint32_t hKey     = ReadArg(cpu, mem, 0);
    uint32_t lpType   = ReadArg(cpu, mem, 3);
    uint32_t lpData   = ReadArg(cpu, mem, 4);
    uint32_t lpcbData = ReadArg(cpu, mem, 5);

    win32::RegValue value;
    HostCallOutcome result;
    if (!g_registry.GetValue(hKey, valueName, value)) {
        result.returnValue = ERROR_FILE_NOT_FOUND_;
        return result;
    }

    if (lpType != 0) mem.Write32(lpType, value.type);

    uint32_t capacity = (lpcbData != 0) ? mem.Read32(lpcbData) : 0;

    if (lpData == 0) {
        // Вызов "только узнать размер" — валидный, распространённый паттерн
        if (lpcbData != 0) mem.Write32(lpcbData, static_cast<uint32_t>(value.data.size()));
        result.returnValue = ERROR_SUCCESS_;
        return result;
    }

    if (value.data.size() > capacity) {
        if (lpcbData != 0) mem.Write32(lpcbData, static_cast<uint32_t>(value.data.size()));
        result.returnValue = ERROR_MORE_DATA_;
        return result;
    }

    for (size_t i = 0; i < value.data.size(); i++) {
        mem.Write8(lpData + static_cast<uint32_t>(i), value.data[i]);
    }
    if (lpcbData != 0) mem.Write32(lpcbData, static_cast<uint32_t>(value.data.size()));

    result.returnValue = ERROR_SUCCESS_;
    return result;
}

HostCallOutcome Impl_RegQueryValueExW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    std::string valueName = ReadWideCStringNarrow(mem, ReadArg(cpu, mem, 1));
    return QueryValueCommon(cpu, mem, valueName);
}

HostCallOutcome Impl_RegQueryValueExA(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    std::string valueName = ReadCString(mem, ReadArg(cpu, mem, 1));
    return QueryValueCommon(cpu, mem, valueName);
}

// LONG RegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved,
//                       DWORD dwType, const BYTE* lpData, DWORD cbData)
HostCallOutcome Impl_RegSetValueExW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hKey        = ReadArg(cpu, mem, 0);
    uint32_t valueNamePtr = ReadArg(cpu, mem, 1);
    uint32_t dwType      = ReadArg(cpu, mem, 3);
    uint32_t lpData      = ReadArg(cpu, mem, 4);
    uint32_t cbData      = ReadArg(cpu, mem, 5);

    std::string valueName = ReadWideCStringNarrow(mem, valueNamePtr);

    std::vector<uint8_t> data(cbData);
    for (uint32_t i = 0; i < cbData; i++) data[i] = mem.Read8(lpData + i);

    g_registry.SetValue(hKey, valueName, dwType, data);
    g_registry.SaveToFile(win32::kRegistryFilePath); // реестр маленький — сохраняем сразу, не теряем данные

    HostCallOutcome result;
    result.returnValue = ERROR_SUCCESS_;
    return result;
}

// LONG RegCloseKey(HKEY hKey) — хендлы не освобождаем по-настоящему
// (упрощение, безопасное: простые игры открывают считанное число ключей)
HostCallOutcome Impl_RegCloseKey(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = ERROR_SUCCESS_;
    return result;
}

// HWND GetDesktopWindow(void) — хендл "окна рабочего стола", часто
// используется как родительское окно для MessageBox и т.п. Возвращаем
// постоянный, узнаваемый фейковый хендл — когда дойдём до настоящей
// оконной системы (Фаза 4), формализуем полноценную схему нумерации HWND,
// а этот код станет её первым, зарезервированным значением.
HostCallOutcome Impl_GetDesktopWindow(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0xFFFF0001u;
    return result;
}

// UINT GetPrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName,
//                             INT nDefault, LPCWSTR lpFileName) —
// у нас нет настоящего INI-файла, но это НЕ имитация: документированное
// поведение при отсутствии ключа — вернуть именно nDefault, поэтому
// "ключ всегда отсутствует" честно укладывается в контракт функции.
HostCallOutcome Impl_GetPrivateProfileIntW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome result;
    result.returnValue = ReadArg(cpu, mem, 2); // nDefault
    return result;
}

// DWORD GetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName,
//                                  LPCWSTR lpDefault, LPWSTR lpReturnedString,
//                                  DWORD nSize, LPCWSTR lpFileName) —
// аналогично: копируем lpDefault в буфер (документированное поведение
// при отсутствии ключа/файла), возвращаем длину скопированной строки.
HostCallOutcome Impl_GetPrivateProfileStringW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpDefault = ReadArg(cpu, mem, 2);
    uint32_t lpReturnedString = ReadArg(cpu, mem, 3);
    uint32_t nSize = ReadArg(cpu, mem, 4);

    HostCallOutcome result;
    if (lpReturnedString == 0 || nSize == 0) {
        result.returnValue = 0;
        return result;
    }

    uint32_t i = 0;
    for (; i < nSize - 1; i++) {
        uint16_t c = (lpDefault != 0) ? mem.Read16(lpDefault + i * 2) : 0;
        if (c == 0) break;
        mem.Write16(lpReturnedString + i * 2, c);
    }
    mem.Write16(lpReturnedString + i * 2, 0); // null-терминатор

    result.returnValue = i;
    return result;
}

// --- GDI32.dll / USER32.dll (Win32, stdcall) — рисование ---

// HDC GetDC(HWND hWnd) — у нас пока один "экранный" DC на всё время жизни,
// размером с то, что мы же сами вернули из GetSystemMetrics (640x360)
HostCallOutcome Impl_GetDC(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.GetOrCreateScreenDC(640, 360);
    return result;
}

HostCallOutcome Impl_CreateCompatibleDC(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.CreateCompatibleDC();
    return result;
}

HostCallOutcome Impl_CreateCompatibleBitmap(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    int32_t w = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t h = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.CreateCompatibleBitmapObj(w, h);
    return result;
}

HostCallOutcome Impl_SelectObject(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hdc = ReadArg(cpu, mem, 0);
    uint32_t hObj = ReadArg(cpu, mem, 1);
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.SelectObject(hdc, hObj);
    return result;
}

HostCallOutcome Impl_DeleteObject(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    gdi::g_gdi.DeleteObject(ReadArg(cpu, mem, 0));
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

HostCallOutcome Impl_DeleteDC(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    gdi::g_gdi.DeleteDC(ReadArg(cpu, mem, 0));
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

HostCallOutcome Impl_SetPixel(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hdc = ReadArg(cpu, mem, 0);
    int32_t x = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t y = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    uint32_t colorref = ReadArg(cpu, mem, 3);
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.SetPixel(hdc, x, y, colorref);
    return result;
}

// BOOL MoveToEx(HDC hdc, int x, int y, LPPOINT lpPoint) — lpPoint (может
// быть NULL) получает СТАРУЮ позицию, реальный контракт функции
HostCallOutcome Impl_MoveToEx(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hdc = ReadArg(cpu, mem, 0);
    int32_t x = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t y = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    uint32_t lpPoint = ReadArg(cpu, mem, 3);

    int oldX = 0, oldY = 0;
    bool hadPos = gdi::g_gdi.GetCurrentPos(hdc, oldX, oldY);
    gdi::g_gdi.MoveTo(hdc, x, y);

    if (lpPoint != 0 && hadPos) {
        mem.Write32(lpPoint + 0, static_cast<uint32_t>(oldX));
        mem.Write32(lpPoint + 4, static_cast<uint32_t>(oldY));
    }

    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

HostCallOutcome Impl_LineTo(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hdc = ReadArg(cpu, mem, 0);
    int32_t x = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t y = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    gdi::g_gdi.LineTo(hdc, x, y);
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

HostCallOutcome Impl_BitBlt(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hdcDst = ReadArg(cpu, mem, 0);
    int32_t xDst = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t yDst = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    int32_t w    = static_cast<int32_t>(ReadArg(cpu, mem, 3));
    int32_t h    = static_cast<int32_t>(ReadArg(cpu, mem, 4));
    uint32_t hdcSrc = ReadArg(cpu, mem, 5);
    int32_t xSrc = static_cast<int32_t>(ReadArg(cpu, mem, 6));
    int32_t ySrc = static_cast<int32_t>(ReadArg(cpu, mem, 7));
    // arg 8 (rop) игнорируем — считаем SRCCOPY, подавляющее большинство реальных случаев

    gdi::g_gdi.BitBlt(hdcDst, xDst, yDst, w, h, hdcSrc, xSrc, ySrc);
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

HostCallOutcome Impl_CreatePen(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    int32_t style = static_cast<int32_t>(ReadArg(cpu, mem, 0));
    int32_t width = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    uint32_t colorref = ReadArg(cpu, mem, 2);
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.CreatePenObj(style, width, colorref);
    return result;
}

HostCallOutcome Impl_GetStockObject(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    int32_t id = static_cast<int32_t>(ReadArg(cpu, mem, 0));
    HostCallOutcome result;
    result.returnValue = gdi::g_gdi.GetStockObject(id);
    return result;
}

// int GetDeviceCaps(HDC hdc, int nIndex) — правдоподобные значения под наш
// виртуальный экран (те же 640x360, что и в GetSystemMetrics)
HostCallOutcome Impl_GetDeviceCaps(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    int32_t index = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    HostCallOutcome result;
    switch (index) {
        case 8:  result.returnValue = 640; break; // HORZRES
        case 10: result.returnValue = 360;  break; // VERTRES
        case 12: result.returnValue = 32;   break; // BITSPIXEL
        case 14: result.returnValue = 1;    break; // PLANES
        default: result.returnValue = 0;    break;
    }
    return result;
}

// int SetROP2(HDC hdc, int mode) — мы всегда рисуем в режиме простой
// перезаписи (R2_COPYPEN), альтернативные ROP2-режимы пока не реализованы
HostCallOutcome Impl_SetROP2(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    // Честная реализация (раньше: заглушка "всегда COPYPEN", из-за которой
    // все "резиновые" превью Paint оставляли следы)
    HostCallOutcome result;
    result.returnValue = static_cast<uint32_t>(gdi::g_gdi.SetROP2(ReadArg(cpu, mem, 0), static_cast<int32_t>(ReadArg(cpu, mem, 1))));
    return result;
}

// DWORD GetLayout(HDC hdc) / SetLayout — направление письма (LTR/RTL),
// LAYOUT_LTR(0) — подавляющий дефолт для большинства игр
HostCallOutcome Impl_GetLayout(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

HostCallOutcome Impl_SetLayout(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0; // предыдущий layout
    return result;
}

// int ReleaseDC(HWND hWnd, HDC hDC) — у нас единый постоянный "экранный" DC,
// реально освобождать нечего (нет настоящего оконного менеджера с лимитом
// DC) — честно возвращаем "успех", как и было бы в реальности для valid DC
HostCallOutcome Impl_ReleaseDC(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// int lstrlenW(LPCWSTR lpString) — длина wide-строки в символах (не байтах),
// без учёта null-терминатора
HostCallOutcome Impl_LstrlenW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t ptr = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    uint32_t len = 0;
    if (ptr != 0) {
        while (len < 4096 && mem.Read16(ptr + len * 2) != 0) len++;
    }
    result.returnValue = len;
    return result;
}

// BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX*) — инициализация
// библиотеки common controls (статус-бары, тулбары и т.п.). Мы их пока не
// реализуем — честный no-op с успешным результатом.
HostCallOutcome Impl_InitCommonControlsEx(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

// HICON LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName) — у нас пока нет
// системы иконок (не критично для геймплея, пока иконка не рисуется
// попиксельно) — возвращаем постоянный ненулевой хендл
HostCallOutcome Impl_LoadIconW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0xFFFE0001u;
    return result;
}

// HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter,
//                            DWORD dwClsContext, REFIID riid, LPVOID* ppv) —
// у нас нет настоящего COM-реестра/активации объектов. Честно возвращаем
// REGDB_E_CLASSNOTREG (0x80040154) — ровно то же самое, что вернула бы
// РЕАЛЬНАЯ Windows, если запрошенный COM-компонент не зарегистрирован
// (обычный случай для опциональных фич тулбаров/шелл-интеграции — MFC-код
// написан с расчётом на то, что CoCreateInstance МОЖЕТ провалиться, и
// просто пропускает опциональную фичу). *ppv обнуляем, как и положено при
// ошибке.
HostCallOutcome Impl_CoCreateInstance(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t ppv = ReadArg(cpu, mem, 4);
    if (ppv != 0) mem.Write32(ppv, 0);
    HostCallOutcome result;
    result.returnValue = 0x80040154u; // REGDB_E_CLASSNOTREG
    return result;
}

// MFC42u.DLL ординал 2550 — ТОЧНО ИДЕНТИФИЦИРОВАН (официальная таблица
// экспортов): "?DragAcceptFiles@CWnd@@QAEXH@Z" = CWnd::DragAcceptFiles(BOOL
// bAccept), this=CWnd*. Реальная реализация просто читает m_hWnd и зовёт
// ::DragAcceptFiles(m_hWnd, bAccept) — у нас нет настоящего drag&drop, так
// что делаем честный no-op, НО заодно используем это как ПРОВЕРКУ гипотезы
// про смещение CWnd::m_hWnd=0x4 (см. HANDOFF/историю чата): если тут
// окажется НЕ похожий на HWND мусор (не в диапазоне win32::HWND_BASE),
// гипотеза неверна и требует пересмотра.
// MFC42u.DLL ординал 1172 — ТОЧНО ИДЕНТИФИЦИРОВАН (официальная таблица
// экспортов): "?AfxGetThread@@YGPAVCWinThread@@XZ" = глобальная функция
// AfxGetThread(), БЕЗ this (не метод класса). Общая заглушка возвращала
// ECX — бессмысленно для глобальной функции (ECX тут просто МУСОР,
// оставшийся от предыдущего вызова), что приводило к разыменованию
// случайного "потока" и крэшу (addr=0x7c — нашли экспериментально, эта
// сессия). В реальном MFC `class CWinApp : public CWinThread` — то есть
// theApp САМ ПО СЕБЕ уже является валидным CWinThread (через наследование),
// так что для однопоточного приложения честный и корректный ответ —
// просто вернуть theApp.
// MFC42u.DLL ординал 323 — ТОЧНО ИДЕНТИФИЦИРОВАН (официальная таблица
// экспортов): "??0CDC@@QAE@XZ" = CDC::CDC() — конструктор по умолчанию.
// Найдено в исходниках MFC (afxwin.h): "HDC m_hDC; // The output DC
// (must be first data member)" — тот же инвариант, что и у CWnd::m_hWnd,
// подтверждённый экспериментально в этой же сессии. CDC : public CObject
// (без CCmdTarget), значит m_hDC на +0x4 (сразу после vtable), следом
// m_hAttribDC на +0x8. Реальный конструктор по умолчанию просто зануляет
// оба хендла (реальный HDC появляется только после Attach()/GetDC() и
// т.п.) — vtable честно НЕ ставим (0), так как в наших уже изученных
// call-сайтах методы CDC вызываются через MFC-ординалы (this=ecx), а не
// через настоящую vtable-диспетчеризацию pDC — не нашли ни одного места,
// где это было бы нужно.
// MFC42u.DLL ординалы 1090/3193/3449 — ТОЧНО ИДЕНТИФИЦИРОВАНЫ (официальная
// таблица экспортов): CDocument::AddView(CView*), GetFirstViewPosition(),
// GetNextView(POSITION&). Раньше мы писали pView->m_pDocument вручную в
// хосте (в Impl_Mfc4604_OnFileNew), что подтвердило смещение +0x44, НО
// пропускало ВТОРУЮ половину настоящей AddView — регистрацию view в
// списке документа (m_viewList). Внешняя подсказка (эта сессия): вместо
// угадывания отдельных полей — реализовать САМ МЕХАНИЗМ, как получилось
// с m_hWnd. Поскольку AddView/GetFirstViewPosition/GetNextView реализуем
// ЦЕЛИКОМ САМИ (это ординалы MFC-библиотеки, не код paint.exe) — не
// обязательно повторять точную раскладку CPtrList внутри CDocument:
// достаточно внутренней самосогласованности между этими тремя. Для SDI
// (один документ — один view, как в Paint) храним просто ОДИН указатель
// на view на документ; POSITION — ненулевой сентинел, если view есть.
std::unordered_map<uint32_t, uint32_t> g_documentToView;

// ============================================================================
// Честная маршрутизация сообщений (AfxWndProc-эквивалент)
// ============================================================================
// Реальная MFC-цепочка: DispatchMessage -> AfxWndProc -> AfxCallWndProc ->
// CWnd::WindowProc -> OnWndMsg -> обход message map (по цепочке
// pBaseMap) -> вызов конкретного обработчика (OnPaint/OnLButtonDown/...).
//
// Мы реализуем это как ОБЫЧНУЮ host-функцию (используя уже существующий
// механизм IndexToHostAddress/TryHandleHostCall — тот же, что и для всех
// остальных host-реализаций Win32 API), которую регистрируем как wndProc
// для MFC-класса окна. DispatchMessageW уже умеет звать cls->wndProc как
// обычный WndProc — с точки зрения этого кода наша функция НЕ отличается
// от настоящей WndProc гостя.
//
// Формат AFX_MSGMAP_ENTRY (24 байта, подтверждено чтением реальных данных
// paint.exe): {nMessage, nCode, nID, nLastID, nSig, pfn} — все UINT/DWORD.
// Значения nSig — из официального afxmsg_.h (mfc42), подтверждены
// сопоставлением с реальными записями paint.exe (WM_SIZE->AfxSig_vwii=17,
// WM_COMMAND(простой)->AfxSig_vv=12, WM_COMMAND(update-ui)->AfxSig_cmdui=44,
// WM_LBUTTONDOWN/MOUSEMOVE->AfxSig_vwp=49 — совпадает с найденным ранее
// sig=0x31=49 для реальных обработчиков мыши CPaintView).

// hwnd -> C++ объект (CWnd*), для обратного поиска "кому доставить сообщение"
std::unordered_map<uint32_t, uint32_t> g_hwndToObject;
// C++ объект -> адрес массива записей его message map (entries, БЕЗ заголовка
// AFX_MSGMAP — то есть уже [pMsgMap+4], разыменованный)
std::unordered_map<uint32_t, uint32_t> g_objectToMsgMapEntries;

// Извлекает x/y как знаковые 16-битные половины lParam (стандартная
// упаковка Windows для мышиных сообщений)
static inline int16_t LowWordSigned(uint32_t v) { return static_cast<int16_t>(v & 0xFFFF); }
static inline int16_t HighWordSigned(uint32_t v) { return static_cast<int16_t>((v >> 16) & 0xFFFF); }

// Пытается найти и вызвать обработчик для (message) в конкретной message
// map. Возвращает true, если нашли и вызвали (outResult=LRESULT), false —
// если в ЭТОЙ карте подходящей записи нет (вызывающий код должен решить,
// что делать дальше — например, DefWindowProc).
bool DispatchViaMessageMap(cpu::CpuState& cpu, cpu::IGuestMemory& mem, uint32_t pWnd,
                            uint32_t entriesAddr, uint32_t message, uint32_t wParam,
                            uint32_t lParam, uint32_t& outResult) {
    if (entriesAddr == 0) return false;
    for (int i = 0; i < 2000; i++) { // разумный потолок — реальные карты редко больше пары сотен записей
        uint32_t entryAddr = entriesAddr + static_cast<uint32_t>(i) * 24;
        uint32_t nMessage = mem.Read32(entryAddr + 0);
        uint32_t nCode    = mem.Read32(entryAddr + 4);
        uint32_t nID      = mem.Read32(entryAddr + 8);
        uint32_t nLastID  = mem.Read32(entryAddr + 12);
        uint32_t nSig     = mem.Read32(entryAddr + 16);
        uint32_t pfn      = mem.Read32(entryAddr + 20);
        if (nMessage == 0 && nCode == 0 && nID == 0 && nLastID == 0 && nSig == 0 && pfn == 0) {
            break; // AfxSig_end — конец карты
        }

        bool matches = false;
        if (message == 0x0111 /*WM_COMMAND*/ && nMessage == 0x0111) {
            uint32_t cmdId = wParam & 0xFFFF;
            uint32_t notifyCode = (wParam >> 16) & 0xFFFF;
            if (cmdId >= nID && cmdId <= nLastID && nCode == notifyCode) matches = true;
        } else if (nMessage == message && nCode == 0) {
            matches = true;
        }
        if (!matches) continue;

        uint32_t ecxSave = cpu.Get(cpu::Reg32::ECX);
        cpu.Get(cpu::Reg32::ECX) = pWnd;
        uint32_t r = 0;
        bool handled = true;
        switch (nSig) {
            case 12: // AfxSig_vv — void (void)
                win32::CallGuestFunctionStdcall(cpu, mem, pfn, {}, g_tebAddr, g_tebAddr);
                break;
            case 9: // AfxSig_is — int (LPCREATESTRUCT) — lParam уже указывает на CREATESTRUCT
                r = win32::CallGuestFunctionStdcall(cpu, mem, pfn, {lParam}, g_tebAddr, g_tebAddr);
                break;
            case 17: { // AfxSig_vwii — void (UINT, int, int) — WM_SIZE
                uint32_t cx = static_cast<uint32_t>(static_cast<int32_t>(LowWordSigned(lParam)));
                uint32_t cy = static_cast<uint32_t>(static_cast<int32_t>(HighWordSigned(lParam)));
                win32::CallGuestFunctionStdcall(cpu, mem, pfn, {wParam, cx, cy}, g_tebAddr, g_tebAddr);
                break;
            }
            case 49: { // AfxSig_vwp — void (UINT, CPoint) — CPoint как ДВЕ отдельные DWORD (x,y)
                uint32_t px = static_cast<uint32_t>(static_cast<int32_t>(LowWordSigned(lParam)));
                uint32_t py = static_cast<uint32_t>(static_cast<int32_t>(HighWordSigned(lParam)));
                win32::CallGuestFunctionStdcall(cpu, mem, pfn, {wParam, px, py}, g_tebAddr, g_tebAddr);
                break;
            }
            case 1: // AfxSig_bD — BOOL (CDC*) — WM_ERASEBKGND; честного CDC* пока нет, передаём 0
                r = win32::CallGuestFunctionStdcall(cpu, mem, pfn, {0}, g_tebAddr, g_tebAddr);
                break;
            default:
                handled = false; // неизвестная сигнатура — не рискуем угадывать ABI
                break;
        }
        cpu.Get(cpu::Reg32::ECX) = ecxSave;
        if (handled) {
            outResult = r;
            return true;
        }
    }
    return false;
}

// Сама "AfxWndProc" — регистрируется как wndProc для MFC-окон. Ищет объект
// по hwnd, пытается его message map (и — TODO на будущее — цепочку
// pBaseMap для базовых классов), при неудаче честно возвращает 0 (как
// минимум эквивалент DefWindowProc для необработанных сообщений).
// "Текущее сообщение" — многие MFC-обработчики (найдено в этой сессии:
// 0x101c347, общая часть OnLButtonDown/OnMouseMove) не используют свои
// прямые параметры для координат мыши, а читают их через
// CWnd::GetCurrentMessage() (ординал 3016) — честный MSG в стиле реальной
// AfxGetCurrentMessage(), обновляем перед КАЖДОЙ доставкой сообщения.
uint32_t g_currentMsgAddr = 0;
void UpdateCurrentMessage(cpu::IGuestMemory& mem, uint32_t hwnd, uint32_t message,
                           uint32_t wParam, uint32_t lParam) {
    if (g_currentMsgAddr == 0) g_currentMsgAddr = SimpleGuestAlloc(28); // sizeof(MSG)
    mem.Write32(g_currentMsgAddr + 0, hwnd);
    mem.Write32(g_currentMsgAddr + 4, message);
    mem.Write32(g_currentMsgAddr + 8, wParam);
    mem.Write32(g_currentMsgAddr + 12, lParam);
    mem.Write32(g_currentMsgAddr + 16, 0); // time
    mem.Write32(g_currentMsgAddr + 20, lParam & 0xFFFF);        // pt.x (из lParam, как в реальной MSG для мыши)
    mem.Write32(g_currentMsgAddr + 24, (lParam >> 16) & 0xFFFF); // pt.y
}

// void InflateRect(LPRECT lprc, int dx, int dy) — сдвигает left/top на
// -dx/-dy, right/bottom на +dx/+dy (честная, прямая реализация контракта).
HostCallOutcome Impl_InflateRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lprc = ReadArg(cpu, mem, 0);
    int32_t dx = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t dy = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    HostCallOutcome result;
    if (lprc == 0) { result.returnValue = 0; return result; }
    int32_t left = static_cast<int32_t>(mem.Read32(lprc + 0));
    int32_t top = static_cast<int32_t>(mem.Read32(lprc + 4));
    int32_t right = static_cast<int32_t>(mem.Read32(lprc + 8));
    int32_t bottom = static_cast<int32_t>(mem.Read32(lprc + 12));
    mem.Write32(lprc + 0, static_cast<uint32_t>(left - dx));
    mem.Write32(lprc + 4, static_cast<uint32_t>(top - dy));
    mem.Write32(lprc + 8, static_cast<uint32_t>(right + dx));
    mem.Write32(lprc + 12, static_cast<uint32_t>(bottom + dy));
    result.returnValue = 1;
    return result;
}

// SHORT GetKeyState(int nVirtKey) — у нас нет непрерывного отслеживания
// состояния клавиш/кнопок мыши между вызовами (только то, что явно
// передано в конкретном сообщении) — честно возвращаем "не нажата" (0,
// старший бит очищен) для всех клавиш.
HostCallOutcome Impl_GetKeyState(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// BOOL IntersectRect(LPRECT lprcDst, const RECT* lprcSrc1, const RECT* lprcSrc2)
// — честное пересечение двух прямоугольников.
HostCallOutcome Impl_IntersectRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lprcDst = ReadArg(cpu, mem, 0);
    uint32_t lprcSrc1 = ReadArg(cpu, mem, 1);
    uint32_t lprcSrc2 = ReadArg(cpu, mem, 2);
    HostCallOutcome result;
    if (lprcDst == 0 || lprcSrc1 == 0 || lprcSrc2 == 0) { result.returnValue = 0; return result; }
    int32_t l1 = static_cast<int32_t>(mem.Read32(lprcSrc1 + 0)), t1 = static_cast<int32_t>(mem.Read32(lprcSrc1 + 4));
    int32_t r1 = static_cast<int32_t>(mem.Read32(lprcSrc1 + 8)), b1 = static_cast<int32_t>(mem.Read32(lprcSrc1 + 12));
    int32_t l2 = static_cast<int32_t>(mem.Read32(lprcSrc2 + 0)), t2 = static_cast<int32_t>(mem.Read32(lprcSrc2 + 4));
    int32_t r2 = static_cast<int32_t>(mem.Read32(lprcSrc2 + 8)), b2 = static_cast<int32_t>(mem.Read32(lprcSrc2 + 12));
    int32_t l = std::max(l1, l2), t = std::max(t1, t2);
    int32_t r = std::min(r1, r2), b = std::min(b1, b2);
    if (l >= r || t >= b) {
        mem.Write32(lprcDst + 0, 0); mem.Write32(lprcDst + 4, 0);
        mem.Write32(lprcDst + 8, 0); mem.Write32(lprcDst + 12, 0);
        result.returnValue = 0;
        return result;
    }
    mem.Write32(lprcDst + 0, static_cast<uint32_t>(l)); mem.Write32(lprcDst + 4, static_cast<uint32_t>(t));
    mem.Write32(lprcDst + 8, static_cast<uint32_t>(r)); mem.Write32(lprcDst + 12, static_cast<uint32_t>(b));
    result.returnValue = 1;
    return result;
}

// HWND GetCapture() — честно переиспользуем уже существующий g_captureWindow.
HostCallOutcome Impl_GetCapture(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_captureWindow;
    return result;
}

// HWND SetActiveWindow(HWND hWnd) — честно отслеживаем "активное окно"
// (похожий паттерн на SetFocus/SetCursor), возвращаем предыдущее.
uint32_t g_activeWindowHwnd = 0;
HostCallOutcome Impl_SetActiveWindow(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = g_activeWindowHwnd;
    g_activeWindowHwnd = hwnd;
    return result;
}

// HWND GetFocus() — честно отслеживаем текущий фокус (тот же паттерн, что
// и SetCursor).
uint32_t g_currentFocusHwnd = 0;
HostCallOutcome Impl_GetFocus(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_currentFocusHwnd;
    return result;
}

// MFC42u.DLL ординал 2859 — CWnd::FromHandle(HWND) — статический метод,
// конвертирует raw HWND в CWnd* через карту хендлов. У нас уже есть
// ГОТОВАЯ карта (g_hwndToObject, используется диспетчером сообщений) —
// честно переиспользуем её, а не дублируем логику.
HostCallOutcome Impl_Mfc2859_FromHandle(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    auto it = g_hwndToObject.find(hwnd);
    result.returnValue = (it != g_hwndToObject.end()) ? it->second : 0;
    return result;
}

// MFC42u.DLL ординал 5977 — CWnd::SetFocus() — this=CWnd*, читаем его
// m_hWnd (+0x4, подтверждённое смещение) и честно устанавливаем как
// текущий фокус, возвращаем ПРЕДЫДУЩИЙ CWnd* фокуса (через обратный
// поиск по g_hwndToObject).
HostCallOutcome Impl_Mfc5977_SetFocus(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pWnd = cpu.Get(cpu::Reg32::ECX);
    uint32_t newHwnd = mem.Read32(pWnd + 0x4);
    uint32_t oldHwnd = g_currentFocusHwnd;
    g_currentFocusHwnd = newHwnd;
    HostCallOutcome result;
    auto it = g_hwndToObject.find(oldHwnd);
    result.returnValue = (it != g_hwndToObject.end()) ? it->second : 0;
    return result;
}

// MFC42u.DLL ординал 3016 — CWnd::GetCurrentMessage() — статический
// метод, возвращает указатель на MSG текущего обрабатываемого сообщения.
// Честно поддерживаем через UpdateCurrentMessage (см. выше,
// вызывается из Impl_AfxWndProc на каждую доставку сообщения).
HostCallOutcome Impl_Mfc3016_GetCurrentMessage(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_currentMsgAddr;
    return result;
}

// ============================================================================
// Честная "живая" отрисовка мышью (эта сессия)
// ============================================================================
// ПОДТВЕРЖДЕНО ЭКСПЕРИМЕНТАЛЬНО: реальный код paint.exe рисует через
// vtable-метод +0x2c ТЕКУЩЕГО ИНСТРУМЕНТА (найден через честный реестр
// инструментов [0x103e900] — связный список, id на +0x30), не через
// message-map view напрямую (см. полную трассировку 0x101ba61 —
// формальную message-map цепочку до обработчиков мыши найти не удалось,
// десятки похожих кластеров в бинарнике; вместо этого честно
// воспроизводим то же самое действие, которое делает 0x101ba61 при
// WM_MOUSEMOVE с зажатой кнопкой). Сигнатура: (this=инструмент,
// arg1=view [читает [view+0x40] — честная canvas-структура],
// arg2={точка+0x8/0xc, предыдущая точка+0x10/0x14, цвет
// переднего/заднего плана +0x18/+0x20 — подтверждено прямой проверкой
// пикселя: 1=чёрный(рисует), 0=белый(невидимо)}).
//
// [0x103d74c] — НАСТОЯЩИЙ текущий инструмент (не хардкод!) — тот же
// глобальный указатель, что использует сам paint.exe.
std::unordered_map<uint32_t, std::pair<int,int>> g_lastMousePos; // hwnd -> (x,y)
// (g_paintCDC/g_paintView перенесены в начало файла, до анонимного
// namespace — см. комментарий там; здесь оставлять их было ошибкой:
// не ломало сборку раньше только потому, что ничего СНАРУЖИ файла к ним
// не обращалось напрямую — до теста Пипетки в этой сессии)

void DrawStrokeSegment(cpu::CpuState& cpu, cpu::IGuestMemory& mem, uint32_t pView,
                        int prevX, int prevY, int curX, int curY, bool useForeground) {
    uint32_t tool = mem.Read32(0x103d74c);
    if (tool == 0) return;

    // Пипетка (id=0x27f) — эта сессия: НЕ идём через настоящую
    // vtable+0x2c/цепочку 0x1015722. Дизассемблированием и сквозным
    // тестом подтверждено: честный код глубоко внутри (через несколько
    // виртуальных вызовов на объектах внутреннего MFC-состояния paint.exe,
    // которое мы честно никогда не строили — своя отрисовка вместо
    // настоящих CToolBar/CView-объектов) регулярно натыкается на объекты
    // с невалидным/несовместимым состоянием — каждый исправленный краш
    // приводил к следующему, глубже. Реализуем ФУНКЦИОНАЛЬНО тот же
    // результат напрямую: честный GetPixel через наш GDI + честные
    // SetForegroundColor/BackgroundColor через уже проверенный сигнальный
    // механизм (тот же, что использует палитра) — то же самое, что делает
    // настоящий код, просто в обход хрупкой внутренней цепочки.
    uint32_t toolId = mem.Read32(tool + 0x30);
    if (toolId == 0x27fu) {
        if (g_paintCDC != 0) {
            uint32_t hdc = mem.Read32(g_paintCDC + 0x4);
            uint32_t color = gdi::g_gdi.GetPixel(hdc, curX, curY);
            if (useForeground) g_requestedForegroundColor = color;
            else g_requestedBackgroundColor = color;
        }
        return;
    }

    uint32_t pointStruct = SimpleGuestAlloc(64);
    mem.Write32(pointStruct + 0x8, static_cast<uint32_t>(curX));
    mem.Write32(pointStruct + 0xc, static_cast<uint32_t>(curY));
    mem.Write32(pointStruct + 0x10, static_cast<uint32_t>(prevX));
    mem.Write32(pointStruct + 0x14, static_cast<uint32_t>(prevY));
    mem.Write32(pointStruct + 0x18, useForeground ? 1u : 0u);
    mem.Write32(pointStruct + 0x20, useForeground ? 1u : 0u);

    // ИСПРАВЛЕНО (эта сессия): раньше здесь был ЖЁСТКО зашит адрес
    // 0x1015722 — это СОБСТВЕННЫЙ vtable+0x2c именно Карандаша
    // (подтверждено дампом реестра инструментов), не общий диспетчер!
    // Для любого ДРУГОГО инструмента (Кисть и т.д.) это вызывало код
    // Карандаша с "this", указывающим на объект СОВСЕМ ДРУГОГО типа —
    // классическая путаница типов: код Карандаша читал свои собственные
    // поля-смещения объекта, получая чужую раскладку памяти на тех же
    // смещениях. Отсюда и крах на мусорном указателе у Кисти (EIP=0x2).
    // Честный фикс: виртуальный вызов через vtable ТЕКУЩЕГО инструмента
    // — ровно то же самое, что мы уже честно делаем для +0x3c при
    // выборе инструмента. Для Карандаша это даст ТОТ ЖЕ 0x1015722
    // (совпадает по данным реестра) — для остальных теперь их
    // СОБСТВЕННЫЙ, правильный код.
    uint32_t toolVtable = mem.Read32(tool);
    uint32_t slot2c = (toolVtable != 0) ? mem.Read32(toolVtable + 0x2c) : 0;
    if (slot2c == 0) return; // нет честной реализации (например, no-op для Лупы/Заливки) — не звоним в никуда

    uint32_t ecxT = cpu.Get(cpu::Reg32::ECX);
    uint32_t ebxT = cpu.Get(cpu::Reg32::EBX);
    uint32_t esiT = cpu.Get(cpu::Reg32::ESI);
    uint32_t ediT = cpu.Get(cpu::Reg32::EDI);
    cpu.Get(cpu::Reg32::ECX) = tool;
    win32::CallGuestFunctionStdcall(cpu, mem, slot2c, {pView, pointStruct}, g_tebAddr, g_tebAddr, 20000);
    cpu.Get(cpu::Reg32::ECX) = ecxT;
    cpu.Get(cpu::Reg32::EBX) = ebxT;
    cpu.Get(cpu::Reg32::ESI) = esiT;
    cpu.Get(cpu::Reg32::EDI) = ediT;
}

HostCallOutcome Impl_AfxWndProc(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd    = ReadArg(cpu, mem, 0);
    uint32_t message = ReadArg(cpu, mem, 1);
    uint32_t wParam  = ReadArg(cpu, mem, 2);
    uint32_t lParam  = ReadArg(cpu, mem, 3);
    UpdateCurrentMessage(mem, hwnd, message, wParam, lParam);

    // Запоминаем точку нажатия — от неё пойдёт первый сегмент штриха.
    // Рисуем на ИЗВЕСТНОМ g_paintView, а не на pWnd — сообщение может
    // формально прийти на hwnd фрейма (или другого окна), у которого
    // +0x40 никогда не устанавливался. В нашем приложении реально только
    // ОДИН рисуемый view, так что это безопасное и корректное упрощение.
    if (message == 0x0201 || message == 0x0204) { // WM_LBUTTONDOWN / WM_RBUTTONDOWN
        int curX = static_cast<int16_t>(lParam & 0xFFFF);
        int curY = static_cast<int16_t>((lParam >> 16) & 0xFFFF);
        g_lastMousePos[hwnd] = {curX, curY};

        // Пипетка (эта сессия): срабатывает уже на само нажатие, не
        // только на последующее движение — реальный Eyedropper в Paint
        // подбирает цвет сразу по клику, без необходимости двигать
        // мышь (простой тап должен сработать, не только перетаскивание).
        uint32_t curTool = mem.Read32(0x103d74c);
        if (curTool != 0 && mem.Read32(curTool + 0x30) == 0x27fu && g_paintCDC != 0) {
            uint32_t hdc = mem.Read32(g_paintCDC + 0x4);
            uint32_t color = gdi::g_gdi.GetPixel(hdc, curX, curY);
            if (message == 0x0201) g_requestedForegroundColor = color;
            else g_requestedBackgroundColor = color;
        }
    }

    // Честное рисование: WM_MOUSEMOVE с зажатой левой(рисует)/правой
    // (тоже рисует, других цветом) кнопкой.
    if (message == 0x0200 && (wParam & 0x3) != 0 && g_paintCDC != 0 && g_paintView != 0) {
        int curX = static_cast<int16_t>(lParam & 0xFFFF);
        int curY = static_cast<int16_t>((lParam >> 16) & 0xFFFF);
        auto it = g_lastMousePos.find(hwnd);
        int prevX = (it != g_lastMousePos.end()) ? it->second.first : curX;
        int prevY = (it != g_lastMousePos.end()) ? it->second.second : curY;
        bool leftButton = (wParam & 0x1) != 0;
        DrawStrokeSegment(cpu, mem, g_paintView, prevX, prevY, curX, curY, leftButton);
        g_lastMousePos[hwnd] = {curX, curY};

        // Перерисовать экран настоящим OnDraw+BitBlt, чтобы штрих
        // сразу стал виден.
        uint32_t ecxOD = cpu.Get(cpu::Reg32::ECX);
        uint32_t ebxOD = cpu.Get(cpu::Reg32::EBX);
        uint32_t esiOD = cpu.Get(cpu::Reg32::ESI);
        uint32_t ediOD = cpu.Get(cpu::Reg32::EDI);
        cpu.Get(cpu::Reg32::ECX) = g_paintView;
        win32::CallGuestFunctionStdcall(cpu, mem, 0x1021493u, {g_paintCDC}, g_tebAddr, g_tebAddr, 20000);
        cpu.Get(cpu::Reg32::ECX) = ecxOD;
        cpu.Get(cpu::Reg32::EBX) = ebxOD;
        cpu.Get(cpu::Reg32::ESI) = esiOD;
        cpu.Get(cpu::Reg32::EDI) = ediOD;
    }

    // Пипетка (эта сессия): раньше здесь был отдельный вызов настоящего
    // vtable+0x28 на WM_LBUTTONUP для применения подобранного цвета —
    // УБРАН. Найдено сквозным тестом: реальная цепочка (vtable+0x2c
    // через 0x1015722, а затем vtable+0x28 через 0x10172b2) регулярно
    // натыкается на объекты внутреннего MFC-состояния paint.exe с
    // невалидным состоянием (которое мы честно никогда не строим — своя
    // отрисовка вместо настоящих CToolBar/CView). Теперь честное чтение
    // пикселя (GetPixel) И применение цвета (через сигнальный механизм
    // g_requestedForeground/BackgroundColor, ProcessColorSelectionRequest)
    // делаются ЗА ОДИН РАЗ прямо в DrawStrokeSegment — см. её код выше,
    // строго только для инструмента с id=0x27f, остальные инструменты не
    // затронуты.

    // Ниже — маршрутизация через message map (WM_CREATE/WM_SIZE и т.д.),
    // ей по-прежнему нужен pWnd, известный объект для ЭТОГО hwnd. Если
    // hwnd незнаком — как и раньше, просто нет обработчика (аналог
    // DefWindowProc), но рисование ВЫШЕ уже отработало независимо от этого.
    HostCallOutcome result;
    auto objIt = g_hwndToObject.find(hwnd);
    if (objIt == g_hwndToObject.end()) { result.returnValue = 0; return result; }
    uint32_t pWnd = objIt->second;

    auto mapIt = g_objectToMsgMapEntries.find(pWnd);
    uint32_t entriesAddr = (mapIt != g_objectToMsgMapEntries.end()) ? mapIt->second : 0;

    uint32_t r = 0;
    if (DispatchViaMessageMap(cpu, mem, pWnd, entriesAddr, message, wParam, lParam, r)) {
        result.returnValue = r;
        return result;
    }
    result.returnValue = 0; // не нашли обработчик — как DefWindowProc по умолчанию
    return result;
}

HostCallOutcome Impl_Mfc1090_AddView(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pDocument = cpu.Get(cpu::Reg32::ECX);
    uint32_t pView = ReadArg(cpu, mem, 0);
    g_documentToView[pDocument] = pView;
    mem.Write32(pView + 0x44, pDocument); // CView::m_pDocument — подтверждённое смещение
    HostCallOutcome result;
    return result;
}

HostCallOutcome Impl_Mfc3193_GetFirstViewPosition(cpu::CpuState& cpu, cpu::IGuestMemory&) {
    uint32_t pDocument = cpu.Get(cpu::Reg32::ECX);
    HostCallOutcome result;
    auto it = g_documentToView.find(pDocument);
    result.returnValue = (it != g_documentToView.end() && it->second != 0) ? 1u : 0u; // POSITION-сентинел
    return result;
}

HostCallOutcome Impl_Mfc3449_GetNextView(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pDocument = cpu.Get(cpu::Reg32::ECX);
    uint32_t posRef = ReadArg(cpu, mem, 0); // POSITION& (адрес переменной вызывающего)
    HostCallOutcome result;
    auto it = g_documentToView.find(pDocument);
    uint32_t view = (it != g_documentToView.end()) ? it->second : 0;
    if (posRef != 0) mem.Write32(posRef, 0); // единственный view в SDI — следующей позиции нет
    result.returnValue = view;
    return result;
}

// Безопасная заглушка для ЛЮБОГО виртуального слота синтетического CDC
// vtable (см. ниже) — найдено этой сессией: настоящий код paint.exe
// иногда делает ВИРТУАЛЬНЫЙ вызов на CDC-объекте (например, через
// FromHandle), а мы честно не эмулируем реальный CDC vtable (см.
// комментарий у конструктора CDC) — раньше это означало vtable=0,
// что гарантированно крашило ЛЮБОЙ такой вызов (обращение по адресу
// [0+offset]). Теперь вместо 0 используем СИНТЕТИЧЕСКИЙ vtable, где
// КАЖДЫЙ слот указывает на эту безопасную заглушку — она ничего не
// делает и возвращает 0.
HostCallOutcome Impl_SafeCdcVtableSlot(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// Лениво строим один общий синтетический vtable (переиспользуется всеми
// CDC-обёртками) — 32 слота по 4 байта, все указывают на безопасную
// заглушку выше. С запасом (реально видели только +0x2c, но лучше
// перекрыть больше, чем гадать заново при следующем похожем крахе).
uint32_t g_safeCdcVtable = 0;
uint32_t GetOrCreateSafeCdcVtable(cpu::IGuestMemory& mem) {
    if (g_safeCdcVtable != 0) return g_safeCdcVtable;
    constexpr int kSlots = 32;
    uint32_t vt = SimpleGuestAlloc(kSlots * 4);
    int idx = FindHostFunction("INTERNAL", "SafeCdcVtableSlot");
    uint32_t stubAddr = (idx >= 0) ? IndexToHostAddress(idx) : 0;
    for (int i = 0; i < kSlots; i++) {
        mem.Write32(vt + i * 4, stubAddr);
    }
    g_safeCdcVtable = vt;
    return vt;
}

// MFC42u.DLL ординал 5784 — "CPen* CDC::SelectObject(CPen* pPen)".
// НАЙДЕНО КАК НАСТОЯЩИЙ КОРЕНЬ КРАША Кисти (эта сессия, подтверждено
// точной трассировкой ESI/ESP): раньше ушла в generic-заглушку для
// неизвестных MFC-ординалов, которая чистит стек ТОЛЬКО под адрес
// возврата (cdecl-допущение) — а у этого метода ЕСТЬ один настоящий
// аргумент на стеке (pPen), который оставался непочищенным. Это
// сдвигало стек вызывающего кода на 4 байта, из-за чего его
// `pop esi` в эпилоге читал не то место — отсюда мусорный esi=2 и
// крах при попытке рисовать Кистью. Честная реализация: берём hdc из
// this->+0x4 (наш подтверждённый CDC layout), hPen из pPen->+0x4
// (стандартная MFC-раскладка CGdiObject — vtable+0, m_hObject+4, та
// же конвенция, что и у нашего собственного CDC), вызываем настоящий
// SelectObject через уже честно работающий gdi::g_gdi.SelectObject, и
// оборачиваем СТАРЫЙ хендл в новый синтетический CPen*-подобный
// объект (тот же безопасный vtable-паттерн, что и у CDC — 12 байт,
// vtable+m_hObject+резерв).
HostCallOutcome Impl_Mfc5784_CDC_SelectObject_Pen(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t cdcObj = cpu.Get(cpu::Reg32::ECX);
    uint32_t pPen = ReadArg(cpu, mem, 0);

    uint32_t hdc = mem.Read32(cdcObj + 0x4);
    uint32_t hPen = (pPen != 0) ? mem.Read32(pPen + 0x4) : 0;

    uint32_t oldHandle = gdi::g_gdi.SelectObject(hdc, hPen);

    uint32_t newPenObj = SimpleGuestAlloc(12);
    mem.Write32(newPenObj + 0x0, GetOrCreateSafeCdcVtable(mem)); // безопасный vtable — тот же паттерн, что и CDC
    mem.Write32(newPenObj + 0x4, oldHandle); // m_hObject — старый хендл, честно из SelectObject
    mem.Write32(newPenObj + 0x8, 0);

    HostCallOutcome result;
    result.returnValue = newPenObj;
    return result;
}

HostCallOutcome Impl_Mfc323_CDC_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t thisPtr = cpu.Get(cpu::Reg32::ECX);
    // vtable — раньше был честно 0 ("не эмулируем"), но найдено этой
    // сессией: настоящий код paint.exe иногда делает ВИРТУАЛЬНЫЙ вызов
    // на CDC-объекте, и vtable=0 гарантированно крашил такой вызов
    // (обращение по адресу [0+offset]). Теперь используем безопасный
    // синтетический vtable (см. GetOrCreateSafeCdcVtable выше) — все
    // слоты ведут в no-op заглушку, виртуальные вызовы больше не падают.
    mem.Write32(thisPtr + 0x0, GetOrCreateSafeCdcVtable(mem));
    mem.Write32(thisPtr + 0x4, 0);  // m_hDC
    mem.Write32(thisPtr + 0x8, 0);  // m_hAttribDC
    HostCallOutcome result;
    result.returnValue = thisPtr;
    return result;
}

// MFC42u.DLL ординал 2854 — "CDC* CDC::FromHandle(HDC)" (static-метод,
// оборачивает СУЩЕСТВУЮЩИЙ HDC во временный CDC-объект). НАЙДЕНО КАК
// НАСТОЯЩИЙ КРАШ (эта сессия): вызывается из SelectTool при переключении
// ЛЮБОГО инструмента через тулбар (не только Пипетки) — судя по всему,
// как часть обновления paint.exe's СОБСТВЕННОГО внутреннего
// CToolBar/CStatusBar-объекта (показ имени текущего инструмента), 
// которого мы честно никогда не создавали (рисуем свой UI поверх
// фреймбуфера вместо настоящих MFC control-объектов). Раньше эта
// функция ВООБЩЕ не была implemented — падала в generic-заглушку,
// которая просто печатает и возвращает 0, а вызывающий код (реальный
// paint.exe) потом РАЗЫМЕНОВЫВАЕТ этот 0/мусор как указатель на
// CDC — вот и падение. Честная, но ЗАЩИЩЁННАЯ реализация: оборачиваем
// ЛЮБОЙ hdc (даже потенциально "мусорный", раз объект-владелец honest
// не создан) в минимальный CDC — этого достаточно, чтобы код НЕ падал;
// сама отрисовка через такой CDC либо честно сработает (если hdc
// реально валиден), либо останется безопасным no-op (наши GDI-функции
// уже везде безопасны при неизвестном hdc).
// НАЙДЕН И ИСПРАВЛЕН ВТОРОЙ КРАШ (эта сессия, продолжение): даже с
// честной обёрткой выше, vtable=0 крашил ЛЮБОЙ последующий виртуальный
// вызов на возвращённом CDC-объекте (подтверждено breakpoint-дампом
// регистров: ECX=реальный объект, [ECX]=0, крах на call [0+0x2c]).
// Используем ту же безопасную синтетическую vtable, что и в
// конструкторе CDC — виртуальные вызовы теперь безопасный no-op вместо
// падения.
HostCallOutcome Impl_Mfc2854_CDC_FromHandle(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    // ИСПРАВЛЕНО (полный аудит ABI): это static PASCAL-метод
    // ("?FromHandle@CDC@@SGPAV1@PAUHDC__@@@Z": S = static, G = __stdcall),
    // у него НЕТ this в ECX, а HDC лежит на СТЕКЕ, и функция сама снимает
    // эти 4 байта. Раньше мы читали HDC из ECX (там был мусор, например
    // координата 0x30) и не чистили стек. Подтверждено в рантайме на пути
    // Кисти: при вызове [esp]=0xa0000006 (настоящий HDC), ESP после
    // возврата не менялся. Сдвиг на 4 байта портил аргументы 0x10148cb и
    // эпилог 0x1014d28, отсюда CDC=0 в 0x101282e и краш Кисти.
    uint32_t hdc = ReadArg(cpu, mem, 0);
    uint32_t cdcObj = SimpleGuestAlloc(12);
    mem.Write32(cdcObj + 0x0, GetOrCreateSafeCdcVtable(mem));
    mem.Write32(cdcObj + 0x4, hdc); // m_hDC
    mem.Write32(cdcObj + 0x8, hdc); // m_hAttribDC: в MFC у обёртки из FromHandle он равен m_hDC
    HostCallOutcome result;
    result.returnValue = cdcObj;
    return result;
}

// MFC42u.DLL ординал 414 — ТОЧНО ИДЕНТИФИЦИРОВАН: "??0CObArray@@QAE@XZ"
// = CObArray::CObArray(). Раскладка из исходников MFC (afxcoll.h):
// "CObject** m_pData; INT_PTR m_nSize; INT_PTR m_nMaxSize; INT_PTR
// m_nGrowBy;" — в MFC42 (32-бит, 1998) INT_PTR это обычный int (4 байта),
// так что +0x4/+0x8/+0xC/+0x10 соответственно. Пустой массив — ровно то,
// что делает реальный конструктор по умолчанию (без Create/SetSize).
HostCallOutcome Impl_Mfc414_CObArray_Ctor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t thisPtr = cpu.Get(cpu::Reg32::ECX);
    mem.Write32(thisPtr + 0x0, 0);  // vtable — как и для CDC, честно не эмулируем
    mem.Write32(thisPtr + 0x4, 0);  // m_pData
    mem.Write32(thisPtr + 0x8, 0);  // m_nSize
    mem.Write32(thisPtr + 0xC, 0);  // m_nMaxSize
    mem.Write32(thisPtr + 0x10, 0); // m_nGrowBy
    HostCallOutcome result;
    result.returnValue = thisPtr;
    return result;
}

// MFC42u.DLL ординал 4458 — ТОЧНО ИДЕНТИФИЦИРОВАН: "?OnCreate@CView@@..."
// = CView::OnCreate(LPCREATESTRUCT), возвращает int (0=успех, -1=отмена
// создания окна) — НЕ указатель "this", поэтому общая заглушка
// "return ECX" тут семантически неверна (возвращала большое число вместо
// 0/-1). Честная базовая реализация CView::OnCreate ничего сложного не
// делает (в основном заглушка под переопределение) — возвращаем явный 0.
// MFC42u.DLL ординал 4451 — "?OnCreate@CFrameWnd@@..." = CFrameWnd::OnCreate
// (LPCREATESTRUCT), тот же случай, что и 4458 (CView::OnCreate) — возвращает
// int (0/-1), не "this". Честный явный 0.
HostCallOutcome Impl_Mfc4451_CFrameWndOnCreate(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// MFC42u.DLL ординал 4884 — "?OnPrepareDC@CView@@..." = CView::OnPrepareDC
// (CDC*, CPrintInfo*), void, 2 stack-арга = 8 байт. НЕ был зарегистрирован
// явно — шёл через generic-заглушку (AllocateMfcStubSlot), которая чистит
// строго 4 байта — тот же класс бага с дрейфом ESP, что мы чинили
// несколько раз за проект. Реальная база CView::OnPrepareDC в простом
// случае (не печать) почти ничего не делает — честный no-op с правильной
// очисткой стека достаточен.
// ============================================================================
// OLEAUT32.dll — честная поддержка BSTR
// ============================================================================
// Настоящий формат BSTR (подтверждено официальной документацией и
// исходниками Wine/ReactOS, не выдумано): указатель ведёт на UTF-16 данные,
// а ПЕРЕД ним (по адресу ptr-4) лежит DWORD — длина строки В БАЙТАХ (без
// учёта завершающего нуля). Сама строка тоже завершается нулевым символом
// (2 байта) для совместимости с обычными C-строками. SysStringLen
// возвращает длину В СИМВОЛАХ (байты/2).
//
// Ординалы подтверждены НЕЗАВИСИМО несколькими источниками (Wine, ReactOS,
// официальная документация Microsoft, скрипт экспортов Ghidra/Hopper) —
// не гадание: 2=SysAllocString, 3=SysReAllocString, 4=SysAllocStringLen,
// 5=SysReAllocStringLen, 6=SysFreeString, 7=SysStringLen.
//
// Найдено в этой сессии: вызывались МОЛЧА через тот же generic-фолбэк, что
// и нереализованные MFC-ординалы (OLEAUT32.dll ни разу не была замечена
// отдельно) — SysAllocString/SysFreeString возвращали ECX ("this", не
// имеющий отношения к BSTR!) вместо настоящей строки, что и приводило к
// падению глубже по цепочке OLE-интерфейсов COleServerItem.

uint32_t AllocBSTR(cpu::IGuestMemory& mem, const uint16_t* chars, uint32_t charCount) {
    uint32_t lenBytes = charCount * 2;
    uint32_t buf = SimpleGuestAlloc(4 + lenBytes + 2); // длина + данные + null-терминатор
    mem.Write32(buf, lenBytes);
    uint32_t dataPtr = buf + 4;
    for (uint32_t i = 0; i < charCount; i++) {
        mem.Write16(dataPtr + i * 2, chars ? chars[i] : 0);
    }
    mem.Write16(dataPtr + lenBytes, 0); // терминатор
    return dataPtr;
}

// BSTR SysAllocString(LPCOLESTR psz) — копирует ДО null-терминатора psz.
HostCallOutcome Impl_SysAllocString(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t psz = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    if (psz == 0) { result.returnValue = 0; return result; } // как настоящая — NULL на NULL
    uint32_t len = 0;
    while (mem.Read16(psz + len * 2) != 0) len++;
    std::vector<uint16_t> chars(len);
    for (uint32_t i = 0; i < len; i++) chars[i] = mem.Read16(psz + i * 2);
    result.returnValue = AllocBSTR(mem, chars.data(), len);
    return result;
}

// BSTR SysAllocStringLen(LPCOLESTR psz, UINT len) — явная длина в символах,
// psz МОЖЕТ быть NULL (тогда просто выделяем буфер нужного размера, честно
// обнулённый — SimpleGuestAlloc и так возвращает обнулённую память).
HostCallOutcome Impl_SysAllocStringLen(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t psz = ReadArg(cpu, mem, 0);
    uint32_t len = ReadArg(cpu, mem, 1);
    HostCallOutcome result;
    if (psz == 0) {
        result.returnValue = AllocBSTR(mem, nullptr, len);
        return result;
    }
    std::vector<uint16_t> chars(len);
    for (uint32_t i = 0; i < len; i++) chars[i] = mem.Read16(psz + i * 2);
    result.returnValue = AllocBSTR(mem, chars.data(), len);
    return result;
}

// void SysFreeString(BSTR bstr) — у нас бамп-аллокатор (SimpleGuestAlloc
// никогда не освобождает память, тот же подход, что и для operator delete
// весь проект) — честный no-op, реальная семантика (bstr может быть NULL,
// тогда тоже ничего не делает) сохранена в том смысле, что поведение
// снаружи неотличимо от настоящего освобождения.
HostCallOutcome Impl_SysFreeString(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    return result;
}

// UINT SysStringLen(BSTR bstr) — длина В СИМВОЛАХ, читаем префикс [bstr-4]
// (байты) и делим на 2. bstr=NULL — длина 0 (как настоящая).
HostCallOutcome Impl_SysStringLen(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t bstr = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    if (bstr == 0) { result.returnValue = 0; return result; }
    result.returnValue = mem.Read32(bstr - 4) / 2;
    return result;
}

// MFC42u.DLL ординал 6065 — "?SetPaneText@CStatusBar@@..." =
// CStatusBar::SetPaneText(int nIndex, LPCTSTR lpszNewText, BOOL bUpdate),
// возвращает BOOL. Похоже, вызывается при движении мыши для показа
// координат в статус-баре — у нас нет настоящего статус-бара с текстом,
// честный безопасный no-op с правильной очисткой стека (3 аргумента =
// 12 байт) — раньше шёл через generic-заглушку, что-то в ней вызывало
// крэш при попытке использовать возврат.
HostCallOutcome Impl_Mfc6065_SetPaneText(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

HostCallOutcome Impl_Mfc4884_OnPrepareDC(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    return result;
}

HostCallOutcome Impl_Mfc4458_CViewOnCreate(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

HostCallOutcome Impl_Mfc1172_AfxGetThread(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = g_theAppPtr;
    return result;
}

HostCallOutcome Impl_Mfc2550_DragAcceptFiles(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t thisPtr = cpu.Get(cpu::Reg32::ECX);
    uint32_t hwndField = mem.Read32(thisPtr + 0x4);
    if (cpu::g_consoleAvailable) {
        bool looksValid = (hwndField >= win32::HWND_BASE);
        std::printf("[ПРОВЕРКА m_hWnd] this=0x%x, [this+4]=0x%x — %s\n",
                    thisPtr, hwndField, looksValid ? "похоже на настоящий HWND" : "НЕ похоже на HWND (гипотеза неверна?)");
    }
    HostCallOutcome result2;
    result2.returnValue = thisPtr;
    return result2;
}

// нет настоящей многопоточности (весь интерпретатор однопоточный), так что
// простое чтение-изменение-запись честно эквивалентно "атомарной" версии.
HostCallOutcome Impl_InterlockedIncrement(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t addr = ReadArg(cpu, mem, 0);
    uint32_t val = mem.Read32(addr) + 1;
    mem.Write32(addr, val);
    HostCallOutcome result;
    result.returnValue = val;
    return result;
}
HostCallOutcome Impl_InterlockedDecrement(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t addr = ReadArg(cpu, mem, 0);
    uint32_t val = mem.Read32(addr) - 1;
    mem.Write32(addr, val);
    HostCallOutcome result;
    result.returnValue = val;
    return result;
}
// логика, что и LoadIconW: у нас нет системы курсоров, но код обычно просто
// передаёт хендл дальше (в WNDCLASS), не проверяя его содержимое
HostCallOutcome Impl_LoadCursorW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0xFFFD0001u;
    return result;
}

// HCURSOR SetCursor(HCURSOR hCursor) — стандартная семантика: устанавливает
// новый курсор, возвращает ПРЕДЫДУЩИЙ хендл (0 при самом первом вызове).
// У нас нет реального рендеринга курсора (пока), но честно отслеживаем
// хендл и возвращаем предыдущий — этого достаточно для guest-кода, который
// обычно просто использует возврат для восстановления курсора позже.
uint32_t g_currentCursor = 0;
HostCallOutcome Impl_SetCursor(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hCursor = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = g_currentCursor;
    g_currentCursor = hCursor;
    return result;
}

// ATOM RegisterClassW(const WNDCLASSW* lpwc) — сохраняем зарегистрированный
// класс, ГЛАВНОЕ ЗДЕСЬ — адрес WndProc (offset 4), он понадобится, когда
// будем реализовывать реальный вызов оконной процедуры через DispatchMessage.
//
// Смещения полей WNDCLASSW (32-бит): style=0, lpfnWndProc=4, cbClsExtra=8,
// cbWndExtra=12, hInstance=16, hIcon=20, hCursor=24, hbrBackground=28,
// lpszMenuName=32, lpszClassName=36
HostCallOutcome Impl_RegisterClassW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpwc = ReadArg(cpu, mem, 0);

    win32::WindowClassInfo info;
    info.wndProc       = mem.Read32(lpwc + 4);
    info.hIcon         = mem.Read32(lpwc + 20);
    info.hCursor       = mem.Read32(lpwc + 24);
    info.hbrBackground = mem.Read32(lpwc + 28);

    uint32_t classNamePtr = mem.Read32(lpwc + 36);
    std::string className = ReadWideCStringNarrow(mem, classNamePtr);

    HostCallOutcome result;
    result.returnValue = win32::g_windowManager.RegisterClass(className, info);
    return result;
}

// HWND CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
//                       DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
//                       HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
HostCallOutcome Impl_CreateWindowExW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t classNamePtr = ReadArg(cpu, mem, 1);
    uint32_t x      = ReadArg(cpu, mem, 4);
    uint32_t y      = ReadArg(cpu, mem, 5);
    uint32_t w      = ReadArg(cpu, mem, 6);
    uint32_t h      = ReadArg(cpu, mem, 7);
    uint32_t hInstance = ReadArg(cpu, mem, 10);

    std::string className = ReadWideCStringNarrow(mem, classNamePtr);

    // CW_USEDEFAULT = 0x80000000 — "пусть система выберет сама". У нас вся
    // "система" это один полноэкранный виртуальный рабочий стол 640x360
    constexpr uint32_t CW_USEDEFAULT = 0x80000000u;
    int32_t ix = (x == CW_USEDEFAULT) ? 0 : static_cast<int32_t>(x);
    int32_t iy = (y == CW_USEDEFAULT) ? 0 : static_cast<int32_t>(y);
    int32_t iw = (w == CW_USEDEFAULT) ? 640 : static_cast<int32_t>(w);
    int32_t ih = (h == CW_USEDEFAULT) ? 360  : static_cast<int32_t>(h);

    HostCallOutcome result;
    result.returnValue = win32::g_windowManager.CreateWindow(className, ix, iy, iw, ih, hInstance);

    // Реальная Windows автоматически закрашивает фон окна кистью
    // hbrBackground ПЕРЕД тем, как приложение рисует поверх — у нас такого
    // автоматического шага не было, и области, которые приложение не
    // перерисовывает явно само (рамка вокруг поля/счётчиков, зазоры между
    // клетками), оставались чёрными вместо классического Windows-серого.
    // Не резолвим точный hbrBackground — берём стандартный серый
    // (COLOR_BTNFACE/COLOR_3DFACE), это верно для подавляющего большинства
    // классических приложений, включая сам сапёр.
    gdi::g_gdi.GetOrCreateScreenDC(640, 360);
    gdi::g_gdi.FillScreenDC(0xFFC0C0C0u);

    return result;
}

// Общая логика загрузки меню по ID — переиспользуется и настоящим
// LoadMenuW (когда его честно вызывает интерпретируемый гостевой код),
// и нашей собственной, упрощённой версией LoadFrame внутри OnFileNew
// (см. Impl_Mfc4604_OnFileNew) — раньше там меню не загружалось вообще,
// раз мы обходим настоящий CFrameWnd::LoadFrame (ординал 4146,
// НЕ реализован честно) собственной последовательностью создания окон.
bool LoadMenuById(uint16_t menuId) {
    if (g_menuLoaded) return true; // уже загружено — не парсим повторно
    if (!g_imageBuffer || g_resourceDirRva == 0) return false;
    uint32_t dataRva, dataSize;
    if (pe::FindResourceGeneric(g_imageBuffer, g_imageBufferSize, g_resourceDirRva,
                                 4 /*RT_MENU*/, menuId, dataRva, dataSize)) {
        if (ParseMenuResource(g_imageBuffer + dataRva, dataSize, g_loadedMenu)) {
            g_menuLoaded = true;
            return true;
        }
    }
    return false;
}

// HMENU LoadMenuW(HINSTANCE hInstance, LPCWSTR lpMenuName) — настоящий
// парсинг MENUTEMPLATE из .rsrc (аналогично тому, что мы уже сделали для
// строк).
HostCallOutcome Impl_LoadMenuW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpMenuName = ReadArg(cpu, mem, 1);
    HostCallOutcome result;
    result.returnValue = 0xFFFC0001u;

    if (lpMenuName < 0x10000) {
        LoadMenuById(static_cast<uint16_t>(lpMenuName));
    }
    return result;
}

// HACCEL LoadAcceleratorsW(HINSTANCE, LPCWSTR) — таблица горячих клавиш,
// тот же принцип: реальный парсинг RT_ACCELERATOR отложен, фейковый хендл
// достаточен, чтобы код продолжил работу (используется в TranslateAccelerator
// внутри message loop, который мы ещё не реализовали)
HostCallOutcome Impl_LoadAcceleratorsW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0xFFFB0001u;
    return result;
}

// BOOL GetMenuItemRect(HWND hWnd, HMENU hMenu, UINT uItem, LPRECT lprc) —
// у нас нет настоящего рисуемого меню, но безопаснее вернуть правдоподобный
// прямоугольник и успех, чем провал (который мог бы завести игру в
// малопротестированную ветку обработки ошибок)
HostCallOutcome Impl_GetMenuItemRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lprc = ReadArg(cpu, mem, 3);
    if (lprc != 0) {
        mem.Write32(lprc + 0, 0);    // left
        mem.Write32(lprc + 4, 0);    // top
        mem.Write32(lprc + 8, 100);  // right
        mem.Write32(lprc + 12, 20);  // bottom
    }
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

// HRSRC FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType) —
// НАСТОЯЩИЙ поиск в дереве ресурсов (см. resource_parser.h), обобщённый
// вариант того, что мы уже делали для строк. Оба lpName/lpType у нас
// поддержаны только в виде числового ID (MAKEINTRESOURCE — старший word
// равен 0) — именованные ресурсы пока не резолвим, это отдельное расширение.
//
// "Хендл" HRSRC у нас — это просто RVA данных ресурса: раз ресурсы уже
// лежат в замапленном образе, отдельная таблица хендлов не нужна.
HostCallOutcome Impl_FindResourceW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpName = ReadArg(cpu, mem, 1);
    uint32_t lpType = ReadArg(cpu, mem, 2);

    HostCallOutcome result;
    if (lpName >= 0x10000 || lpType >= 0x10000 || !g_imageBuffer) {
        result.returnValue = 0; // именованные ресурсы/нет образа — не поддержано
        return result;
    }

    uint32_t dataRva = 0, dataSize = 0;
    bool found = pe::FindResourceGeneric(g_imageBuffer, g_imageBufferSize, g_resourceDirRva,
                                          static_cast<uint16_t>(lpType), static_cast<uint16_t>(lpName),
                                          dataRva, dataSize);
    result.returnValue = found ? dataRva : 0;
    return result;
}

// HGLOBAL LoadResource(HMODULE hModule, HRSRC hResInfo) — у нас хендл уже
// и есть RVA данных, поэтому просто прокидываем его дальше без изменений
HostCallOutcome Impl_LoadResource(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome result;
    result.returnValue = ReadArg(cpu, mem, 1);
    return result;
}

// LPVOID LockResource(HGLOBAL hResData) — переводим RVA в реальный
// абсолютный адрес внутри гостевой памяти, который код сможет разыменовать
HostCallOutcome Impl_LockResource(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t rva = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = (rva != 0) ? (g_imageBase + rva) : 0;
    return result;
}

// int SetDIBitsToDevice(HDC hdc, int xDest, int yDest, DWORD dwWidth, DWORD dwHeight,
//                        int xSrc, int ySrc, UINT uStartScan, UINT cScanLines,
//                        const VOID* lpvBits, const BITMAPINFO* lpbmi, UINT fuColorUse)
//
// Настоящий разбор формата DIB — не заглушка. Поддержаны самые
// распространённые для игр той эпохи глубины цвета: 1/4/8 бит (с палитрой)
// и 24 бита (без палитры). Важные детали формата, о которые легко
// споткнуться: строки хранятся СНИЗУ ВВЕРХ (если biHeight положительный) и
// выровнены по границе 4 байта — обе учтены. Сжатые DIB (RLE) и
// DIB_PAL_COLORS не поддержаны — встретим реально, тогда добавим.
HostCallOutcome Impl_SetDIBitsToDevice(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hdc       = ReadArg(cpu, mem, 0);
    int32_t xDest      = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t yDest      = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    uint32_t dwWidth   = ReadArg(cpu, mem, 3);
    uint32_t dwHeight  = ReadArg(cpu, mem, 4);
    int32_t xSrc       = static_cast<int32_t>(ReadArg(cpu, mem, 5));
    int32_t ySrc       = static_cast<int32_t>(ReadArg(cpu, mem, 6));
    uint32_t startScan = ReadArg(cpu, mem, 7);
    uint32_t scanLines = ReadArg(cpu, mem, 8);
    uint32_t lpvBits   = ReadArg(cpu, mem, 9);
    uint32_t lpbmi     = ReadArg(cpu, mem, 10);
    // arg 11 (fuColorUse) игнорируем — поддерживаем только DIB_RGB_COLORS(0)

    HostCallOutcome result;
    result.returnValue = 0; // как и настоящая функция при неудаче

    if (hdc == 0 || lpvBits == 0 || lpbmi == 0) return result;

    uint32_t biSize = mem.Read32(lpbmi + 0);
    int32_t biWidth = static_cast<int32_t>(mem.Read32(lpbmi + 4));
    int32_t biHeight = static_cast<int32_t>(mem.Read32(lpbmi + 8)); // знаковое!
    uint16_t biBitCount = mem.Read16(lpbmi + 14);
    uint32_t biCompression = mem.Read32(lpbmi + 16);
    uint32_t biClrUsed = mem.Read32(lpbmi + 32);

    if (biCompression != 0) return result; // RLE-сжатие не поддержано
    bool topDown = (biHeight < 0);
    int32_t absHeight = topDown ? -biHeight : biHeight;
    if (biWidth <= 0 || absHeight <= 0) return result;
    if (biBitCount != 1 && biBitCount != 4 && biBitCount != 8 && biBitCount != 24) {
        return result; // неподдержанная глубина цвета — узнаем, если реально встретится
    }

    uint32_t colorTableRva = lpbmi + biSize;
    uint32_t paletteCount = (biBitCount <= 8)
        ? ((biClrUsed != 0) ? biClrUsed : (1u << biBitCount))
        : 0;

    auto ReadPaletteColor = [&](uint32_t index) -> uint32_t {
        if (index >= paletteCount) return 0xFF000000u;
        uint32_t entryAddr = colorTableRva + index * 4; // RGBQUAD: B,G,R,reserved
        uint8_t b = mem.Read8(entryAddr + 0);
        uint8_t g = mem.Read8(entryAddr + 1);
        uint8_t r = mem.Read8(entryAddr + 2);
        return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
    };

    uint32_t rowBytes = ((static_cast<uint32_t>(biWidth) * biBitCount + 31) / 32) * 4;

    // ИСПРАВЛЕНИЕ: SetDIBitsToDevice ySrc — это нижний-левый угол исходного
    // прямоугольника. DIB хранится построчно. Если DIB bottom-up, мы должны
    // правильно сопоставить визуальные строки на экране со строками в памяти.
    for (uint32_t r = 0; r < dwHeight; r++) {
        int32_t dstY = yDest + static_cast<int32_t>(r);
        
        int32_t memoryRow;
        if (!topDown) { // Bottom-up DIB (стандартный случай)
            // ySrc=0 означает "отсчёт от низа".
            // Значит самая ВЕРХНЯЯ визуальная строка (r=0) прямоугольника высотой dwHeight
            // находится на высоте ySrc + dwHeight - 1 от низа DIB.
            int32_t srcY = ySrc + static_cast<int32_t>(dwHeight - 1 - r);
            memoryRow = srcY - static_cast<int32_t>(startScan);
        } else { // Top-down DIB
            int32_t srcY = ySrc + static_cast<int32_t>(r);
            memoryRow = srcY - static_cast<int32_t>(startScan);
        }

        if (memoryRow < 0 || memoryRow >= static_cast<int32_t>(scanLines)) continue;

        uint32_t rowAddr = lpvBits + static_cast<uint32_t>(memoryRow) * rowBytes;

        for (uint32_t c = 0; c < dwWidth; c++) {
            int32_t dstX = xDest + static_cast<int32_t>(c);
            int32_t srcX = xSrc + static_cast<int32_t>(c);
            
            if (srcX < 0 || srcX >= biWidth) continue;

            uint32_t colorArgb = 0xFF000000u;
            switch (biBitCount) {
                case 24: {
                    uint32_t pixAddr = rowAddr + static_cast<uint32_t>(srcX) * 3;
                    uint8_t b = mem.Read8(pixAddr + 0);
                    uint8_t g = mem.Read8(pixAddr + 1);
                    uint8_t r = mem.Read8(pixAddr + 2);
                    colorArgb = 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
                    break;
                }
                case 8: {
                    uint8_t idx = mem.Read8(rowAddr + static_cast<uint32_t>(srcX));
                    colorArgb = ReadPaletteColor(idx);
                    break;
                }
                case 4: {
                    uint8_t byteVal = mem.Read8(rowAddr + static_cast<uint32_t>(srcX) / 2);
                    uint8_t idx = (srcX % 2 == 0) ? (byteVal >> 4) : (byteVal & 0x0F);
                    colorArgb = ReadPaletteColor(idx);
                    break;
                }
                case 1: {
                    uint8_t byteVal = mem.Read8(rowAddr + static_cast<uint32_t>(srcX) / 8);
                    uint8_t bit = (byteVal >> (7 - (srcX % 8))) & 1;
                    colorArgb = ReadPaletteColor(bit);
                    break;
                }
            }
            
            uint32_t colorref = gdi::ArgbToColorRef(colorArgb);
            gdi::g_gdi.SetPixel(hdc, dstX, dstY, colorref);
        }
    }

    result.returnValue = scanLines; // успех — реальная функция возвращает число обработанных строк
    return result;
}

// DWORD CheckMenuItem(HMENU hMenu, UINT uIDCheckItem, UINT uCheck) — у нас
// нет настоящих пунктов меню с отслеживаемым состоянием, возвращаем "было
// не отмечено" (безопасный дефолт, реальная логика игры хранит состояние
// сложности в своих переменных, а не полагается на состояние самого меню)
HostCallOutcome Impl_CheckMenuItem(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0; // MF_UNCHECKED (предыдущее состояние)
    return result;
}

// BOOL SetMenu(HWND hWnd, HMENU hMenu) — у нас нет настоящего отображаемого
// меню, честный no-op с успехом
HostCallOutcome Impl_SetMenu(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

// BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) —
// реально обновляем геометрию окна в нашем WindowManager. bRepaint игнорируем —
// у нас пока нет диспетчера WM_PAINT, реальная перерисовка будет отдельным шагом.
HostCallOutcome Impl_MoveWindow(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    int32_t x = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t y = static_cast<int32_t>(ReadArg(cpu, mem, 2));
    int32_t w = static_cast<int32_t>(ReadArg(cpu, mem, 3));
    int32_t h = static_cast<int32_t>(ReadArg(cpu, mem, 4));

    HostCallOutcome result;
    result.returnValue = win32::g_windowManager.MoveWindow(hwnd, x, y, w, h) ? 1u : 0u;

    // Очищаем холст ТОЛЬКО если размер РЕАЛЬНО поменялся (смена сложности) —
    // не на каждый вызов MoveWindow. Некоторые переключатели (Метки/Цвет/
    // Звук) тоже дёргают MoveWindow с ТЕМИ ЖЕ размерами просто "на всякий
    // случай" — но полной перерисовки после них не происходит, и раньше
    // наша безусловная очистка оставляла экран чёрным именно в этих случаях.
    if (w != g_lastWindowW || h != g_lastWindowH) {
        gdi::g_gdi.FillScreenDC(0xFFC0C0C0u); // серым, как реальная Windows перед перерисовкой — не чёрным
    }
    // Запоминаем точные размеры — main.cpp использует их как ТОЧНУЮ область
    // контента вместо ненадёжного автоматического сканирования "не чёрных"
    // пикселей (которое давало неверные границы — отсюда и "стена" слева,
    // и недостающая серость по краям).
    g_lastWindowW = w;
    g_lastWindowH = h;

    return result;
}

// BOOL SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom) —
// просто заполняет структуру RECT (left,top,right,bottom — по 4 байта каждое)
HostCallOutcome Impl_SetRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lprc = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    if (lprc != 0) {
        mem.Write32(lprc + 0, ReadArg(cpu, mem, 1));  // left
        mem.Write32(lprc + 4, ReadArg(cpu, mem, 2));  // top
        mem.Write32(lprc + 8, ReadArg(cpu, mem, 3));  // right
        mem.Write32(lprc + 12, ReadArg(cpu, mem, 4)); // bottom
        result.returnValue = 1; // TRUE
    } else {
        result.returnValue = 0; // FALSE
    }
    return result;
}

// BOOL InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase) — теперь
// ПО-НАСТОЯЩЕМУ помечает окно как нуждающееся в перерисовке (см. message_loop.h) —
// GetMessage/PeekMessage синтезируют WM_PAINT, когда видят этот флаг.
// BOOL CopyRect(LPRECT lprcDst, const RECT* lprcSrc) — тривиальное
// копирование 16 байт (left/top/right/bottom).
// BOOL GetWindowRect(HWND hWnd, LPRECT lpRect) — честно берём геометрию из
// window_manager (там же, где CreateWindowExW её сохранил).
// BOOL IsWindow(HWND hWnd) — честно проверяем по window_manager.
HostCallOutcome Impl_IsWindow(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = (win32::g_windowManager.GetWindow(hwnd) != nullptr) ? 1u : 0u;
    return result;
}

HostCallOutcome Impl_GetWindowRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    uint32_t lpRect = ReadArg(cpu, mem, 1);
    HostCallOutcome result;
    const win32::WindowInfo* wi = win32::g_windowManager.GetWindow(hwnd);
    if (!wi || lpRect == 0) { result.returnValue = 0; return result; }
    mem.Write32(lpRect + 0, static_cast<uint32_t>(wi->x));
    mem.Write32(lpRect + 4, static_cast<uint32_t>(wi->y));
    mem.Write32(lpRect + 8, static_cast<uint32_t>(wi->x + wi->width));
    mem.Write32(lpRect + 12, static_cast<uint32_t>(wi->y + wi->height));
    result.returnValue = 1;
    return result;
}

HostCallOutcome Impl_CopyRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t dst = ReadArg(cpu, mem, 0);
    uint32_t src = ReadArg(cpu, mem, 1);
    HostCallOutcome result;
    if (dst == 0 || src == 0) { result.returnValue = 0; return result; }
    for (int i = 0; i < 4; i++) mem.Write32(dst + i * 4, mem.Read32(src + i * 4));
    result.returnValue = 1;
    return result;
}

HostCallOutcome Impl_InvalidateRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    win32::g_messageQueue.InvalidateWindow(hwnd);
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

// BOOL ShowWindow(HWND hWnd, int nCmdShow) — у нас нет отдельного состояния
// видимости окна (наш единственный экранный DC всегда "активен"), поэтому
// честно возвращаем успех — реальная видимость появится, когда подключим
// экранный DC к настоящему framebuffer Switch.
HostCallOutcome Impl_ShowWindow(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1; // TRUE (условно "было видимо и раньше")
    return result;
}

// Структура MSG (28 байт): hwnd@0, message@4, wParam@8, lParam@12, time@16,
// pt.x@20, pt.y@24
void WriteMsgStruct(cpu::IGuestMemory& mem, uint32_t lpMsg, const win32::QueuedMessage& msg) {
    if (lpMsg == 0) return;
    mem.Write32(lpMsg + 0, msg.hwnd);
    mem.Write32(lpMsg + 4, msg.message);
    mem.Write32(lpMsg + 8, msg.wParam);
    mem.Write32(lpMsg + 12, msg.lParam);
    mem.Write32(lpMsg + 16, 0); // time — у нас нет настоящих таймстемпов сообщений

    // pt.x/pt.y — координаты курсора В МОМЕНТ отправки сообщения. РАНЬШЕ
    // тут всегда был (0,0) — это и было настоящей причиной "смайлик не
    // отжимается": многие Win32-приложения (включая winmine) при отпускании
    // кнопки мыши повторно проверяют "а курсор всё ещё над кнопкой?" именно
    // через msg.pt, а НЕ через lParam текущего сообщения. Для мышиных
    // сообщений (0x200-0x209) lParam и pt кодируют ОДНИ И ТЕ ЖЕ координаты
    // (у нас single-window, client==screen), так что просто переиспользуем.
    if (msg.message >= win32::WM_MOUSEMOVE && msg.message <= win32::WM_RBUTTONUP) {
        mem.Write32(lpMsg + 20, msg.lParam & 0xFFFFu);         // pt.x = LOWORD(lParam)
        mem.Write32(lpMsg + 24, (msg.lParam >> 16) & 0xFFFFu); // pt.y = HIWORD(lParam)
    } else {
        mem.Write32(lpMsg + 20, 0);
        mem.Write32(lpMsg + 24, 0);
    }
}

// Общая логика получения следующего сообщения — используется и GetMessage,
// и PeekMessage. Приоритет (как в реальной Windows): WM_QUIT > очередь > WM_PAINT.
// filterMin/filterMax: 0,0 означает "любые сообщения" (так GetMessage почти
// всегда и вызывается) — иначе учитываем диапазон, как настоящий PeekMessage:
// если очередное сообщение НЕ подходит под фильтр, оно остаётся в очереди
// нетронутым (не теряется, не проглатывается), просто сейчас не возвращается.
bool FetchNextMessage(win32::QueuedMessage& out, uint32_t filterMin = 0, uint32_t filterMax = 0) {
    if (win32::g_messageQueue.QuitRequested()) {
        out = {0, win32::WM_QUIT, 0, 0};
        return true;
    }
    if (win32::g_messageQueue.PopFiltered(filterMin, filterMax, out)) {
        return true;
    }
    bool noFilter = (filterMin == 0 && filterMax == 0);
    uint32_t needsPaintHwnd = 0;
    if ((noFilter || (win32::WM_PAINT >= filterMin && win32::WM_PAINT <= filterMax))
        && win32::g_messageQueue.AnyWindowNeedsPaint(needsPaintHwnd)) {
        win32::g_messageQueue.ConsumeNeedsPaint(needsPaintHwnd); // иначе WM_PAINT синтезировался бы бесконечно
        out = {needsPaintHwnd, win32::WM_PAINT, 0, 0};
        return true;
    }
    if (noFilter || (win32::WM_TIMER >= filterMin && win32::WM_TIMER <= filterMax)) {
        uint32_t timerHwnd = 0, timerId = 0;
        uint32_t nowTick = g_getTickCountImpl ? g_getTickCountImpl() : 0;
        if (win32::g_timerRegistry.CheckDue(nowTick, timerHwnd, timerId)) {
            out = {timerHwnd, win32::WM_TIMER, timerId, 0};
            return true;
        }
    }
    return false;
}

// BOOL GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) —
// теперь ПО-НАСТОЯЩЕМУ блокируется (как и настоящая функция), опрашивая
// g_pumpHostFrame, пока не появится тач-событие или запрос на выход. Если
// колбэк не установлен (хостовые тесты, где реального железа нет) — ведёт
// себя как раньше: синтезирует WM_QUIT сразу, не блокируясь навечно.
HostCallOutcome Impl_GetMessage(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpMsg = ReadArg(cpu, mem, 0);

    win32::QueuedMessage msg;
    bool haveMsg = FetchNextMessage(msg);

    while (!haveMsg) {
        if (!g_pumpHostFrame) {
            // Нет реального железа (хостовый тест) — прежнее поведение
            msg = {0, win32::WM_QUIT, 0, 0};
            haveMsg = true;
            break;
        }

        HostFrameResult frame = g_pumpHostFrame();

        if (frame.shouldQuit) {
            win32::g_messageQueue.RequestQuit();
            msg = {0, win32::WM_QUIT, 0, 0};
            haveMsg = true;
            break;
        }

        if (frame.hasTouchEvent) {
            uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
            uint32_t wParam = (frame.message == win32::WM_LBUTTONDOWN) ? 1u : 0u; // MK_LBUTTON
            uint32_t lParam = ((frame.y & 0xFFFFu) << 16) | (frame.x & 0xFFFFu);
            msg = {hwnd, frame.message, wParam, lParam};
            haveMsg = true;
            break;
        }

        // ВАЖНО: g_pumpHostFrame мог не вернуть событие НАПРЯМУЮ, а вместо
        // этого положить сообщение(я) в очередь (так делает наш реальный
        // main.cpp для клика — кладёт сразу down+up). Без этой проверки
        // такие сообщения никогда бы не забрались из очереди.
        haveMsg = FetchNextMessage(msg);
    }

    WriteMsgStruct(mem, lpMsg, msg);

    HostCallOutcome result;
    result.returnValue = (msg.message == win32::WM_QUIT) ? 0u : 1u;
    return result;
}

// BOOL PeekMessage(LPMSG,HWND,UINT,UINT,UINT wRemoveMsg) — в отличие от
// GetMessage НЕ блокируется: опрашивает железо ОДИН раз (не в цикле) и сразу
// возвращает FALSE, если событий нет.
//
// ИСПРАВЛЕНО (эта сессия — НАЙДЕННЫЙ БАГ): раньше wRemoveMsg вообще не
// читался, сообщение ВСЕГДА реально удалялось из очереди. Реальный
// CWinThread::Run() в MFC вызывает PeekMessage(...,PM_NOREMOVE) в цикле
// проверки простоя (idle) ПЕРЕД настоящей обработкой — если это "съедало"
// сообщение, до настоящего GetMessage/DispatchMessage оно уже не
// долетало. Судя по всему, именно так пропадали клики мыши на paint.exe
// (сообщение реально попадало в очередь через Post, но никогда не
// доходило до wndProc) — этот баг мог не проявляться для более простых
// message-loop паттернов (например, у winmine.exe, если он не построен
// на MFC-подобном idle-цикле с PM_NOREMOVE).
HostCallOutcome Impl_PeekMessage(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpMsg = ReadArg(cpu, mem, 0);
    uint32_t filterMin = ReadArg(cpu, mem, 2);
    uint32_t filterMax = ReadArg(cpu, mem, 3);
    uint32_t wRemoveMsg = ReadArg(cpu, mem, 4);
    bool removeMsg = (wRemoveMsg & 0x1) != 0; // PM_REMOVE=1, PM_NOREMOVE=0

    win32::QueuedMessage msg;
    bool have = removeMsg
        ? FetchNextMessage(msg, filterMin, filterMax)
        : win32::g_messageQueue.PeekFiltered(filterMin, filterMax, msg);

    if (!have && g_pumpHostFrame) {
        HostFrameResult frame = g_pumpHostFrame();
        if (frame.shouldQuit) {
            win32::g_messageQueue.RequestQuit();
            msg = {0, win32::WM_QUIT, 0, 0};
            have = true;
        } else if (frame.hasTouchEvent) {
            uint32_t hwnd = win32::g_windowManager.GetLastCreatedWindow();
            uint32_t wParam = (frame.message == win32::WM_LBUTTONDOWN) ? 1u : 0u;
            uint32_t lParam = ((frame.y & 0xFFFFu) << 16) | (frame.x & 0xFFFFu);
            msg = {hwnd, frame.message, wParam, lParam};
            have = true;
        } else {
            // Как и в GetMessage — колбэк мог положить сообщение в очередь
            // напрямую, а не вернуть его сразу
            have = removeMsg
                ? FetchNextMessage(msg, filterMin, filterMax)
                : win32::g_messageQueue.PeekFiltered(filterMin, filterMax, msg);
        }
    }

    if (have) {
        WriteMsgStruct(mem, lpMsg, msg);
    }

    HostCallOutcome result;
    result.returnValue = have ? 1u : 0u;
    return result;
}

// BOOL TranslateMessage(const MSG*) — переводит WM_KEYDOWN/UP в WM_CHAR.
// У нас пока нет настоящего клавиатурного ввода — честный no-op.
HostCallOutcome Impl_TranslateMessage(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// LRESULT DispatchMessage(const MSG* lpMsg) — САМОЕ ГЛАВНОЕ: реально находит
// WndProc окна и по-настоящему его вызывает через CallGuestFunctionStdcall.
HostCallOutcome Impl_DispatchMessage(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpMsg = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = 0;
    if (lpMsg == 0) return result;

    uint32_t hwnd = mem.Read32(lpMsg + 0);
    uint32_t message = mem.Read32(lpMsg + 4);
    uint32_t wParam = mem.Read32(lpMsg + 8);
    uint32_t lParam = mem.Read32(lpMsg + 12);

    const win32::WindowClassInfo* cls = win32::g_windowManager.GetWindowClass(hwnd);
    if (!cls || cls->wndProc == 0) {
        return result; // нет окна/процедуры — аналог DefWindowProc с нулевым результатом
    }

    uint32_t lresult = win32::CallGuestFunctionStdcall(
        cpu, mem, cls->wndProc, {hwnd, message, wParam, lParam}, g_tebAddr, g_tebAddr);
    result.returnValue = lresult;
    return result;
}

// void PostQuitMessage(int nExitCode) — просит завершить message loop
HostCallOutcome Impl_PostQuitMessage(cpu::CpuState&, cpu::IGuestMemory&) {
    win32::g_messageQueue.RequestQuit();
    return HostCallOutcome{};
}

// LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM) — обработка "по умолчанию"
// для сообщений, которые WndProc сам не обработал. Настоящая DefWindowProc
// делает много всего по мелочи (эрозия фона, системное меню и т.д.) — мы
// пока честно ничего не делаем (безопасный no-op), это WndProc'ы обычно
// вызывают в конце своей обработки, если сообщение не их профиля.
HostCallOutcome Impl_DefWindowProcW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// int TranslateAcceleratorW(HWND hWnd, HACCEL hAccTable, LPMSG lpMsg) —
// проверяет, не является ли сообщение "горячей клавишей" из таблицы
// акселераторов (Ctrl+N и т.п.), и если да — сама диспетчеризует его,
// возвращая ненулевое значение (сигнал вызывающему коду: "уже обработано,
// не надо TranslateMessage/DispatchMessage для этого сообщения"). У нас нет
// настоящей таблицы акселераторов (LoadAcceleratorsW — фейковый хендл),
// поэтому честно говорим "это не акселератор" (0) — вызывающий код тогда
// сам передаст сообщение в обычный TranslateMessage/DispatchMessage.
HostCallOutcome Impl_TranslateAcceleratorW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// BOOL PtInRect(const RECT* lprc, POINT pt) — ВАЖНЫЙ нюанс: POINT передаётся
// ПО ЗНАЧЕНИЮ (не указателем!), то есть как два отдельных DWORD на стеке
// (x, затем y) — реальный контракт stdcall для мелких структур-по-значению.
// right/bottom в Win32 RECT — ИСКЛЮЧАЮЩИЕ границы (точка на них уже "снаружи").
HostCallOutcome Impl_PtInRect(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lprc = ReadArg(cpu, mem, 0);
    int32_t ptX = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    int32_t ptY = static_cast<int32_t>(ReadArg(cpu, mem, 2));

    HostCallOutcome result;
    if (lprc == 0) {
        result.returnValue = 0;
        return result;
    }

    int32_t left = static_cast<int32_t>(mem.Read32(lprc + 0));
    int32_t top = static_cast<int32_t>(mem.Read32(lprc + 4));
    int32_t right = static_cast<int32_t>(mem.Read32(lprc + 8));
    int32_t bottom = static_cast<int32_t>(mem.Read32(lprc + 12));

    bool inside = (ptX >= left && ptX < right && ptY >= top && ptY < bottom);
    result.returnValue = inside ? 1u : 0u;
    return result;
}

// int MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoints, UINT cPoints) —
// переводит координаты точек из системы координат одного окна в другую.
// У нас все окна используют ОДНО и то же начало координат (0,0) — реальных
// смещений между окнами нет, поэтому честно ничего не меняем в точках (они
// уже в правильной системе координат), возвращаем 0 (нулевое смещение).
HostCallOutcome Impl_MapWindowPoints(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0;
    return result;
}

// --- Оставшиеся нереализованные импорты (превентивно, до победного краша) ---
// Большинство — безопасные заглушки; SendMessageW/PostMessageW реализованы
// по-настоящему, раз у нас уже есть вся инфраструктура для этого.

// DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
HostCallOutcome Impl_GetModuleFileNameA(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t buf = ReadArg(cpu, mem, 1);
    uint32_t size = ReadArg(cpu, mem, 2);
    const char* name = "winmine.exe";
    uint32_t len = 0;
    if (buf != 0) {
        while (name[len] && len + 1 < size) { mem.Write8(buf + len, static_cast<uint8_t>(name[len])); len++; }
        mem.Write8(buf + len, 0);
    }
    HostCallOutcome result;
    result.returnValue = len;
    return result;
}

// LPWSTR lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2) — настоящая копия,
// простая и безопасная реализация
HostCallOutcome Impl_lstrcpyW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t dst = ReadArg(cpu, mem, 0);
    uint32_t src = ReadArg(cpu, mem, 1);
    if (dst != 0 && src != 0) {
        for (uint32_t i = 0; i < 1024; i++) {
            uint16_t ch = mem.Read16(src + i * 2);
            mem.Write16(dst + i * 2, ch);
            if (ch == 0) break;
        }
    }
    HostCallOutcome result;
    result.returnValue = dst;
    return result;
}

// HMODULE LoadLibraryA(LPCSTR lpLibFileName) — у нас нет настоящей загрузки
// DLL, но возвращаем правдоподобный ненулевой хендл, чтобы вызывающий код
// не считал вызов проваленным
HostCallOutcome Impl_LoadLibraryA(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 0x69000000u; // произвольный, но правдоподобный "хендл модуля"
    return result;
}

// int MessageBoxW(HWND,LPCWSTR,LPCWSTR,UINT) — у нас нет настоящего модального
// диалога — честно возвращаем IDOK (1), будто пользователь сразу нажал "ОК"
HostCallOutcome Impl_MessageBoxW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1; // IDOK
    return result;
}

// UINT GetDlgItemInt(HWND,int,BOOL*,BOOL) — нет реальных диалоговых контролов
// UINT GetDlgItemInt(HWND hDlg, int nIDDlgItem, BOOL *lpTranslated, BOOL bSigned) —
// теперь реально читает то, что пользователь ввёл через клавиатуру (см.
// main.cpp — она кладёт текст в ту же g_dialogItemText, что и SetDlgItemTextW)
// и парсит как число. Раньше ВСЕГДА возвращала 0/"не удалось перевести" —
// именно поэтому "Особые..." принимала введённые числа визуально, но не
// применяла их к игре: диалоговая процедура читает поля именно этой
// функцией (она для числовых полей стандартна в Win32), не GetDlgItemTextW.
HostCallOutcome Impl_GetDlgItemInt(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t id = ReadArg(cpu, mem, 1);
    uint32_t lpTranslated = ReadArg(cpu, mem, 2);

    HostCallOutcome result;
    result.returnValue = 0;

    auto it = g_dialogItemText.find(static_cast<uint16_t>(id));
    if (it == g_dialogItemText.end() || it->second.empty()) {
        if (lpTranslated != 0) mem.Write32(lpTranslated, 0); // FALSE — поле пустое/не найдено
        return result;
    }

    // UTF-16 -> узкая строка (числа состоят только из ASCII-цифр, этого достаточно)
    std::string narrow;
    for (uint16_t ch : it->second) {
        if (ch >= '0' && ch <= '9') narrow += static_cast<char>(ch);
        else if (ch == '-' && narrow.empty()) narrow += '-'; // знак минуса — только в начале
    }

    if (narrow.empty() || narrow == "-") {
        if (lpTranslated != 0) mem.Write32(lpTranslated, 0);
        return result;
    }

    long value = std::strtol(narrow.c_str(), nullptr, 10);
    if (lpTranslated != 0) mem.Write32(lpTranslated, 1); // TRUE — успешно перевели
    result.returnValue = static_cast<uint32_t>(value);
    return result;
}

// BOOL WinHelpW(...) — справки у нас нет, честно "успех" без реального действия
HostCallOutcome Impl_WinHelpW(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// BOOL SetDlgItemInt(HWND,int,UINT,BOOL)
HostCallOutcome Impl_SetDlgItemInt(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// BOOL EndDialog(HWND hDlg, INT_PTR nResult) — теперь по-настоящему сигналит
// модальному циклу DialogBoxParamW завершиться (см. ниже)
HostCallOutcome Impl_EndDialog(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    g_dialogEndRequested = true;
    g_dialogEndResult = static_cast<int32_t>(ReadArg(cpu, mem, 1));
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// BOOL SetDlgItemTextW(HWND,int,LPCWSTR) — теперь реально запоминает текст,
// который main.cpp использует при отрисовке ВМЕСТО пустого текста из
// ресурса (так игра показывает реальные имена/время чемпионов)
HostCallOutcome Impl_SetDlgItemTextW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t id = ReadArg(cpu, mem, 1);
    uint32_t strPtr = ReadArg(cpu, mem, 2);
    std::vector<uint16_t> text;
    for (uint32_t i = 0; i < 256; i++) {
        uint16_t ch = mem.Read16(strPtr + i * 2);
        if (ch == 0) break;
        text.push_back(ch);
    }
    g_dialogItemText[static_cast<uint16_t>(id)] = std::move(text);
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// int wsprintfW(LPWSTR, LPCWSTR fmt, ...) — cdecl, переменное число аргументов.
// Реализуем базовые спецификаторы (%d %u %s %c %%) — этого достаточно для
// типичных строк вида "Вы прошли за %d секунд". Не поддерживаемые
// спецификаторы копируются как есть (безопасно, просто не отформатируется).
HostCallOutcome Impl_wsprintfW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t dst = ReadArg(cpu, mem, 0);
    uint32_t fmt = ReadArg(cpu, mem, 1);
    int argIndex = 2; // следующие cdecl-аргументы на стеке, по порядку

    uint32_t outPos = 0;
    for (uint32_t i = 0; i < 512; i++) {
        uint16_t ch = mem.Read16(fmt + i * 2);
        if (ch == 0) break;

        if (ch == '%') {
            uint16_t spec = mem.Read16(fmt + (i + 1) * 2);
            if (spec == 'd' || spec == 'u' || spec == 'i') {
                uint32_t val = ReadArg(cpu, mem, argIndex++);
                char buf[16];
                std::snprintf(buf, sizeof(buf), (spec == 'd' || spec == 'i') ? "%d" : "%u",
                              (spec == 'd' || spec == 'i') ? static_cast<int32_t>(val) : val);
                for (char* p = buf; *p; p++) mem.Write16(dst + outPos++ * 2, static_cast<uint16_t>(*p));
                i++; // пропускаем спецификатор
                continue;
            } else if (spec == 's') {
                uint32_t strPtr = ReadArg(cpu, mem, argIndex++);
                for (uint32_t j = 0; j < 256; j++) {
                    uint16_t c = mem.Read16(strPtr + j * 2);
                    if (c == 0) break;
                    mem.Write16(dst + outPos++ * 2, c);
                }
                i++;
                continue;
            } else if (spec == 'c') {
                uint32_t val = ReadArg(cpu, mem, argIndex++);
                mem.Write16(dst + outPos++ * 2, static_cast<uint16_t>(val));
                i++;
                continue;
            } else if (spec == '%') {
                mem.Write16(dst + outPos++ * 2, '%');
                i++;
                continue;
            }
        }
        mem.Write16(dst + outPos++ * 2, ch);
    }
    mem.Write16(dst + outPos * 2, 0);

    HostCallOutcome result;
    result.returnValue = outPos;
    return result;
}

// LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) —
// синхронная отправка: в отличие от PostMessage, реально вызывает WndProc
// НАПРЯМУЮ и ждёт результата — у нас уже есть вся инфраструктура для этого
// (та же, что использует DispatchMessage), просто переиспользуем её.
HostCallOutcome Impl_SendMessageW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    uint32_t message = ReadArg(cpu, mem, 1);
    uint32_t wParam = ReadArg(cpu, mem, 2);
    uint32_t lParam = ReadArg(cpu, mem, 3);

    HostCallOutcome result;
    result.returnValue = 0;

    const win32::WindowClassInfo* cls = win32::g_windowManager.GetWindowClass(hwnd);
    if (!cls || cls->wndProc == 0) return result;

    result.returnValue = win32::CallGuestFunctionStdcall(
        cpu, mem, cls->wndProc, {hwnd, message, wParam, lParam}, g_tebAddr, g_tebAddr);
    return result;
}

// HWND GetDlgItem(HWND hDlg, int nIDDlgItem) — фейковый, но стабильный
// хендл "дочернего контрола" (у нас нет настоящих диалоговых контролов)
HostCallOutcome Impl_GetDlgItem(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t id = ReadArg(cpu, mem, 1);
    HostCallOutcome result;
    result.returnValue = 0xD1600000u | (id & 0xFFFFu); // предсказуемый фейковый хендл
    return result;
}

// UINT GetDlgItemTextW(HWND,int,LPWSTR,int) — возвращает реально сохранённый
// текст (через SetDlgItemTextW), если есть; иначе честно пустую строку
HostCallOutcome Impl_GetDlgItemTextW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t id = ReadArg(cpu, mem, 1);
    uint32_t buf = ReadArg(cpu, mem, 2);
    uint32_t maxLen = ReadArg(cpu, mem, 3);
    HostCallOutcome result;
    result.returnValue = 0;
    if (buf == 0) return result;

    auto it = g_dialogItemText.find(static_cast<uint16_t>(id));
    if (it == g_dialogItemText.end()) {
        mem.Write16(buf, 0);
        return result;
    }
    uint32_t len = 0;
    for (; len < it->second.size() && len + 1 < maxLen; len++) {
        mem.Write16(buf + len * 2, it->second[len]);
    }
    mem.Write16(buf + len * 2, 0);
    result.returnValue = len;
    return result;
}

// INT_PTR DialogBoxParamW(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM) — самая
// рискованная из оставшихся: у нас нет настоящего модального диалога
// (собственного message loop для диалоговых окон). Честно и безопасно
// "закрываем" его сразу же, как будто пользователь нажал Cancel — это
// избавляет от риска зависания/краха, ценой того что диалог (например,
// "новый рекорд, введите имя") визуально не появится.
// INT_PTR DialogBoxParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName,
//                          HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
//
// Теперь по-настоящему: находим и разбираем ресурс DIALOG, вызываем DlgProc
// с WM_INITDIALOG (даём игре заполнить поля реальными данными через
// SetDlgItemTextW), затем крутим свой мини message loop — так же, как
// CallGuestFunctionStdcall крутит вложенный интерпретатор для WndProc — пока
// EndDialog не попросит закрыться. main.cpp рисует диалог сам (как и меню)
// и кладёт WM_COMMAND в очередь при клике по кнопкам.
HostCallOutcome Impl_DialogBoxParamW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t lpTemplateName = ReadArg(cpu, mem, 1);
    uint32_t dlgProc = ReadArg(cpu, mem, 3);
    uint32_t initParam = ReadArg(cpu, mem, 4);

    HostCallOutcome result;
    result.returnValue = static_cast<uint32_t>(-1); // по умолчанию — "не удалось"

    if (lpTemplateName >= 0x10000 || !g_imageBuffer || g_resourceDirRva == 0 || dlgProc == 0) {
        return result; // не похоже на числовой ID ресурса — не поддерживаем
    }

    uint32_t dataRva, dataSize;
    if (!pe::FindResourceGeneric(g_imageBuffer, g_imageBufferSize, g_resourceDirRva,
                                  5 /*RT_DIALOG*/, static_cast<uint16_t>(lpTemplateName), dataRva, dataSize)) {
        return result;
    }

    win32::ParsedDialog dlg;
    if (!ParseDialogResource(g_imageBuffer + dataRva, dataSize, dlg)) {
        return result; // например, DIALOGEX — не поддерживаем формат
    }

    g_currentDialog = dlg;
    g_dialogHwnd = 0xD1A00001u;
    g_dialogActive = true;
    g_dialogItemText.clear();
    g_dialogEndRequested = false;
    g_dialogEndResult = 0;

    // WM_INITDIALOG — даём диалоговой процедуре шанс заполнить поля
    CallGuestFunctionStdcall(cpu, mem, dlgProc, {g_dialogHwnd, win32::WM_INITDIALOG, 0, initParam},
                              g_tebAddr, g_tebAddr);

    // Модальный цикл: крутим, пока DlgProc не вызовет EndDialog (main.cpp
    // кладёт WM_COMMAND в общую очередь при клике по кнопкам диалога —
    // забираем и передаём напрямую в DlgProc, как и положено диалогу)
    int guard = 0;
    while (!g_dialogEndRequested && guard < 2000000) {
        guard++;
        if (g_pumpHostFrame) {
            HostFrameResult frame = g_pumpHostFrame();
            if (frame.shouldQuit) break; // пользователь нажал + — выходим и из диалога тоже
        }
        win32::QueuedMessage msg;
        if (win32::g_messageQueue.Pop(msg)) {
            CallGuestFunctionStdcall(cpu, mem, dlgProc, {msg.hwnd, msg.message, msg.wParam, msg.lParam},
                                      g_tebAddr, g_tebAddr);
        }
    }

    g_dialogActive = false;
    result.returnValue = static_cast<uint32_t>(g_dialogEndResult);
    return result;
}

// BOOL PostMessageW(HWND,UINT,WPARAM,LPARAM) — у нас есть настоящая очередь
// сообщений, реализуем по-настоящему
HostCallOutcome Impl_PostMessageW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    uint32_t message = ReadArg(cpu, mem, 1);
    uint32_t wParam = ReadArg(cpu, mem, 2);
    uint32_t lParam = ReadArg(cpu, mem, 3);
    win32::g_messageQueue.Post(hwnd, message, wParam, lParam);
    HostCallOutcome result;
    result.returnValue = 1;
    return result;
}

// BOOL ShellAboutW(HWND,LPCWSTR,LPCWSTR,HICON) — окна "О программе" у нас
// нет — честный успех без реального действия
// int ShellAboutW(HWND hWnd, LPCWSTR szApp, LPCWSTR szOtherStuff, HICON hIcon) —
// в отличие от DialogBoxParamW, тут НЕТ шаблона ресурса — весь текст
// приходит прямо аргументами (имя приложения + многострочное описание).
// Разбиваем многострочный текст на отдельные строки (по \r\n или \n) и
// крутим свой мини modal loop, как и с DialogBoxParamW — пока main.cpp не
// положит сигнал "нажали OK" в очередь.
// РАБОЧАЯ ГИПОТЕЗА (подтверждена структурно): MFC42u.DLL ординал 1569
// вызывается ровно на месте WinMain (сигнатура аргументов совпадает, а
// возврат уходит напрямую в exit() — классический эпилог CRT-стартапа).
//
// Теперь пробуем настоящую логику AfxWinMain вместо простой заглушки:
// найти theApp (захвачен ДИНАМИЧЕСКИ через ординал 561, см. g_theAppPtr —
// НЕ хардкодим адрес под конкретную сборку), прочитать его vtable-указатель
// ПРЯМО ИЗ ОБЪЕКТА (тоже не хардкод), и вызвать InitInstance() по смещению
// 0x58 в этой vtable — офсет подтверждён дизассемблированием (это
// единственный "нестандартный" слот, чьё тело содержит SetRegistryKey с
// UTF-16 строкой "Paint" — типичный первый вызов настоящего InitInstance).
//
// Если гипотеза почему-то не сработает (theApp не захвачен, vtable/метод
// нулевые) — безопасно откатываемся к старому поведению (просто TRUE),
// ничего не падает.
HostCallOutcome Impl_Probe_Ordinal1569(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    HostCallOutcome result;
    result.returnValue = 1;

    if (g_theAppPtr == 0) {
        if (cpu::g_consoleAvailable) {
            std::printf("[AfxWinMain] theApp не захвачен (ординал 561 ещё не вызывался?) — используем заглушку\n");
        }
        return result;
    }

    uint32_t vtable = mem.Read32(g_theAppPtr);
    if (vtable == 0) {
        if (cpu::g_consoleAvailable) std::printf("[AfxWinMain] theApp=0x%x, но vtable пуста\n", g_theAppPtr);
        return result;
    }

    constexpr uint32_t INIT_INSTANCE_VTABLE_OFFSET = 0x58; // подтверждено дизассемблированием (SetRegistryKey("Paint"))
    uint32_t initInstanceAddr = mem.Read32(vtable + INIT_INSTANCE_VTABLE_OFFSET);
    if (initInstanceAddr == 0) {
        if (cpu::g_consoleAvailable) std::printf("[AfxWinMain] InitInstance-слот пуст (vtable=0x%x)\n", vtable);
        return result;
    }

    if (cpu::g_consoleAvailable) {
        std::printf("[AfxWinMain] Вызываем InitInstance() по 0x%x (theApp=0x%x, vtable=0x%x)\n",
                     initInstanceAddr, g_theAppPtr, vtable);
    }

    cpu.Get(cpu::Reg32::ECX) = g_theAppPtr; // "this" для __thiscall
    uint32_t initResult = win32::CallGuestFunctionStdcall(cpu, mem, initInstanceAddr, {}, g_tebAddr, g_tebAddr);

    if (cpu::g_consoleAvailable) {
        std::printf("[AfxWinMain] InitInstance() вернул %u\n", initResult);
    }

    if (initResult == 0) {
        result.returnValue = 0;
        return result;
    }

    // InitInstance() успешна — теперь вызываем Run() (смещение 0x5C в той
    // же vtable, сразу после InitInstance на 0x58 — подтверждено
    // дизассемблированием: это прямой thunk на MFC42u.DLL ординал 5710, то
    // есть приложение НЕ переопределяет Run(), используется базовая
    // реализация из самой MFC — значит, её тоже нужно реализовать самим
    // (см. Impl_Mfc5710_Run), как и с AfxWinMain/InitInstance.
    constexpr uint32_t RUN_VTABLE_OFFSET = 0x5C;
    uint32_t runAddr = mem.Read32(vtable + RUN_VTABLE_OFFSET);
    if (runAddr != 0) {
        if (cpu::g_consoleAvailable) {
            std::printf("[AfxWinMain] Вызываем Run() по 0x%x\n", runAddr);
        }
        cpu.Get(cpu::Reg32::ECX) = g_theAppPtr;
        uint32_t runResult = win32::CallGuestFunctionStdcall(cpu, mem, runAddr, {}, g_tebAddr, g_tebAddr);
        if (cpu::g_consoleAvailable) {
            std::printf("[AfxWinMain] Run() вернул %u\n", runResult);
        }
        result.returnValue = runResult;
        return result;
    }

    result.returnValue = initResult ? 1u : 0u;
    return result;
}

HostCallOutcome Impl_ShellAboutW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t szApp = ReadArg(cpu, mem, 1);
    uint32_t szOtherStuff = ReadArg(cpu, mem, 2);

    HostCallOutcome result;
    result.returnValue = 1;

    g_aboutBoxTitle.clear();
    if (szApp != 0) {
        for (uint32_t i = 0; i < 128; i++) {
            uint16_t ch = mem.Read16(szApp + i * 2);
            if (ch == 0) break;
            g_aboutBoxTitle.push_back(ch);
        }
    }

    g_aboutBoxLines.clear();
    if (szOtherStuff != 0) {
        std::vector<uint16_t> line;
        for (uint32_t i = 0; i < 2048; i++) {
            uint16_t ch = mem.Read16(szOtherStuff + i * 2);
            if (ch == 0) break;
            if (ch == '\r') continue; // просто пропускаем, \n ниже закроет строку
            if (ch == '\n') {
                g_aboutBoxLines.push_back(std::move(line));
                line.clear();
                continue;
            }
            line.push_back(ch);
        }
        if (!line.empty()) g_aboutBoxLines.push_back(std::move(line));
    }

    g_aboutBoxHwnd = 0xAB0B0001u;
    g_aboutBoxActive = true;
    g_aboutBoxCloseRequested = false;

    int guard = 0;
    while (!g_aboutBoxCloseRequested && guard < 2000000) {
        guard++;
        if (g_pumpHostFrame) {
            HostFrameResult frame = g_pumpHostFrame();
            if (frame.shouldQuit) break;
        }
        // Просто ждём — main.cpp сам меняет g_aboutBoxCloseRequested при
        // клике по кнопке OK (это НЕ настоящий DlgProc гостевого кода —
        // ShellAboutW сама по себе блокирующая функция без DlgProc)
    }

    g_aboutBoxActive = false;
    return result;
}

// Захват мыши — простая переменная-бухгалтерия. У нас всегда одно окно, и
// все события мыши и так адресуются ровно в него (см. GetLastCreatedWindow),
// так что реального "перенаправления" делать не нужно — но контракт
// (вернуть ПРЕДЫДУЩЕЕ захватившее окно) соблюдаем честно.
uint32_t g_captureWindow = 0;

// HWND SetCapture(HWND hWnd)
HostCallOutcome Impl_SetCapture(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = g_captureWindow; // предыдущее захватившее окно
    g_captureWindow = hwnd;
    return result;
}

// BOOL ReleaseCapture(void)
HostCallOutcome Impl_ReleaseCapture(cpu::CpuState&, cpu::IGuestMemory&) {
    g_captureWindow = 0;
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

// UINT_PTR SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) —
// теперь РЕАЛЬНО регистрирует таймер (было заглушкой, из-за чего секундомер
// в сапёре замирал на первой секунде). Периодическая проверка "не пора ли
// сработать" — в FetchNextMessage ниже, на реальном времени через
// g_getTickCountImpl. lpTimerFunc (колбэк вместо WM_TIMER) пока не
// поддерживаем — winmine использует обычный WM_TIMER, не колбэк.
HostCallOutcome Impl_SetTimer(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    uint32_t nIDEvent = ReadArg(cpu, mem, 1);
    uint32_t uElapse = ReadArg(cpu, mem, 2);
    win32::g_timerRegistry.SetTimer(hwnd, nIDEvent, uElapse, g_getTickCountImpl ? g_getTickCountImpl() : 0);
    HostCallOutcome result;
    result.returnValue = (nIDEvent != 0) ? nIDEvent : 1u;
    return result;
}

// BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent) — теперь реально снимает
// таймер с учёта, чтобы он больше не тикал.
HostCallOutcome Impl_KillTimer(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    uint32_t uIDEvent = ReadArg(cpu, mem, 1);
    win32::g_timerRegistry.KillTimer(hwnd, uIDEvent);
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

// BOOL PlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound) — теперь
// РЕАЛЬНОЕ воспроизведение. winmine.exe хранит звуки как встроенные
// ресурсы под нестандартным строковым типом "WAVE" (не RT_RCDATA!) —
// нашли это, перечислив реальное дерево .rsrc. Сама выдача звука в
// динамики — платформенно-специфичная вещь (audout на Switch), поэтому
// тут только разбор WAV и конвертация; реальный вывод — через колбэк
// g_playAudioImpl, который выставляет main.cpp (как и с g_pumpHostFrame).
namespace {

constexpr uint32_t SND_RESOURCE_ = 0x00040000u;

// Простейший разбор RIFF/WAVE: находим чанки "fmt " и "data", извлекаем
// частоту дискретизации/каналы/битность и сами PCM-сэмплы.
bool ParseWav(const uint8_t* data, uint32_t size,
              uint16_t& outChannels, uint32_t& outSampleRate, uint16_t& outBitsPerSample,
              const uint8_t*& outPcmData, uint32_t& outPcmSize) {
    if (size < 12) return false;
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return false;

    uint32_t pos = 12;
    bool haveFmt = false, haveData = false;
    while (pos + 8 <= size) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, data + pos, 4);
        uint32_t chunkSize;
        std::memcpy(&chunkSize, data + pos + 4, 4);
        uint32_t chunkDataPos = pos + 8;
        if (chunkDataPos + chunkSize > size) break;

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::memcpy(&outChannels, data + chunkDataPos + 2, 2);
            std::memcpy(&outSampleRate, data + chunkDataPos + 4, 4);
            std::memcpy(&outBitsPerSample, data + chunkDataPos + 14, 2);
            haveFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            outPcmData = data + chunkDataPos;
            outPcmSize = chunkSize;
            haveData = true;
        }
        pos = chunkDataPos + chunkSize + (chunkSize & 1); // чанки выровнены по 2 байта
    }
    return haveFmt && haveData;
}

// Конвертирует произвольный PCM (8/16 бит, моно/стерео, любая частота) в
// формат, который ожидает наш audout-колбэк: 48000 Гц, стерео, 16 бит,
// перемежающиеся сэмплы. Ресемплинг — простой (без интерполяции, ближайший
// сосед) — для коротких игровых звуков этого достаточно.
std::vector<int16_t> ConvertToOutputFormat(const uint8_t* pcm, uint32_t pcmSize,
                                            uint16_t channels, uint32_t sampleRate, uint16_t bitsPerSample) {
    std::vector<int16_t> mono; // сначала приводим к моно int16, потом ресемплим и дублируем в стерео

    if (bitsPerSample == 8) {
        uint32_t count = pcmSize / channels;
        mono.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            int32_t sum = 0;
            for (uint16_t c = 0; c < channels; c++) sum += static_cast<int32_t>(pcm[i * channels + c]) - 128;
            mono.push_back(static_cast<int16_t>((sum / channels) * 256));
        }
    } else if (bitsPerSample == 16) {
        uint32_t count = pcmSize / (2 * channels);
        mono.reserve(count);
        const int16_t* samples = reinterpret_cast<const int16_t*>(pcm);
        for (uint32_t i = 0; i < count; i++) {
            int32_t sum = 0;
            for (uint16_t c = 0; c < channels; c++) sum += samples[i * channels + c];
            mono.push_back(static_cast<int16_t>(sum / channels));
        }
    } else {
        return {}; // экзотичная битность — пропускаем, не крашимся
    }

    constexpr uint32_t OUT_RATE = 48000;
    if (sampleRate == 0) return {};
    uint32_t outCount = static_cast<uint32_t>(static_cast<uint64_t>(mono.size()) * OUT_RATE / sampleRate);

    std::vector<int16_t> stereo;
    stereo.reserve(static_cast<size_t>(outCount) * 2);
    for (uint32_t i = 0; i < outCount; i++) {
        uint32_t srcIdx = static_cast<uint32_t>(static_cast<uint64_t>(i) * sampleRate / OUT_RATE);
        if (srcIdx >= mono.size()) srcIdx = static_cast<uint32_t>(mono.size()) - 1;
        int16_t s = mono[srcIdx];
        stereo.push_back(s);
        stereo.push_back(s);
    }
    return stereo;
}

} // namespace

HostCallOutcome Impl_PlaySoundW(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t pszSound = ReadArg(cpu, mem, 0);
    uint32_t fdwSound = ReadArg(cpu, mem, 2);

    HostCallOutcome result;
    result.returnValue = 1; // TRUE — по умолчанию, даже если реально ничего не сыграли

    // pszSound == 0 значит "остановить текущий звук" — у нас нет реального
    // проигрывания в фоне (звуки короткие, fire-and-forget), просто ОК
    if (pszSound == 0) return result;

    // Ресурс распознаём по MAKEINTRESOURCE-паттерну (значение < 0x10000,
    // выглядит как число, а не как указатель на строку) ИЛИ по явному флагу
    bool isResource = (fdwSound & SND_RESOURCE_) || (pszSound < 0x10000);
    if (!isResource || !g_imageBuffer || g_resourceDirRva == 0) return result;

    uint32_t dataRva, dataSize;
    if (!pe::FindResourceByTypeName(g_imageBuffer, g_imageBufferSize, g_resourceDirRva,
                                     "WAVE", static_cast<uint16_t>(pszSound), dataRva, dataSize)) {
        return result; // такого звука нет — не страшно, просто молчим
    }

    uint16_t channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const uint8_t* pcmData = nullptr;
    uint32_t pcmSize = 0;
    if (!ParseWav(g_imageBuffer + dataRva, dataSize, channels, sampleRate, bitsPerSample, pcmData, pcmSize)) {
        return result; // не смогли разобрать WAV — не крашимся, просто молчим
    }

    std::vector<int16_t> output = ConvertToOutputFormat(pcmData, pcmSize, channels, sampleRate, bitsPerSample);
    if (!output.empty() && g_playAudioImpl) {
        g_playAudioImpl(output);
    }

    return result;
}

// BOOL UpdateWindow(HWND hWnd) — если у окна есть невалидная область,
// НАПРЯМУЮ (в обход очереди сообщений) вызывает его WndProc с WM_PAINT —
// именно так ведёт себя настоящая функция (это синхронный SendMessage,
// а не PostMessage через очередь).
HostCallOutcome Impl_UpdateWindow(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    HostCallOutcome result;
    result.returnValue = 1; // TRUE

    if (!win32::g_messageQueue.ConsumeNeedsPaint(hwnd)) {
        return result; // нечего перерисовывать — как и в реальности, ничего не делаем
    }

    const win32::WindowClassInfo* cls = win32::g_windowManager.GetWindowClass(hwnd);
    if (!cls || cls->wndProc == 0) return result;

    win32::CallGuestFunctionStdcall(cpu, mem, cls->wndProc,
                                      {hwnd, win32::WM_PAINT, 0, 0}, g_tebAddr, g_tebAddr);
    return result;
}

// HDC BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint) — начало обработки
// WM_PAINT. Заполняем PAINTSTRUCT (hdc@0, fErase@4, rcPaint@8..23,
// fRestore@24, fIncUpdate@28, rgbReserved@32..63 — итого 64 байта) и
// возвращаем наш единственный экранный DC (мы не различаем несколько окон
// по-настоящему — у нас пока один "рабочий стол").
HostCallOutcome Impl_BeginPaint(cpu::CpuState& cpu, cpu::IGuestMemory& mem) {
    uint32_t hwnd = ReadArg(cpu, mem, 0);
    uint32_t lpPaint = ReadArg(cpu, mem, 1);

    uint32_t hdc = gdi::g_gdi.GetOrCreateScreenDC(640, 360);

    if (lpPaint != 0) {
        const win32::WindowInfo* win = win32::g_windowManager.GetWindow(hwnd);
        int32_t w = win ? win->width : 640;
        int32_t h = win ? win->height : 360;

        mem.Write32(lpPaint + 0, hdc);   // hdc
        mem.Write32(lpPaint + 4, 1);     // fErase = TRUE
        mem.Write32(lpPaint + 8, 0);     // rcPaint.left
        mem.Write32(lpPaint + 12, 0);    // rcPaint.top
        mem.Write32(lpPaint + 16, static_cast<uint32_t>(w)); // rcPaint.right
        mem.Write32(lpPaint + 20, static_cast<uint32_t>(h)); // rcPaint.bottom
        mem.Write32(lpPaint + 24, 0);    // fRestore
        mem.Write32(lpPaint + 28, 0);    // fIncUpdate
        for (int i = 0; i < 8; i++) mem.Write32(lpPaint + 32 + i * 4, 0); // rgbReserved
    }

    HostCallOutcome result;
    result.returnValue = hdc;
    return result;
}

// BOOL EndPaint(HWND hWnd, const PAINTSTRUCT* lpPaint) — завершение
// обработки WM_PAINT. У нас нет реального освобождения DC — честный no-op с успехом.
HostCallOutcome Impl_EndPaint(cpu::CpuState&, cpu::IGuestMemory&) {
    HostCallOutcome result;
    result.returnValue = 1; // TRUE
    return result;
}

} // namespace

const HostFunctionEntry g_hostFunctionTable[] = {
    { "msvcrt.dll",   "exit",               4,  CallConv::Cdecl,   &Impl_Exit },
    { "msvcrt.dll",   "_exit",              4,  CallConv::Cdecl,   &Impl_Exit },
    { "msvcrt.dll",   "rand",               0,  CallConv::Cdecl,   &Impl_Rand },
    { "msvcrt.dll",   "srand",              4,  CallConv::Cdecl,   &Impl_Srand },
    { "msvcrt.dll",   "_controlfp",         8,  CallConv::Cdecl,   &Impl_ControlFp },
    { "msvcrt.dll",   "__set_app_type",     4,  CallConv::Cdecl,   &Impl_SetAppType },
    { "msvcrt.dll",   "__p__fmode",         0,  CallConv::Cdecl,   &Impl_PFmode },
    { "msvcrt.dll",   "__p__commode",       0,  CallConv::Cdecl,   &Impl_PCommode },
    { "msvcrt.dll",   "__setusermatherr",   4,  CallConv::Cdecl,   &Impl_SetUserMatherr },
    { "msvcrt.dll",   "_initterm",          8,  CallConv::Cdecl,   &Impl_InitTerm },
    { "msvcrt.dll",   "__getmainargs",      20, CallConv::Cdecl,   &Impl_GetMainArgs },
    { "msvcrt.dll",   "__wgetmainargs",     20, CallConv::Cdecl,   &Impl_WGetMainArgs },
    { "MFC42u.DLL",   "",                   16, CallConv::Stdcall, &Impl_Probe_Ordinal1569, 1569 },
    { "msvcrt.dll",   "_cexit",             0,  CallConv::Cdecl,   &Impl_CExit },
    { "msvcrt.dll",   "_c_exit",            0,  CallConv::Cdecl,   &Impl_CExit },
    { "msvcrt.dll",   "_XcptFilter",        8,  CallConv::Cdecl,   &Impl_XcptFilter },
    { "msvcrt.dll",   "_except_handler3",   16, CallConv::Cdecl,   &Impl_ExceptHandler3 },
    { "KERNEL32.dll", "GetTickCount",       0,  CallConv::Stdcall, &Impl_GetTickCount },
    { "KERNEL32.dll", "GetSystemTimeAsFileTime", 4, CallConv::Stdcall, &Impl_GetSystemTimeAsFileTime },
    { "KERNEL32.dll", "GetCurrentProcessId", 0, CallConv::Stdcall, &Impl_GetCurrentProcessId },
    { "KERNEL32.dll", "GetCurrentThreadId", 0, CallConv::Stdcall, &Impl_GetCurrentThreadId },
    { "KERNEL32.dll", "QueryPerformanceCounter", 4, CallConv::Stdcall, &Impl_QueryPerformanceCounter },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc1165_GetState, 1165 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc415_Ctor, 415 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc561_Ctor, 561 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc1634_Attach, 1634 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Cdecl,   &Impl_Mfc823_OperatorNew, 823 },
    { "MFC42u.DLL",   "",                   16, CallConv::Stdcall, &Impl_Mfc520_Ctor, 520 },
    { "MFC42u.DLL",   "",                   16, CallConv::Stdcall, &Impl_Mfc6133_Ctor, 6133 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc986_Ctor, 986 },
    { "MFC42u.DLL",   "",                   12, CallConv::Stdcall, &Impl_Mfc2028_ConnectTemplate, 2028 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc5713_ParseCmdLine, 5713 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc5712_Check, 5712 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc4604_OnFileNew, 4604 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc1172_AfxGetThread, 1172 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc323_CDC_Ctor, 323 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc2854_CDC_FromHandle, 2854 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc5784_CDC_SelectObject_Pen, 5784 },
    { "INTERNAL",     "SafeCdcVtableSlot",  4,  CallConv::Stdcall, &Impl_SafeCdcVtableSlot },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc1090_AddView, 1090 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc3193_GetFirstViewPosition, 3193 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc3449_GetNextView, 3449 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc414_CObArray_Ctor, 414 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc2550_DragAcceptFiles, 2550 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc4458_CViewOnCreate, 4458 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc4451_CFrameWndOnCreate, 4451 },
    { "MFC42u.DLL",   "",                   8,  CallConv::Stdcall, &Impl_Mfc4884_OnPrepareDC, 4884 },
    { "OLEAUT32.dll", "",                   4,  CallConv::Stdcall, &Impl_SysAllocString, 2 },
    { "OLEAUT32.dll", "",                   8,  CallConv::Stdcall, &Impl_SysAllocStringLen, 4 },
    { "OLEAUT32.dll", "",                   4,  CallConv::Stdcall, &Impl_SysFreeString, 6 },
    { "OLEAUT32.dll", "",                   4,  CallConv::Stdcall, &Impl_SysStringLen, 7 },
    { "INTERNAL",     "AfxWndProc",         16, CallConv::Stdcall, &Impl_AfxWndProc },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc4154_Ctor, 4154 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc4155_LoadStringW, 4155 },
    { "MFC42u.DLL",   "",                   16, CallConv::Stdcall, &Impl_Mfc3517_Ctor, 3517 },
    { "MFC42u.DLL",   "",                   4,  CallConv::Stdcall, &Impl_Mfc858_CopyAssign, 858 },
    { "KERNEL32.dll", "GetCommandLineW",    0,  CallConv::Stdcall, &Impl_GetCommandLineW },
    { "SHELL32.dll",  "CommandLineToArgvW", 8,  CallConv::Stdcall, &Impl_CommandLineToArgvW },
    { "KERNEL32.dll", "GlobalFree",         4,  CallConv::Stdcall, &Impl_GlobalFree },
    { "ole32.dll",    "CLSIDFromString",    8,  CallConv::Stdcall, &Impl_CLSIDFromString },
    { "MFC42u.DLL",   "",                   12, CallConv::Stdcall, &Impl_Mfc3516_ReadProfileCount, 3516 },
    { "MFC42u.DLL",   "",                   0,  CallConv::Cdecl,   &Impl_Mfc5710_Run, 5710 },
    { "msvcrt.dll",   "_onexit",            4,  CallConv::Cdecl,   &Impl_OnExit },
    { "msvcrt.dll",   "_wcsdup",            4,  CallConv::Cdecl,   &Impl_WcsDup },
    { "USER32.dll",   "SystemParametersInfoW", 16, CallConv::Stdcall, &Impl_SystemParametersInfoW },
    { "GDI32.dll",    "CreateFontIndirectW", 4, CallConv::Stdcall, &Impl_CreateFontIndirectW },
    { "KERNEL32.dll", "SetErrorMode",       4,  CallConv::Stdcall, &Impl_SetErrorMode },
    { "GDI32.dll",    "CreateBitmap",       20, CallConv::Stdcall, &Impl_CreateBitmap },
    { "GDI32.dll",    "CreateSolidBrush",   4,  CallConv::Stdcall, &Impl_CreateSolidBrush },
    { "GDI32.dll",    "CreatePatternBrush", 4,  CallConv::Stdcall, &Impl_CreatePatternBrush },
    { "GDI32.dll",    "SelectPalette",      12, CallConv::Stdcall, &Impl_SelectPalette },
    { "GDI32.dll",    "Polygon",            12, CallConv::Stdcall, &Impl_Polygon },
    { "GDI32.dll",    "Polyline",           12, CallConv::Stdcall, &Impl_Polyline },
    { "GDI32.dll",    "Rectangle",          20, CallConv::Stdcall, &Impl_Rectangle },
    { "GDI32.dll",    "Ellipse",            20, CallConv::Stdcall, &Impl_Ellipse },
    { "USER32.dll",   "FillRect",           12, CallConv::Stdcall, &Impl_FillRect },
    { "GDI32.dll",    "SetTextColor",       8,  CallConv::Stdcall, &Impl_SetTextColor },
    { "GDI32.dll",    "SetBkColor",         8,  CallConv::Stdcall, &Impl_SetBkColor },
    { "GDI32.dll",    "SetBkMode",          8,  CallConv::Stdcall, &Impl_SetBkMode },
    { "GDI32.dll",    "GetTextColor",       4,  CallConv::Stdcall, &Impl_GetTextColor },
    { "GDI32.dll",    "GetBkMode",          4,  CallConv::Stdcall, &Impl_GetBkMode },
    { "GDI32.dll",    "GetPixel",           12, CallConv::Stdcall, &Impl_GetPixel },
    { "GDI32.dll",    "RealizePalette",     4,  CallConv::Stdcall, &Impl_RealizePalette },
    { "msvcrt.dll",   "_itow",              12, CallConv::Cdecl,   &Impl_Itow },
    { "USER32.dll",   "GetSysColor",        4,  CallConv::Stdcall, &Impl_GetSysColor },
    { "gdiplus.dll",  "GdiplusStartup",     12, CallConv::Stdcall, &Impl_GdiplusStartup },
    { "gdiplus.dll",  "GdiplusShutdown",    4,  CallConv::Stdcall, &Impl_GdiplusShutdown },
    { "USER32.dll",   "SetRectEmpty",       4,  CallConv::Stdcall, &Impl_SetRectEmpty },
    { "KERNEL32.dll", "InterlockedExchange", 8, CallConv::Stdcall, &Impl_InterlockedExchange },
    { "KERNEL32.dll", "GetLastError",       0,  CallConv::Stdcall, &Impl_GetLastError },
    { "KERNEL32.dll", "RaiseException",     16, CallConv::Stdcall, &Impl_RaiseException_Experimental },
    { "KERNEL32.dll", "OutputDebugStringA", 4,  CallConv::Stdcall, &Impl_OutputDebugStringA },
    { "KERNEL32.dll", "GetModuleHandleA",   4,  CallConv::Stdcall, &Impl_GetModuleHandleA },
    { "KERNEL32.dll", "GetModuleHandleW",   4,  CallConv::Stdcall, &Impl_GetModuleHandleA }, // не смотрит на имя — безопасно переиспользовать
    { "KERNEL32.dll", "GetStartupInfoA",    4,  CallConv::Stdcall, &Impl_GetStartupInfoA },
    { "KERNEL32.dll", "GetStartupInfoW",    4,  CallConv::Stdcall, &Impl_GetStartupInfoA }, // раскладка структуры идентична A/W — переиспользуем
    { "KERNEL32.dll", "GetProcAddress",     8,  CallConv::Stdcall, &Impl_GetProcAddress },
    { "USER32.dll",   "LoadStringW",        16, CallConv::Stdcall, &Impl_LoadStringW },
    { "USER32.dll",   "GetSystemMetrics",   4,  CallConv::Stdcall, &Impl_GetSystemMetrics },
    { "ADVAPI32.dll", "RegCreateKeyExW",    36, CallConv::Stdcall, &Impl_RegCreateKeyExW },
    { "ADVAPI32.dll", "RegOpenKeyExA",      20, CallConv::Stdcall, &Impl_RegOpenKeyExA },
    { "ADVAPI32.dll", "RegOpenKeyExW",      20, CallConv::Stdcall, &Impl_RegOpenKeyExW },
    { "ADVAPI32.dll", "RegEnumKeyW",        16, CallConv::Stdcall, &Impl_RegEnumKeyW },
    { "ADVAPI32.dll", "RegEnumKeyExW",      32, CallConv::Stdcall, &Impl_RegEnumKeyW },
    { "ADVAPI32.dll", "RegDeleteKeyW",      8,  CallConv::Stdcall, &Impl_RegDeleteW },
    { "ADVAPI32.dll", "RegDeleteKeyExW",    16, CallConv::Stdcall, &Impl_RegDeleteW },
    { "ADVAPI32.dll", "RegDeleteValueW",    8,  CallConv::Stdcall, &Impl_RegDeleteW },
    { "ADVAPI32.dll", "RegOpenKeyW",        12, CallConv::Stdcall, &Impl_RegOpenKeyW },
    { "ADVAPI32.dll", "RegQueryValueExW",   24, CallConv::Stdcall, &Impl_RegQueryValueExW },
    { "ADVAPI32.dll", "RegQueryValueExA",   24, CallConv::Stdcall, &Impl_RegQueryValueExA },
    { "ADVAPI32.dll", "RegSetValueExW",     24, CallConv::Stdcall, &Impl_RegSetValueExW },
    { "ADVAPI32.dll", "RegCloseKey",        4,  CallConv::Stdcall, &Impl_RegCloseKey },
    { "KERNEL32.dll", "GetPrivateProfileIntW",    16, CallConv::Stdcall, &Impl_GetPrivateProfileIntW },
    { "KERNEL32.dll", "GetPrivateProfileStringW", 24, CallConv::Stdcall, &Impl_GetPrivateProfileStringW },
    { "USER32.dll",   "GetDesktopWindow",   0,  CallConv::Stdcall, &Impl_GetDesktopWindow },
    { "USER32.dll",   "GetDC",              4,  CallConv::Stdcall, &Impl_GetDC },
    { "GDI32.dll",    "CreateCompatibleDC",     4,  CallConv::Stdcall, &Impl_CreateCompatibleDC },
    { "GDI32.dll",    "CreateCompatibleBitmap", 12, CallConv::Stdcall, &Impl_CreateCompatibleBitmap },
    { "GDI32.dll",    "SelectObject",           8,  CallConv::Stdcall, &Impl_SelectObject },
    { "GDI32.dll",    "DeleteObject",           4,  CallConv::Stdcall, &Impl_DeleteObject },
    { "GDI32.dll",    "DeleteDC",               4,  CallConv::Stdcall, &Impl_DeleteDC },
    { "GDI32.dll",    "SetPixel",               16, CallConv::Stdcall, &Impl_SetPixel },
    { "GDI32.dll",    "MoveToEx",               16, CallConv::Stdcall, &Impl_MoveToEx },
    { "GDI32.dll",    "LineTo",                 12, CallConv::Stdcall, &Impl_LineTo },
    { "GDI32.dll",    "BitBlt",                 36, CallConv::Stdcall, &Impl_BitBlt },
    { "GDI32.dll",    "CreatePen",              12, CallConv::Stdcall, &Impl_CreatePen },
    { "GDI32.dll",    "GetStockObject",         4,  CallConv::Stdcall, &Impl_GetStockObject },
    { "GDI32.dll",    "GetDeviceCaps",          8,  CallConv::Stdcall, &Impl_GetDeviceCaps },
    { "GDI32.dll",    "SetROP2",                8,  CallConv::Stdcall, &Impl_SetROP2 },
    { "GDI32.dll",    "GetLayout",              4,  CallConv::Stdcall, &Impl_GetLayout },
    { "GDI32.dll",    "SetLayout",              8,  CallConv::Stdcall, &Impl_SetLayout },
    { "USER32.dll",   "ReleaseDC",              8,  CallConv::Stdcall, &Impl_ReleaseDC },
    { "KERNEL32.dll", "lstrlenW",               4,  CallConv::Stdcall, &Impl_LstrlenW },
    { "COMCTL32.dll", "InitCommonControlsEx",   4,  CallConv::Stdcall, &Impl_InitCommonControlsEx },
    { "USER32.dll",   "LoadIconW",              8,  CallConv::Stdcall, &Impl_LoadIconW },
    { "USER32.dll",   "LoadCursorW",            8,  CallConv::Stdcall, &Impl_LoadCursorW },
    { "USER32.dll",   "SetCursor",              4,  CallConv::Stdcall, &Impl_SetCursor },
    { "KERNEL32.dll", "InterlockedIncrement",   4,  CallConv::Stdcall, &Impl_InterlockedIncrement },
    { "KERNEL32.dll", "InterlockedDecrement",   4,  CallConv::Stdcall, &Impl_InterlockedDecrement },
    { "ole32.dll",    "CoCreateInstance",       20, CallConv::Stdcall, &Impl_CoCreateInstance },
    { "USER32.dll",   "RegisterClassW",         4,  CallConv::Stdcall, &Impl_RegisterClassW },
    { "USER32.dll",   "CreateWindowExW",        48, CallConv::Stdcall, &Impl_CreateWindowExW },
    { "USER32.dll",   "LoadMenuW",              8,  CallConv::Stdcall, &Impl_LoadMenuW },
    { "USER32.dll",   "LoadAcceleratorsW",      8,  CallConv::Stdcall, &Impl_LoadAcceleratorsW },
    { "USER32.dll",   "GetMenuItemRect",        16, CallConv::Stdcall, &Impl_GetMenuItemRect },
    { "KERNEL32.dll", "FindResourceW",          12, CallConv::Stdcall, &Impl_FindResourceW },
    { "KERNEL32.dll", "LoadResource",           8,  CallConv::Stdcall, &Impl_LoadResource },
    { "KERNEL32.dll", "LockResource",           4,  CallConv::Stdcall, &Impl_LockResource },
    { "GDI32.dll",    "SetDIBitsToDevice",      48, CallConv::Stdcall, &Impl_SetDIBitsToDevice },
    { "USER32.dll",   "CheckMenuItem",          12, CallConv::Stdcall, &Impl_CheckMenuItem },
    { "USER32.dll",   "SetMenu",                8,  CallConv::Stdcall, &Impl_SetMenu },
    { "USER32.dll",   "MoveWindow",             24, CallConv::Stdcall, &Impl_MoveWindow },
    { "USER32.dll",   "SetRect",                20, CallConv::Stdcall, &Impl_SetRect },
    { "USER32.dll",   "InvalidateRect",         12, CallConv::Stdcall, &Impl_InvalidateRect },
    { "USER32.dll",   "CopyRect",               8,  CallConv::Stdcall, &Impl_CopyRect },
    { "USER32.dll",   "GetWindowRect",          8,  CallConv::Stdcall, &Impl_GetWindowRect },
    { "USER32.dll",   "IsWindow",               4,  CallConv::Stdcall, &Impl_IsWindow },
    { "USER32.dll",   "GetFocus",               0,  CallConv::Stdcall, &Impl_GetFocus },
    { "USER32.dll",   "SetActiveWindow",        4,  CallConv::Stdcall, &Impl_SetActiveWindow },
    { "USER32.dll",   "GetCapture",             0,  CallConv::Stdcall, &Impl_GetCapture },
    { "USER32.dll",   "GetKeyState",            4,  CallConv::Stdcall, &Impl_GetKeyState },
    { "USER32.dll",   "IntersectRect",          12, CallConv::Stdcall, &Impl_IntersectRect },
    { "USER32.dll",   "InflateRect",            12, CallConv::Stdcall, &Impl_InflateRect },
    { "MFC42u.DLL",   "",                       4,  CallConv::Stdcall, &Impl_Mfc2859_FromHandle, 2859 },
    { "MFC42u.DLL",   "",                       0,  CallConv::Cdecl,   &Impl_Mfc5977_SetFocus, 5977 },
    { "MFC42u.DLL",   "",                       0,  CallConv::Cdecl,   &Impl_Mfc3016_GetCurrentMessage, 3016 },
    { "MFC42u.DLL",   "",                       12, CallConv::Stdcall, &Impl_Mfc6065_SetPaneText, 6065 },
    { "USER32.dll",   "ShowWindow",             8,  CallConv::Stdcall, &Impl_ShowWindow },
    { "USER32.dll",   "GetMessageW",            16, CallConv::Stdcall, &Impl_GetMessage },
    { "USER32.dll",   "GetMessageA",            16, CallConv::Stdcall, &Impl_GetMessage },
    { "USER32.dll",   "PeekMessageW",           20, CallConv::Stdcall, &Impl_PeekMessage },
    { "USER32.dll",   "PeekMessageA",           20, CallConv::Stdcall, &Impl_PeekMessage },
    { "USER32.dll",   "TranslateMessage",       4,  CallConv::Stdcall, &Impl_TranslateMessage },
    { "USER32.dll",   "DispatchMessageW",       4,  CallConv::Stdcall, &Impl_DispatchMessage },
    { "USER32.dll",   "DispatchMessageA",       4,  CallConv::Stdcall, &Impl_DispatchMessage },
    { "USER32.dll",   "PostQuitMessage",        4,  CallConv::Stdcall, &Impl_PostQuitMessage },
    { "USER32.dll",   "DefWindowProcW",         16, CallConv::Stdcall, &Impl_DefWindowProcW },
    { "USER32.dll",   "TranslateAcceleratorW",  12, CallConv::Stdcall, &Impl_TranslateAcceleratorW },
    { "USER32.dll",   "PtInRect",               12, CallConv::Stdcall, &Impl_PtInRect },
    { "USER32.dll",   "MapWindowPoints",        16, CallConv::Stdcall, &Impl_MapWindowPoints },
    { "KERNEL32.dll", "GetModuleFileNameA",     12, CallConv::Stdcall, &Impl_GetModuleFileNameA },
    { "KERNEL32.dll", "lstrcpyW",               8,  CallConv::Stdcall, &Impl_lstrcpyW },
    { "KERNEL32.dll", "LoadLibraryA",           4,  CallConv::Stdcall, &Impl_LoadLibraryA },
    { "USER32.dll",   "MessageBoxW",            16, CallConv::Stdcall, &Impl_MessageBoxW },
    { "USER32.dll",   "GetDlgItemInt",          16, CallConv::Stdcall, &Impl_GetDlgItemInt },
    { "USER32.dll",   "WinHelpW",               16, CallConv::Stdcall, &Impl_WinHelpW },
    { "USER32.dll",   "SetDlgItemInt",          16, CallConv::Stdcall, &Impl_SetDlgItemInt },
    { "USER32.dll",   "EndDialog",              8,  CallConv::Stdcall, &Impl_EndDialog },
    { "USER32.dll",   "SetDlgItemTextW",        12, CallConv::Stdcall, &Impl_SetDlgItemTextW },
    { "USER32.dll",   "wsprintfW",              8,  CallConv::Cdecl,   &Impl_wsprintfW },
    { "USER32.dll",   "SendMessageW",           16, CallConv::Stdcall, &Impl_SendMessageW },
    { "USER32.dll",   "GetDlgItem",             8,  CallConv::Stdcall, &Impl_GetDlgItem },
    { "USER32.dll",   "GetDlgItemTextW",        16, CallConv::Stdcall, &Impl_GetDlgItemTextW },
    { "USER32.dll",   "DialogBoxParamW",        20, CallConv::Stdcall, &Impl_DialogBoxParamW },
    { "USER32.dll",   "PostMessageW",           16, CallConv::Stdcall, &Impl_PostMessageW },
    { "SHELL32.dll",  "ShellAboutW",            16, CallConv::Stdcall, &Impl_ShellAboutW },
    { "USER32.dll",   "SetCapture",             4,  CallConv::Stdcall, &Impl_SetCapture },
    { "USER32.dll",   "ReleaseCapture",         0,  CallConv::Stdcall, &Impl_ReleaseCapture },
    { "USER32.dll",   "SetTimer",               16, CallConv::Stdcall, &Impl_SetTimer },
    { "USER32.dll",   "KillTimer",              8,  CallConv::Stdcall, &Impl_KillTimer },
    { "WINMM.dll",    "PlaySoundW",             12, CallConv::Stdcall, &Impl_PlaySoundW },
    { "USER32.dll",   "UpdateWindow",           4,  CallConv::Stdcall, &Impl_UpdateWindow },
    { "USER32.dll",   "BeginPaint",             8,  CallConv::Stdcall, &Impl_BeginPaint },
    { "USER32.dll",   "EndPaint",               8,  CallConv::Stdcall, &Impl_EndPaint },
};
const size_t g_hostFunctionTableSize = sizeof(g_hostFunctionTable) / sizeof(g_hostFunctionTable[0]);

namespace {

bool DllNameEquals(const char* a, const char* b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
        if (x == 0) return true;
    }
}

// Таблицы, зарегистрированные другими файлами (kernel32_*.cpp и т.п.).
// Индексы продолжают статическую таблицу: [size .. size + extra).
std::vector<const HostFunctionEntry*>& ExtraEntries() {
    static std::vector<const HostFunctionEntry*> entries;
    return entries;
}
// Слой каждой записи из ExtraEntries (nullptr — всегда включена)
std::vector<const char*>& ExtraLayers() {
    static std::vector<const char*> layers;
    return layers;
}
std::vector<std::string>& EnabledLayers() {
    static std::vector<std::string> layers;
    return layers;
}
bool ExtraEnabled(size_t i) {
    const char* layer = ExtraLayers()[i];
    if (!layer) return true;
    for (const std::string& l : EnabledLayers()) if (l == layer) return true;
    return false;
}

} // namespace

bool RegisterHostFunctions(const HostFunctionEntry* entries, size_t count) {
    return RegisterHostFunctionsLayer(entries, count, nullptr);
}

bool RegisterHostFunctionsLayer(const HostFunctionEntry* entries, size_t count, const char* layer) {
    for (size_t i = 0; i < count; ++i) {
        ExtraEntries().push_back(&entries[i]);
        ExtraLayers().push_back(layer);
    }
    return true;
}

void EnableHostLayer(const char* layer, bool on) {
    auto& v = EnabledLayers();
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == layer) { if (!on) v.erase(v.begin() + static_cast<long>(i)); return; }
    if (on) v.push_back(layer);
}

bool IsHostLayerEnabled(const char* layer) {
    for (const std::string& l : EnabledLayers()) if (l == layer) return true;
    return false;
}

size_t HostFunctionCount() { return g_hostFunctionTableSize + ExtraEntries().size(); }

const HostFunctionEntry& GetHostFunction(int index) {
    size_t i = static_cast<size_t>(index);
    if (i < g_hostFunctionTableSize) return g_hostFunctionTable[i];
    return *ExtraEntries()[i - g_hostFunctionTableSize];
}

// Сначала ищем в зарегистрированных таблицах (новые реализации
// переопределяют старые из этого файла), потом в статической.
static int FindHostFunctionExact(const char* dllName, const char* funcName) {
    const auto& extra = ExtraEntries();
    for (size_t i = extra.size(); i-- > 0;) {
        if (!ExtraEnabled(i)) continue;
        if (extra[i]->funcName[0] && std::strcmp(extra[i]->funcName, funcName) == 0 &&
            DllNameEquals(extra[i]->dllName, dllName)) {
            return static_cast<int>(g_hostFunctionTableSize + i);
        }
    }
    for (size_t i = 0; i < g_hostFunctionTableSize; i++) {
        if (std::strcmp(g_hostFunctionTable[i].funcName, funcName) == 0 &&
            DllNameEquals(g_hostFunctionTable[i].dllName, dllName)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// DLL «наборов API» Windows 7+ (api-ms-win-core-*.dll) и KERNELBASE — лишь
// другое имя тех же функций KERNEL32/ADVAPI32: такие импорты делают
// библиотеки поновее XP (например, mfc42u.dll из Windows 7).
static bool IsApiSetDll(const char* dll) {
    return strncasecmp(dll, "api-ms-win-", 11) == 0 || strncasecmp(dll, "ext-ms-win-", 11) == 0 ||
           strcasecmp(dll, "kernelbase.dll") == 0 || strcasecmp(dll, "kernelbase") == 0;
}

int FindHostFunction(const char* dllName, const char* funcName) {
    int idx = FindHostFunctionExact(dllName, funcName);
    if (idx >= 0 || !IsApiSetDll(dllName)) return idx;
    for (const char* d : {"KERNEL32.dll", "ADVAPI32.dll", "USER32.dll", "msvcrt.dll"}) {
        idx = FindHostFunctionExact(d, funcName);
        if (idx >= 0) return idx;
    }
    return -1;
}

int FindHostFunctionByOrdinal(const char* dllName, uint16_t ordinal) {
    const auto& extra = ExtraEntries();
    for (size_t i = extra.size(); i-- > 0;) {
        if (!ExtraEnabled(i)) continue;
        if (extra[i]->funcName[0] == '\0' && extra[i]->ordinal == ordinal && DllNameEquals(extra[i]->dllName, dllName)) {
            return static_cast<int>(g_hostFunctionTableSize + i);
        }
    }
    for (size_t i = 0; i < g_hostFunctionTableSize; i++) {
        if (g_hostFunctionTable[i].funcName[0] == '\0' &&
            g_hostFunctionTable[i].ordinal == ordinal &&
            DllNameEquals(g_hostFunctionTable[i].dllName, dllName)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int HostAddressToIndex(uint32_t addr) {
    if (addr < HOST_FUNCTION_BASE) return -1;
    uint32_t delta = addr - HOST_FUNCTION_BASE;
    if (delta % HOST_FUNCTION_STRIDE != 0) return -1;
    uint32_t idx = delta / HOST_FUNCTION_STRIDE;
    if (idx >= HostFunctionCount()) return -1;
    return static_cast<int>(idx);
}

// Сравнение имён DLL без учёта регистра (в IAT встречается и "MFC42u.DLL",
// и "mfc42u.dll", в зависимости от линкера)
static bool EqualsIgnoreCaseAscii(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::vector<MfcStubSlot> g_mfcStubSlots;
uint32_t g_mfcStateBase = 0;

uint32_t AllocateMfcStubSlot(const std::string& dllName, uint16_t ordinal) {
    for (size_t i = 0; i < g_mfcStubSlots.size(); i++) {
        if (g_mfcStubSlots[i].ordinal == ordinal && g_mfcStubSlots[i].dllName == dllName) {
            return MFC_STUB_BASE + static_cast<uint32_t>(i) * HOST_FUNCTION_STRIDE;
        }
    }
    g_mfcStubSlots.push_back({dllName, ordinal, std::string(), 0, true});
    return MFC_STUB_BASE + static_cast<uint32_t>(g_mfcStubSlots.size() - 1) * HOST_FUNCTION_STRIDE;
}

uint32_t AllocateNamedStubSlot(const std::string& dllName, const std::string& funcName) {
    for (size_t i = 0; i < g_mfcStubSlots.size(); i++) {
        if (g_mfcStubSlots[i].funcName == funcName && g_mfcStubSlots[i].dllName == dllName) {
            return MFC_STUB_BASE + static_cast<uint32_t>(i) * HOST_FUNCTION_STRIDE;
        }
    }
    MfcStubSlot slot{dllName, 0, funcName, 0, false};
    slot.abiKnown = Win32ApiCalleeCleanup(dllName.c_str(), funcName.c_str(), slot.cleanupBytes);
    g_mfcStubSlots.push_back(slot);
    return MFC_STUB_BASE + static_cast<uint32_t>(g_mfcStubSlots.size() - 1) * HOST_FUNCTION_STRIDE;
}

namespace {

// Универсальная диагностическая заглушка для ЛЮБОГО ординала, под который
// у нас пока нет настоящей реализации. Печатает, что именно было вызвано
// (ординал + состояние ECX, часто это "this" для __thiscall MFC-методов, и
// первых пары слов на стеке), возвращает 0 — и выполнение идёт ДАЛЬШЕ,
// вместо остановки на первом же незнакомом вызове. Только для хостового
// исследования (реальный printf на устройстве во время графики испортил бы
// экран) — печатаем без проверки g_consoleAvailable намеренно, этот код
// путь не предназначен для финальной сборки на Switch как есть.
HostCallOutcome Impl_MfcOrdinalStub(cpu::CpuState& cpu, cpu::IGuestMemory& mem, const MfcStubSlot& slot) {
    uint32_t ecx = cpu.Get(cpu::Reg32::ECX);
    uint32_t esp = cpu.ESP();
    if (!slot.funcName.empty()) {
        // Заглушка Win32-функции по имени
        if (!slot.abiKnown) {
            if (cpu::g_consoleAvailable) {
                std::printf("[STUB] %s!%s: размер аргументов неизвестен (добавь в win32_api_abi_table.h), ВЕРНУТЬСЯ_В=0x%x\n",
                            slot.dllName.c_str(), slot.funcName.c_str(), mem.Read32(esp));
            }
            cpu::FatalError("Вызвана нереализованная функция Win32 с неизвестной сигнатурой (см. лог [STUB])");
        }
        if (cpu::g_consoleAvailable) {
            std::printf("[STUB] %s!%s вызвана, не реализована, возвращаем 0 (ВЕРНУТЬСЯ_В=0x%x, аргументы: [%x %x %x %x])\n",
                        slot.dllName.c_str(), slot.funcName.c_str(), mem.Read32(esp),
                        mem.Read32(esp+4), mem.Read32(esp+8), mem.Read32(esp+12), mem.Read32(esp+16));
        }
        HostCallOutcome named;
        named.returnValue = 0; // у Win32 нет "this" в ECX, возвращать ECX тут бессмысленно
        return named;
    }
    if (cpu::g_consoleAvailable) {
        std::printf("[MFC STUB] %s ординал %u вызван (ECX=0x%x, ВЕРНУТЬСЯ_В=0x%x, стек: [%x %x %x %x %x %x])\n",
                    slot.dllName.c_str(), slot.ordinal, ecx, mem.Read32(esp+0),
                    mem.Read32(esp+4), mem.Read32(esp+8), mem.Read32(esp+12),
                    mem.Read32(esp+16), mem.Read32(esp+20), mem.Read32(esp+24));
    }
    HostCallOutcome result;
    // ВАЖНО: возвращаем ECX ("this"), а не 0. Огромная доля MFC-вызовов на
    // этапе инициализации — это __thiscall-конструкторы, а по ABI MSVC
    // конструктор ВСЕГДА возвращает свой же "this" в EAX (стандартная
    // конвенция, позволяющая, например, писать "new T()->Method()").
    // Возвращая 0 по умолчанию, мы гарантированно ломали ВСЕ такие цепочки
    // на первом же разыменовании результата — возврат ECX чинит подавляющее
    // большинство конструкторных вызовов "бесплатно", даже без знания,
    // что именно делает конкретный ординал.
    result.returnValue = ecx;
    return result;
}

} // namespace

bool TryHandleHostCall(cpu::CpuState& cpu, cpu::IGuestMemory& mem, HostCallOutcome& outOutcome) {
    int idx = HostAddressToIndex(cpu.eip);
    if (idx >= 0) {
        const HostFunctionEntry& entry = GetHostFunction(idx);
        if (g_traceHostCalls) {
            std::printf("[call] %s!%s(%x, %x, %x, %x)\n", entry.dllName, entry.funcName[0] ? entry.funcName : "#",
                        mem.Read32(cpu.ESP() + 4), mem.Read32(cpu.ESP() + 8), mem.Read32(cpu.ESP() + 12), mem.Read32(cpu.ESP() + 16));
        }
        outOutcome = entry.fn(cpu, mem);
        if (g_traceHostCalls) std::printf("       -> %x\n", outOutcome.returnValue);
        // Выход из процесса мог случиться во вложенном вызове гостя (например,
        // ExitProcess внутри оконной процедуры): тогда о нём сообщает и
        // внешний вызов, чтобы цикл исполнения на любом уровне остановился.
        if (outOutcome.requestExit && !g_guestExitRequested) {
            g_guestExitRequested = true;
            g_guestExitCode = outOutcome.exitCode;
        }
        if (g_guestExitRequested) {
            outOutcome.requestExit = true;
            outOutcome.exitCode = g_guestExitCode;
        }
        if (outOutcome.customReturn) return true;
        cpu.Get(cpu::Reg32::EAX) = outOutcome.returnValue;

        uint32_t returnAddr = mem.Read32(cpu.ESP());
        uint32_t cleanup = (entry.callConv == CallConv::Stdcall) ? entry.argBytes : 0;
        cpu.ESP() += 4 + cleanup;
        cpu.eip = returnAddr;
        return true;
    }

    // Динамический пул MFC-заглушек — отдельный диапазон адресов ЗА
    // основной таблицей (см. MFC_STUB_BASE)
    if (cpu.eip >= MFC_STUB_BASE) {
        uint32_t delta = cpu.eip - MFC_STUB_BASE;
        if (delta % HOST_FUNCTION_STRIDE == 0) {
            size_t slotIdx = delta / HOST_FUNCTION_STRIDE;
            if (slotIdx < g_mfcStubSlots.size()) {
                outOutcome = Impl_MfcOrdinalStub(cpu, mem, g_mfcStubSlots[slotIdx]);
                cpu.Get(cpu::Reg32::EAX) = outOutcome.returnValue;

                // ИСПРАВЛЕНО (полный аудит ABI): раньше здесь стояло
                // допущение "__thiscall, стек чистит вызывающий код, как
                // cdecl". Это НЕВЕРНО для MSVC: __thiscall и __stdcall
                // чистят свои стековые аргументы САМИ. Каждый неизвестный
                // MFC-метод с аргументами сдвигал стек вызывающего кода на
                // N байт, и тот падал позже в совершенно другом месте
                // (корень крашей Кисти: ординалы 5784 и 2854). Аудит нашёл
                // 342 таких метода только в paint.exe. Теперь очистка
                // берётся из таблицы, вычисленной по декорированным именам
                // экспортов (mfc42u_abi_table.h). Семантику метода
                // заглушка по-прежнему не эмулирует (возвращает 0), но
                // стек после неё всегда корректен.
                uint32_t cleanup = 0;
                if (!g_mfcStubSlots[slotIdx].funcName.empty()) {
                    cleanup = g_mfcStubSlots[slotIdx].cleanupBytes; // Win32 по имени
                } else if (EqualsIgnoreCaseAscii(g_mfcStubSlots[slotIdx].dllName, "MFC42u.DLL")) {
                    cleanup = Mfc42uCalleeCleanup(g_mfcStubSlots[slotIdx].ordinal);
                }
                uint32_t returnAddr = mem.Read32(cpu.ESP());
                cpu.ESP() += 4 + cleanup;
                cpu.eip = returnAddr;
                return true;
            }
        }
    }

    return false;
}

} // namespace win32
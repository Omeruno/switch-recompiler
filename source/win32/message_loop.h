#pragma once
// message_loop.h
//
// Очередь сообщений (WM_*) и механизм НАСТОЯЩЕГО вызова гостевого кода
// (WndProc и т.п.) изнутри host-функций. Это ключевая часть: DispatchMessage
// должен по-настоящему выполнить оконную процедуру игры, а не просто
// вернуть заглушку — иначе игра никогда не отреагирует на WM_PAINT и не
// нарисует ничего на экране.
//
// Механизм честный: эмулирует call/ret ровно так, как это сделало бы
// настоящее железо — пушим аргументы и фейковый адрес возврата (sentinel),
// передаём управление, крутим Step() (с перехватом host-вызовов внутри —
// WndProc почти наверняка сам дёргает BeginPaint/DefWindowProcW и т.п.),
// пока не вернёмся на sentinel, читаем EAX как результат.

#include <cstdint>
#include <vector>
#include <deque>
#include <unordered_map>
#include "../cpu/cpu_state.h"
#include "../cpu/guest_memory.h"

namespace win32 {

constexpr uint32_t WM_NULL        = 0x0000;
constexpr uint32_t WM_DESTROY     = 0x0002;
constexpr uint32_t WM_PAINT       = 0x000F;
constexpr uint32_t WM_CLOSE       = 0x0010;
constexpr uint32_t WM_QUIT        = 0x0012;
constexpr uint32_t WM_ERASEBKGND  = 0x0014;
constexpr uint32_t WM_COMMAND     = 0x0111;
constexpr uint32_t WM_INITDIALOG  = 0x0110;
constexpr uint32_t WM_TIMER       = 0x0113;
constexpr uint32_t WM_MOUSEMOVE   = 0x0200;
constexpr uint32_t WM_LBUTTONDOWN = 0x0201;
constexpr uint32_t WM_LBUTTONUP   = 0x0202;
constexpr uint32_t WM_RBUTTONDOWN = 0x0204;
constexpr uint32_t WM_RBUTTONUP   = 0x0205;

struct QueuedMessage {
    uint32_t hwnd = 0;
    uint32_t message = 0;
    uint32_t wParam = 0;
    uint32_t lParam = 0;
};

class MessageQueue {
public:
    void Post(uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam);
    bool Pop(QueuedMessage& out);

    // Как Pop, но учитывает диапазон [filterMin,filterMax] — реальный
    // PeekMessage именно так фильтрует (0,0 означает "любые сообщения").
    // Если ПЕРВОЕ сообщение в очереди не подходит под фильтр, оно остаётся
    // в очереди (не теряется) — просто сейчас не возвращается.
    bool PopFiltered(uint32_t filterMin, uint32_t filterMax, QueuedMessage& out);

    // Как PopFiltered, но НЕ убирает сообщение из очереди (для настоящего
    // PM_NOREMOVE — найдено критичным в этой сессии: реальный
    // CWinThread::Run() в MFC вызывает PeekMessage(..., PM_NOREMOVE) в
    // цикле проверки "не пора ли выйти из простоя", и если ЭТО "съедает"
    // сообщение из очереди, реальная последующая обработка
    // GetMessage/DispatchMessage находит очередь уже пустой — именно так,
    // судя по всему, происходило с paint.exe: сообщение реально доходило
    // до очереди (Post отрабатывал), но никогда не долетало до wndProc).
    bool PeekFiltered(uint32_t filterMin, uint32_t filterMax, QueuedMessage& out) const;

    void InvalidateWindow(uint32_t hwnd);
    bool AnyWindowNeedsPaint(uint32_t& outHwnd) const;
    bool ConsumeNeedsPaint(uint32_t hwnd); // true+сбрасывает флаг, если было выставлено

    void RequestQuit();
    bool QuitRequested() const { return quitRequested_; }

    // ДИАГНОСТИКА (эта сессия, третий заход): прямой доступ к размеру
    // очереди, для проверки — реально ли Post() кладёт сообщения, и
    // реально ли что-то их потом убирает (или они там просто копятся,
    // не находимые фильтром).
    size_t Size() const { return queue_.size(); }

private:
    std::deque<QueuedMessage> queue_;
    std::unordered_map<uint32_t, bool> needsPaint_;
    bool quitRequested_ = false;
};

extern MessageQueue g_messageQueue;

// Реестр таймеров SetTimer/KillTimer — раньше эти функции были просто
// "заглушками" (не роняли игру, но и не тикали по-настоящему), из-за чего
// секундомер в сапёре замирал на первой секунде. Теперь SetTimer реально
// регистрирует таймер, а проверка "не пора ли сработать" происходит в
// host_functions.cpp (там же есть доступ к реальному времени через
// g_getTickCountImpl) при каждом опросе очереди сообщений.
struct RegisteredTimer {
    uint32_t hwnd;
    uint32_t timerId;
    uint32_t intervalMs;
    uint32_t lastFireTick;
};

class TimerRegistry {
public:
    void SetTimer(uint32_t hwnd, uint32_t timerId, uint32_t intervalMs, uint32_t nowTick);
    void KillTimer(uint32_t hwnd, uint32_t timerId);
    // Если какой-то таймер пора "выстрелить" — возвращает true, заполняет
    // hwnd/timerId и обновляет его lastFireTick. За один вызов — максимум
    // один сработавший таймер (как и настоящий GetMessage обрабатывает по
    // одному сообщению за раз).
    bool CheckDue(uint32_t nowTick, uint32_t& outHwnd, uint32_t& outTimerId);

private:
    std::vector<RegisteredTimer> timers_;
};

extern TimerRegistry g_timerRegistry;

// Базовый адрес стека игрового потока — для замера, сколько стека реально
// израсходовано на каждом уровне вложенности (диагностика). Выставляется
// один раз, в самом начале потока, в main.cpp.
extern void* g_threadStackBase;

// Настоящий вызов гостевой функции (например WndProc) с соглашением stdcall.
// ВАЖНО: рекурсивно использует TryHandleHostCall внутри цикла — то есть
// вызываемая функция МОЖЕТ сама дёргать любые наши Win32-функции
// (BeginPaint, GetDC, DefWindowProcW и т.д.), это будет работать корректно,
// ровно как на настоящем железе.
uint32_t CallGuestFunctionStdcall(cpu::CpuState& cpu, cpu::IGuestMemory& mem,
                                   uint32_t functionAddr, const std::vector<uint32_t>& args,
                                   uint32_t fsBase, uint32_t gsBase,
                                   int maxSteps = 20000000);

} // namespace win32

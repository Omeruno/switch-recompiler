#pragma once
// clock.h
//
// Часы процесса гостя: GetTickCount, QueryPerformanceCounter, timeGetTime,
// таймеры окон и Sleep идут по одним часам. Время = реальное время с
// запуска + «промотка». В режиме промотки (хостовые тесты) ожидания — Sleep,
// простой цикла сообщений до таймера — не ждут по-настоящему, а сдвигают
// часы вперёд: тесты быстрые и воспроизводимые. На Switch ожидание
// настоящее.

#include <chrono>
#include <cstdint>
#include <thread>

namespace rt {

class Clock {
public:
    // Микросекунды с запуска процесса (с «аптаймом системы» ~17 минут, чтобы
    // GetTickCount не начинался с нуля, как и в настоящей Windows)
    static uint64_t NowUs() {
        using namespace std::chrono;
        uint64_t real = static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
        if (!Start()) Start() = real;
        return real - Start() + Skew() + 1000000000ull;
    }
    static uint32_t NowMs() { return static_cast<uint32_t>(NowUs() / 1000); }

    // Подождать ms миллисекунд (или промотать часы)
    static void Sleep(uint32_t ms) {
        if (FastForward()) { Skew() += static_cast<uint64_t>(ms) * 1000; return; }
        if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        else std::this_thread::yield();
    }

    static void SetFastForward(bool on) { FastForward() = on; }
    static bool IsFastForward() { return FastForward(); }

private:
    static uint64_t& Start() { static uint64_t v = 0; return v; }
    static uint64_t& Skew() { static uint64_t v = 0; return v; }
    static bool& FastForward() { static bool v = false; return v; }
};

} // namespace rt

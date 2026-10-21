#pragma once
// guest_memory.h
//
// Адресное пространство эмулируемой программы (см. paged_memory.h) и
// аварийные выходы для фатальных ошибок эмуляции.
//
// ВАЖНО: реальная сборка под Switch (devkitA64) идёт с флагом -fno-exceptions —
// поэтому здесь принципиально нет ни одного throw. Вместо исключений при
// обращении за пределы буфера — печатаем диагностику и вызываем abort().
// Это не "плохое исправление", а осознанный выбор: в embedded/homebrew-среде
// исключения обычно отключают ради размера бинарника и предсказуемости,
// и это стандартная практика (тот же подход в большинстве игровых движков).

#include <cstdint>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace cpu {

// main.cpp выставляет false сразу после consoleExit(). Без этой проверки
// FatalError/FatalMemoryError попытались бы напечатать диагностику через
// printf ПОСЛЕ закрытия консоли — и именно это оказалось настоящей причиной
// крашей на реальном железе (падало внутри ConsoleSwRenderer_drawChar из
// libnx, найдено через addr2line) — не сама ошибка, а ПОПЫТКА её напечатать
// в уже закрытую консоль маскировала истинное место сбоя.
inline bool g_consoleAvailable = true;

// Текущий EIP гостевого кода — интерпретатор обновляет перед каждым шагом.
// Полезно для ЛЮБОЙ будущей диагностики краха (не только этой сессии) —
// без этого FatalError/FatalMemoryError не говорят, ГДЕ в гостевом коде
// произошёл сбой, только что именно (адрес памяти/сообщение).
inline uint32_t g_lastKnownEip = 0;


// Last nested-interpreter failure. The nested WndProc loop can terminate via
// FatalError without returning to main's regular guest-exception handler.
inline uint32_t g_fatalGuestCode = 0;
inline uint32_t g_fatalGuestAddress = 0;

// Logging must never attempt printing into libnx's already-closed console.
// This is intentionally only called on fatal exit, not during frame rendering.
inline void WriteFatalGuestReport(const char* type, const char* msg, uint32_t addr) {
#ifdef __SWITCH__
    std::FILE* f = std::fopen("sdmc:/switch-recompiler-crash.txt", "w");
    if (!f) return;
    std::fprintf(f, "switch-recompiler fatal guest error\n");
    std::fprintf(f, "Type: %s\n", type ? type : "unknown");
    std::fprintf(f, "Message: %s\n", msg ? msg : "unknown");
    std::fprintf(f, "EIP: 0x%08x\n", g_lastKnownEip);
    std::fprintf(f, "Guest exception code: 0x%08x\n", g_fatalGuestCode);
    std::fprintf(f, "Guest fault address: 0x%08x\n", addr ? addr : g_fatalGuestAddress);
    std::fclose(f);
#else
    (void)type; (void)msg; (void)addr;
#endif
}

[[noreturn]] inline void FatalError(const char* msg) {
    WriteFatalGuestReport("FatalError", msg, 0);
    if (g_consoleAvailable) {
        std::printf("[FATAL] %s (EIP=0x%08x)\n", msg, g_lastKnownEip);
        std::fflush(stdout);
    }
    // ВАЖНО: std::abort() на Switch/Atmosphere вызывает полный крах системы
    // (синий экран с перезагрузкой), а не просто завершение НАШЕГО процесса,
    // как на обычном ПК. std::exit() должен корректно вернуть в hbmenu.
    std::exit(1);
}

[[noreturn]] inline void FatalMemoryError(const char* msg, uint32_t addr) {
    WriteFatalGuestReport("FatalMemoryError", msg, addr);
    if (g_consoleAvailable) {
        std::printf("[FATAL] Guest memory error: %s (addr=0x%08x, EIP=0x%08x)\n", msg, addr, g_lastKnownEip);
        std::fflush(stdout);
    }
    std::exit(1);
}

} // namespace cpu

#include "paged_memory.h"

namespace cpu {

// Исторические имена: раньше IGuestMemory был виртуальным интерфейсом, а
// FlatGuestMemory — одним сплошным буфером. Теперь единственная
// реализация — страничная память (paged_memory.h); имена оставлены, чтобы
// не трогать сотни сигнатур host-функций и тесты. Вызовы не виртуальные,
// компилятор их встраивает.
using IGuestMemory = PagedGuestMemory;
using FlatGuestMemory = PagedGuestMemory;

} // namespace cpu

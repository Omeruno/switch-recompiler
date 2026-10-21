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
inline uint32_t g_fatalPreviousEip = 0;
inline uint32_t g_fatalStackPointer = 0;
inline char g_fatalModule[96] = {};
inline uint32_t g_fatalModuleOffset = 0;

inline char g_fatalInstructionBytes[96] = {};
inline uint32_t g_fatalRegisters[8] = {};
inline uint32_t g_fatalStackWords[8] = {};
inline uint32_t g_fatalStackCount = 0;

// In-memory ring buffer: no SD writes during guest execution.
inline char g_recentDynamicImports[8][176] = {};
inline uint32_t g_recentDynamicImportSerial = 0;
// Raised Win32 SEH exception context; remains zero when none was raised.
inline bool g_sehRaised = false;
inline uint32_t g_sehExceptionCode = 0;
inline uint32_t g_sehExceptionFlags = 0;
inline uint32_t g_sehExceptionArgumentCount = 0;
inline uint32_t g_sehExceptionArguments[4] = {};

inline void RecordDynamicImport(const char* dll, const char* func, uint32_t result) {
    uint32_t slot = g_recentDynamicImportSerial++ % 8;
    std::snprintf(g_recentDynamicImports[slot], sizeof(g_recentDynamicImports[slot]),
                  "%s!%s => 0x%08x", dll ? dll : "(null)",
                  func ? func : "(null)", result);
}



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
    std::fprintf(f, "Previous instruction EIP: 0x%08x\n", g_fatalPreviousEip);
    std::fprintf(f, "ESP: 0x%08x\n", g_fatalStackPointer);
    std::fprintf(f, "Previous module: %s\n", g_fatalModule[0] ? g_fatalModule : "(unknown)");
    std::fprintf(f, "Previous module offset: 0x%08x\n", g_fatalModuleOffset);
    std::fprintf(f, "Previous instruction bytes: %s\n", g_fatalInstructionBytes[0] ? g_fatalInstructionBytes : "(unavailable)");
    std::fprintf(f, "Registers: EAX=%08x ECX=%08x EDX=%08x EBX=%08x ESP=%08x EBP=%08x ESI=%08x EDI=%08x\n",
                 g_fatalRegisters[0], g_fatalRegisters[1], g_fatalRegisters[2], g_fatalRegisters[3],
                 g_fatalRegisters[4], g_fatalRegisters[5], g_fatalRegisters[6], g_fatalRegisters[7]);
    for (uint32_t i = 0; i < g_fatalStackCount; ++i)
        std::fprintf(f, "Stack[ESP+%u]: 0x%08x\n", i * 4, g_fatalStackWords[i]);
    if (g_sehRaised) {
        std::fprintf(f, "Win32 RaiseException: code=0x%08x flags=0x%08x arguments=%u\n",
                     g_sehExceptionCode, g_sehExceptionFlags, g_sehExceptionArgumentCount);
        for (unsigned i = 0; i < 4 && i < g_sehExceptionArgumentCount; ++i)
            std::fprintf(f, "  RaiseException arg[%u]=0x%08x\n", i, g_sehExceptionArguments[i]);
    }
    std::fprintf(f, "Recent GetProcAddress calls (oldest to newest):\n");
    uint32_t count = g_recentDynamicImportSerial < 8 ? g_recentDynamicImportSerial : 8;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t slot = (g_recentDynamicImportSerial - count + i) % 8;
        std::fprintf(f, "  %s\n", g_recentDynamicImports[slot]);
    }
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

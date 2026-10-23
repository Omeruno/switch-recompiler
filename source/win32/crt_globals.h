#pragma once
// crt_globals.h
//
// Область CRT-переменных и ограниченного хранилища строк/argv. Часть импортов из
// msvcrt.dll — это не функции, а ДАННЫЕ (например _adjust_fdiv, _acmdln —
// компилятор обращается к ним как `mov eax, [__imp__acmdln]` и потом читает
// по этому адресу, а не вызывает как функцию). Чтобы это работало, IAT-слот
// такого импорта должен указывать на настоящую, валидную ячейку памяти —
// вот эта ячейка тут и живёт.
//
// Значения внутри — честные дефолты (adjust_fdiv=0 значит "бага Pentium FDIV
// нет", что верно для любого современного/эмулируемого CPU; fmode/commode —
// стандартные значения CRT), а не случайные числа.

#include <cstdint>
#include "../cpu/guest_memory.h"

namespace win32 {

constexpr uint32_t CRT_GLOBALS_SIZE = 0x20000;

constexpr uint32_t CRT_OFFSET_ADJUST_FDIV = 0x00; // int, 4 байта
constexpr uint32_t CRT_OFFSET_FMODE       = 0x04; // int, 4 байта (_O_TEXT по умолчанию)
constexpr uint32_t CRT_OFFSET_COMMODE     = 0x08; // int, 4 байта
constexpr uint32_t CRT_OFFSET_CMDLINE_STR = 0x0C; // reserved legacy inline char[64]; use CMDLINE_PTR
constexpr uint32_t CRT_OFFSET_CMDLINE_PTR = 0x4C; // char* to full command line (_acmdln)
constexpr uint32_t CRT_OFFSET_ARGV        = 0x50; // char*[2] = { &CMDLINE_STR, 0 }
constexpr uint32_t CRT_OFFSET_ENV         = 0x58; // char*[1] = { 0 } — пустое окружение

// Широкие (UTF-16) варианты — нужны __wgetmainargs/GetCommandLineW, которые
// использует стартовый код MFC-приложений (paint.exe и подобные; обычный
// __getmainargs выше хватало сапёру, у него узкий main-style запуск).
constexpr uint32_t CRT_OFFSET_WCMDLINE_STR = 0x5C; // reserved legacy inline UTF-16[64]; use WCMDLINE_PTR
constexpr uint32_t CRT_OFFSET_WARGV        = 0xDC; // wchar_t*[2] = { &WCMDLINE_STR, 0 }
constexpr uint32_t CRT_OFFSET_WENV         = 0xE4; // wchar_t*[1] = { 0 } — пустое окружение
constexpr uint32_t CRT_OFFSET_WCMDLINE_PTR = 0xE8; // wchar_t* to full UTF-16 command line (_wcmdln)

// Адрес начала этой области — выставляется один раз перед стартом исполнения,
// чтобы host-функции (__getmainargs и т.д.) знали, куда писать/откуда читать.
extern uint32_t g_crtGlobalsBase;

constexpr uint32_t CRT_OFFSET_ARGC=0xEC,CRT_OFFSET_ARGV_PTR=0xF0,CRT_OFFSET_WARGV_PTR=0xF4;

// Full bounded strings/argv live after the legacy fixed slots.
bool InitCrtGlobals(cpu::IGuestMemory& mem, uint32_t base, const char* commandLine);

} // namespace win32

#pragma once
// msvcrt.h — общее состояние эмулируемой msvcrt.dll (между msvcrt_*.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include "hle_util.h"

namespace win32 {
namespace crt {

// Блок переменных CRT в памяти гостя (errno, _iob, __mb_cur_max, ...).
struct Globals {
    uint32_t errnoAddr = 0;
    uint32_t doserrnoAddr = 0;
    uint32_t iob = 0;           // FILE _iob[20], по 32 байта
    uint32_t mbCurMax = 0;      // int __mb_cur_max
    uint32_t fmode = 0, commode = 0;
    uint32_t argc = 0, argv = 0, wargv = 0; // int __argc; char** __argv; wchar_t** __wargv
    uint32_t environ = 0, wenviron = 0;     // char** _environ (и __initenv)
    uint32_t acmdln = 0, wcmdln = 0;        // char* _acmdln; wchar_t* _wcmdln
    uint32_t pctype = 0, pwctype = 0;       // указатели на таблицы ctype
    uint32_t ctypeTable = 0;                // short[257] (с -1)
    uint32_t pgmptr = 0, wpgmptr = 0;
    uint32_t osver = 0, winver = 0, winmajor = 0, winminor = 0;
    uint32_t timezone = 0, daylight = 0, tzname = 0;
    uint32_t heapHandle = 0;
};
Globals& G(cpu::IGuestMemory& m);

constexpr uint32_t kFileSize = 32;
constexpr uint32_t kIobCount = 20;

// errno
void SetErrno(cpu::IGuestMemory& m, int e);
constexpr int kEINVAL = 22, kENOENT = 2, kEBADF = 9, kENOMEM = 12, kEACCES = 13, kEEXIST = 17, kERANGE = 34, kEDOM = 33;

// Куча CRT (на куче процесса)
uint32_t Malloc(cpu::IGuestMemory& m, uint32_t size, bool zero = false);
void Free(cpu::IGuestMemory& m, uint32_t p);

// Строки в памяти гостя -> строки хоста
uint32_t NewString(cpu::IGuestMemory& m, const std::string& s);
uint32_t NewWString(cpu::IGuestMemory& m, const std::u16string& s);

// Вывод stdio (msvcrt_stdio.cpp): запись в FILE* гостя, flush всех потоков.
int StreamWrite(cpu::IGuestMemory& m, uint32_t file, const char* data, size_t len);
void FlushAll(cpu::IGuestMemory& m);

} // namespace crt
} // namespace win32

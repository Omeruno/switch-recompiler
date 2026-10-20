#pragma once
// msvcrt_format.h — printf/scanf-форматирование в стиле msvcrt.dll над
// памятью гостя (аргументы читаются со стека или из va_list гостя).

#include <cstdint>
#include <cstring>
#include <string>

#include "../cpu/guest_memory.h"

namespace win32 {
namespace crt {

// Последовательное чтение аргументов из va_list гостя (указатель на
// 4-байтовые слоты стека).
struct GuestVaList {
    cpu::IGuestMemory& m;
    uint32_t p;
    uint32_t Next32() { uint32_t v = m.Read32(p); p += 4; return v; }
    uint64_t Next64() { uint64_t lo = Next32(); return lo | (static_cast<uint64_t>(Next32()) << 32); }
    double NextDouble() { uint64_t b = Next64(); double d; std::memcpy(&d, &b, 8); return d; }
};

// Форматирует строку формата fmt (ANSI или UTF-16 в памяти гостя).
// Результат — в «байтах» для узкого варианта (ANSI) или в UTF-16 для
// широкого. Возвращаемая строка — для узкого; для широкого см. ниже.
std::string FormatA(cpu::IGuestMemory& m, uint32_t fmt, GuestVaList& args);
std::u16string FormatW(cpu::IGuestMemory& m, uint32_t fmt, GuestVaList& args);

// scanf: src — уже прочитанный вход; возвращает число присвоенных полей
// (или -1 при EOF до первого поля); consumed — сколько символов входа
// использовано.
int ScanA(cpu::IGuestMemory& m, const std::string& src, uint32_t fmt, GuestVaList& args, size_t* consumed = nullptr);
int ScanW(cpu::IGuestMemory& m, const std::u16string& src, uint32_t fmt, GuestVaList& args, size_t* consumed = nullptr);

} // namespace crt
} // namespace win32

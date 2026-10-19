#pragma once
// iat_patcher.h
//
// Связующее звено между pe_parser (знает СПИСОК импортов и их адреса
// в таблице) и host_functions (знает, КАК реализовать конкретные функции).
// Отдельный файл специально, чтобы host_functions.h не тянул за собой
// зависимость от PE-парсера — это чисто Win32-семантика сама по себе.

#include <cstdint>
#include "host_functions.h"
#include "../loader/pe_parser.h"

namespace win32 {

struct PatchResult {
    int totalImports = 0;
    int patched = 0;         // сколько реально нашли реализацию
    int unimplemented = 0;   // сколько оставили как "не реализовано" (адрес 0)
};

// Проходит по всем импортам parser'а. Для функций — прописывает host-адрес
// в IAT (см. host_functions.h). Для ИЗВЕСТНЫХ импортов-ДАННЫХ (не функций,
// напр. _adjust_fdiv, _acmdln) — прописывает адрес реальной ячейки внутри
// CRT-globals области (crtGlobalsBase), а не host-функции — вызывать их
// нельзя, это переменные, а не код.
// Адрес для одного импорта из эмулируемой (HLE) DLL: host-функция,
// ячейка данных CRT или диагностическая заглушка. implemented — есть ли
// настоящая реализация.
uint32_t ResolveHostImport(const std::string& dllName, const pe::ImportedFunction& func,
                           uint32_t crtGlobalsBase, bool& implemented);

PatchResult PatchImportAddressTable(const pe::PEParser& parser,
                                     uint8_t* imageBuffer,
                                     size_t imageBufferSize,
                                     uint32_t crtGlobalsBase);

} // namespace win32

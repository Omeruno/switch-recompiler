#pragma once
// resource_parser.h
//
// Разбор дерева ресурсов PE (.rsrc) — структура стандартная для всех PE:
// TYPE -> NAME/ID -> LANGUAGE -> данные (3 уровня вложенных директорий,
// на последнем уровне — указатель на реальные байты ресурса).
//
// Пока реализованы только строковые таблицы (RT_STRING), нужные для
// LoadStringW — но структура написана так, чтобы её было легко расширить
// под RT_DIALOG/RT_MENU в будущей фазе (диалоги, меню игры).

#include <cstdint>
#include <cstddef>
#include <vector>

namespace pe {

constexpr uint16_t RT_ICON      = 3;
constexpr uint16_t RT_MENU      = 4;
constexpr uint16_t RT_DIALOG    = 5;
constexpr uint16_t RT_STRING    = 6;
constexpr uint16_t RT_ACCELERATOR = 9;
constexpr uint16_t RT_GROUP_ICON  = 14;

// Ищет строку с данным ID среди ресурсов RT_STRING.
// `image` — буфер, уже разложенный по RVA (как после PEParser::MapImageToMemory,
// т.е. image[rva] == байт по этому RVA), `resourceDirRva` —
// PEParser::GetResourceDirectoryRva().
// Возвращает символы строки в UTF-16 (БЕЗ null-терминатора) — пустой vector,
// если ресурс не найден, дерево отсутствует, или что-то не сошлось по формату.
std::vector<uint16_t> FindStringResource(const uint8_t* image, size_t imageSize,
                                          uint32_t resourceDirRva, uint16_t stringId);

// Обобщённый поиск ЛЮБОГО типа ресурса по числовому ID (без учёта языка —
// берём первую попавшуюся запись, этого достаточно для игр с одним языком).
// Возвращает true и заполняет outDataRva/outSize, если нашли; false — если
// ресурс такого типа/ID нет, либо у образа вообще нет .rsrc.
// Именованные (не числовые) ID пока не поддержаны — это отдельное расширение
// на будущее, если реально понадобится.
bool FindResourceGeneric(const uint8_t* image, size_t imageSize, uint32_t resourceDirRva,
                          uint16_t typeId, uint16_t nameId,
                          uint32_t& outDataRva, uint32_t& outSize);

// Как FindResourceGeneric, но тип ресурса задан СТРОКОВЫМ ИМЕНЕМ (не числовым
// ID) — так хранятся, например, звуки в winmine.exe: тип называется буквально
// "WAVE", а не RT_RCDATA/числовой ID. Имя сравнивается без учёта регистра.
bool FindResourceByTypeName(const uint8_t* image, size_t imageSize, uint32_t resourceDirRva,
                             const char* typeName, uint16_t nameId,
                             uint32_t& outDataRva, uint32_t& outSize);

} // namespace pe
#pragma once
// resources.h
//
// Ресурсы модулей (.rsrc): поиск по типу и имени, каждое — число (ID) или
// строка, как в FindResource. Язык: сначала заданный, затем нейтральный,
// затем любой. Работает по разложенному образу модуля (rt::Module::image).

#include <cstdint>
#include <string>

#include "process.h"

namespace rt {

// Тип/имя ресурса: число (MAKEINTRESOURCE) или строка
struct ResId {
    uint16_t id = 0;
    std::u16string name;   // если не пусто — строковое имя
    static ResId Num(uint16_t v) { ResId r; r.id = v; return r; }
    static ResId Str(const std::u16string& s) { ResId r; r.name = s; return r; }
};

struct ResourceData {
    uint32_t rva = 0;      // RVA данных в модуле (адрес = module->base + rva)
    uint32_t size = 0;
    uint32_t entryRva = 0; // RVA записи IMAGE_RESOURCE_DATA_ENTRY (это HRSRC)
};

bool FindResource(const Module* m, const ResId& type, const ResId& name, ResourceData& out, uint16_t lang = 0);

// Указатель на данные ресурса в разложенном образе (nullptr, если вне образа)
const uint8_t* ResourcePtr(const Module* m, const ResourceData& d);

// Checked string span in the module snapshot; zero-length entries are valid.
bool FindStringResourceSpan(const Module*,uint32_t id,ResourceData&,uint32_t& offset,uint16_t& chars);

// Строка из таблицы строк (RT_STRING): false, если нет
bool LoadStringResource(const Module* m, uint32_t id, std::u16string& out);

// Стандартные типы
constexpr uint16_t RT_CURSOR = 1, RT_BITMAP = 2, RT_ICON = 3, RT_MENU = 4, RT_DIALOG = 5, RT_STRING = 6,
                   RT_ACCELERATOR = 9, RT_RCDATA = 10, RT_GROUP_CURSOR = 12, RT_GROUP_ICON = 14;

} // namespace rt

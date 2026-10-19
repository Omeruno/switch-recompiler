#pragma once
// menu_parser.h
//
// Разбор ресурса MENU (найденного через LoadMenuW) в удобную структуру.
// winmine.exe сама никогда не рисует своё меню — это в реальной Windows
// делает USER32 (системная отрисовка полосы меню и выпадающих списков).
// У нас такого системного слоя нет, поэтому рисуем и обрабатываем клики
// сами (см. main.cpp) — а этот файл только разбирает сырые байты ресурса
// в понятную структуру: пункты, ID, текст, есть ли подменю/разделитель.

#include <cstdint>
#include <vector>

namespace win32 {

struct MenuItem {
    uint16_t id = 0;          // ID для WM_COMMAND при выборе (0 у разделителей и подменю)
    bool isSeparator = false;
    bool isPopup = false;     // сам открывает подменю (у нас пока только 1 уровень вложенности реально нужен)
    std::vector<uint16_t> text;         // UTF-16 без '&' и без части после '\t'
    std::vector<uint16_t> shortcutText; // часть после '\t' (например "F2"), пусто если нет
    std::vector<MenuItem> subItems;     // непусто только если isPopup
};

struct ParsedMenu {
    std::vector<MenuItem> topLevel; // каждый элемент — один пункт полосы меню ("Игра", "Справка")
};

// Разбирает сырые байты ресурса MENU (то, что вернул FindResourceGeneric для
// RT_MENU) в ParsedMenu. Возвращает false, если формат не сошёлся — тогда
// ParsedMenu остаётся пустым, вызывающий код просто не рисует меню вообще
// (не крашимся на непонятном ресурсе).
bool ParseMenuResource(const uint8_t* data, uint32_t size, ParsedMenu& out);

} // namespace win32
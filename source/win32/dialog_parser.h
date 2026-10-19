#pragma once
// dialog_parser.h
//
// Разбор ресурса DIALOG (классический формат DLGTEMPLATE, не DIALOGEX) в
// удобную структуру. Как и с MENU — winmine.exe не рисует диалоги сама,
// это в реальной Windows делает системный USER32. У нас такого слоя нет,
// поэтому рисуем и обрабатываем сами (см. main.cpp).

#include <cstdint>
#include <string>
#include <vector>

namespace win32 {

struct DialogControl {
    uint16_t id = 0;
    int16_t x = 0, y = 0, cx = 0, cy = 0; // в диалоговых единицах (не пикселях!)
    uint32_t style = 0;
    std::string windowClass; // "BUTTON", "EDIT", "STATIC" и т.п. (в нижнем регистре)
    std::vector<uint16_t> text; // UTF-16 текст контрола (для STATIC/BUTTON — видимый текст)
};

struct ParsedDialog {
    int16_t x = 0, y = 0, cx = 0, cy = 0; // в диалоговых единицах
    std::vector<uint16_t> title;
    std::vector<DialogControl> controls;
};

// Разбирает сырые байты ресурса DIALOG (то, что вернул FindResourceGeneric
// для RT_DIALOG=5) в ParsedDialog. false — формат не classic DIALOG (может
// быть DIALOGEX, с сигнатурой 0xFFFF0001 в начале — мы её не поддерживаем).
bool ParseDialogResource(const uint8_t* data, uint32_t size, ParsedDialog& out);

} // namespace win32

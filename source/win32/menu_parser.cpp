#include "menu_parser.h"
#include <cstring>

namespace win32 {

namespace {

// Читает один пункт меню начиная с позиции pos, продвигает pos вперёд.
// Возвращает false при ошибке формата (вышли за границы буфера).
bool ParseOneItem(const uint8_t* data, uint32_t size, uint32_t& pos, MenuItem& out) {
    if (pos + 2 > size) return false;
    uint16_t flags;
    std::memcpy(&flags, data + pos, 2);
    pos += 2;

    constexpr uint16_t MF_POPUP_ = 0x0010;
    constexpr uint16_t MF_SEPARATOR_ = 0x0800; // не всегда выставляется честно — пустая строка тоже разделитель
    out.isPopup = (flags & MF_POPUP_) != 0;

    if (!out.isPopup) {
        if (pos + 2 > size) return false;
        std::memcpy(&out.id, data + pos, 2);
        pos += 2;
    }

    // Строка — до null-терминатора
    std::vector<uint16_t> fullText;
    while (pos + 2 <= size) {
        uint16_t ch;
        std::memcpy(&ch, data + pos, 2);
        pos += 2;
        if (ch == 0) break;
        fullText.push_back(ch);
    }

    // Разделяем на "видимый текст" и "текст горячей клавиши" по табу
    bool afterTab = false;
    for (uint16_t ch : fullText) {
        if (ch == '\t') { afterTab = true; continue; }
        if (afterTab) out.shortcutText.push_back(ch);
        else out.text.push_back(ch);
    }

    out.isSeparator = fullText.empty() || (flags & MF_SEPARATOR_);

    if (out.isPopup) {
        // Подпункты идут сразу следом, пока один из них не будет с MF_END(0x80)
        constexpr uint16_t MF_END_ = 0x0080;
        for (;;) {
            MenuItem child;
            uint32_t beforePos = pos;
            if (!ParseOneItem(data, size, pos, child)) return false;

            uint16_t childFlags;
            std::memcpy(&childFlags, data + beforePos, 2);
            bool isEnd = (childFlags & MF_END_) != 0;

            out.subItems.push_back(std::move(child));
            if (isEnd) break;
            if (pos >= size) break; // защита от зацикливания на битых данных
        }
    }

    return true;
}

} // namespace

bool ParseMenuResource(const uint8_t* data, uint32_t size, ParsedMenu& out) {
    out.topLevel.clear();
    if (size < 4) return false;

    uint16_t version, headerSize;
    std::memcpy(&version, data, 2);
    std::memcpy(&headerSize, data + 2, 2);
    if (version != 0) return false; // поддерживаем только классический (не MENUEX) формат

    uint32_t pos = 4 + headerSize;
    constexpr uint16_t MF_END_ = 0x0080;

    for (;;) {
        if (pos >= size) break;
        MenuItem item;
        uint32_t beforePos = pos;
        if (!ParseOneItem(data, size, pos, item)) return false;

        uint16_t flags;
        std::memcpy(&flags, data + beforePos, 2);
        bool isEnd = (flags & MF_END_) != 0;

        out.topLevel.push_back(std::move(item));
        if (isEnd) break;
    }

    return !out.topLevel.empty();
}

} // namespace win32
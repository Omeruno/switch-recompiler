#include "dialog_parser.h"
#include <cstring>

namespace win32 {

namespace {

// Читает sz_Or_Ord: 0x0000=пусто, 0xFFFF+WORD=ординал (номер встроенного
// класса), иначе — null-terminated UTF-16 строка. pos продвигается вперёд.
// Если это ординал — записывает его в outOrdinal и возвращает пустую строку.
bool ReadSzOrOrd(const uint8_t* data, uint32_t size, uint32_t& pos,
                  std::vector<uint16_t>& outText, uint16_t& outOrdinal) {
    outOrdinal = 0;
    outText.clear();
    if (pos + 2 > size) return false;
    uint16_t first;
    std::memcpy(&first, data + pos, 2);
    pos += 2;

    if (first == 0x0000) return true; // пусто
    if (first == 0xFFFF) {
        if (pos + 2 > size) return false;
        std::memcpy(&outOrdinal, data + pos, 2);
        pos += 2;
        return true;
    }
    // Обычная строка — first уже первый символ
    outText.push_back(first);
    while (pos + 2 <= size) {
        uint16_t ch;
        std::memcpy(&ch, data + pos, 2);
        pos += 2;
        if (ch == 0) break;
        outText.push_back(ch);
    }
    return true;
}

const char* OrdinalToClassName(uint16_t ord) {
    switch (ord) {
        case 0x0080: return "button";
        case 0x0081: return "edit";
        case 0x0082: return "static";
        case 0x0083: return "listbox";
        case 0x0084: return "scrollbar";
        case 0x0085: return "combobox";
        default: return "";
    }
}

void AlignToDword(uint32_t& pos) {
    pos = (pos + 3) & ~3u;
}

} // namespace

bool ParseDialogResource(const uint8_t* data, uint32_t size, ParsedDialog& out) {
    out = ParsedDialog{};
    if (size < 18) return false;

    uint32_t style, exStyle;
    std::memcpy(&style, data + 0, 4);
    std::memcpy(&exStyle, data + 4, 4);
    (void)exStyle;

    // DIALOGEX имеет сигнатуру 0xFFFF 0x0001 в первых 4 байтах (в виде
    // WORD wDlgVer=1, WORD wSignature=0xFFFF, то есть при чтении как DWORD
    // это выглядит как 0x0001FFFF в little-endian... проверяем по WORD'ам,
    // чтобы не перепутать) — мы такой формат не поддерживаем.
    uint16_t maybeSig;
    std::memcpy(&maybeSig, data + 2, 2);
    if (maybeSig == 0xFFFF) return false; // это DIALOGEX, не поддерживаем

    uint16_t cdit;
    std::memcpy(&cdit, data + 8, 2);
    int16_t x, y, cx, cy;
    std::memcpy(&x, data + 10, 2);
    std::memcpy(&y, data + 12, 2);
    std::memcpy(&cx, data + 14, 2);
    std::memcpy(&cy, data + 16, 2);
    out.x = x; out.y = y; out.cx = cx; out.cy = cy;

    uint32_t pos = 18;
    std::vector<uint16_t> dummyText;
    uint16_t dummyOrd;
    if (!ReadSzOrOrd(data, size, pos, dummyText, dummyOrd)) return false; // menu
    if (!ReadSzOrOrd(data, size, pos, dummyText, dummyOrd)) return false; // windowClass
    if (!ReadSzOrOrd(data, size, pos, out.title, dummyOrd)) return false; // title

    constexpr uint32_t DS_SETFONT = 0x40;
    if (style & DS_SETFONT) {
        if (pos + 2 > size) return false;
        pos += 2; // pointsize — не используем, у нас свой фиксированный шрифт
        std::vector<uint16_t> typeface;
        if (!ReadSzOrOrd(data, size, pos, typeface, dummyOrd)) return false;
    }

    for (uint16_t i = 0; i < cdit; i++) {
        AlignToDword(pos);
        if (pos + 24 > size) return false; // минимум на style+exStyle+x+y+cx+cy+id

        DialogControl ctrl;
        std::memcpy(&ctrl.style, data + pos, 4); pos += 4;
        pos += 4; // exStyle — пропускаем, не используем
        int16_t cx_, cy_, cw_, ch_;
        std::memcpy(&cx_, data + pos, 2); pos += 2;
        std::memcpy(&cy_, data + pos, 2); pos += 2;
        std::memcpy(&cw_, data + pos, 2); pos += 2;
        std::memcpy(&ch_, data + pos, 2); pos += 2;
        ctrl.x = cx_; ctrl.y = cy_; ctrl.cx = cw_; ctrl.cy = ch_;
        std::memcpy(&ctrl.id, data + pos, 2); pos += 2;

        uint16_t classOrd;
        std::vector<uint16_t> classText;
        if (!ReadSzOrOrd(data, size, pos, classText, classOrd)) return false;
        if (classOrd != 0) {
            ctrl.windowClass = OrdinalToClassName(classOrd);
        } else {
            for (uint16_t c : classText) ctrl.windowClass += static_cast<char>(c < 128 ? c : '?');
        }

        uint16_t textOrd;
        if (!ReadSzOrOrd(data, size, pos, ctrl.text, textOrd)) return false;
        // textOrd (если бы был) — ссылка на числовой ресурс, у наших контролов не встречается

        if (pos + 2 > size) return false;
        uint16_t extraCount;
        std::memcpy(&extraCount, data + pos, 2);
        pos += 2 + extraCount;

        out.controls.push_back(std::move(ctrl));
    }

    return true;
}

} // namespace win32

#pragma once
// font.h
//
// Шрифты для GDI: TrueType-файлы (по умолчанию Liberation Sans/Serif/Mono,
// метрически совместимые с Arial/Times New Roman/Courier New) растеризуются
// stb_truetype. Имя шрифта Windows сопоставляется с одним из файлов, высота
// понимается как в LOGFONT (отрицательная — высота символа, положительная —
// высота ячейки). Текст рисуется без сглаживания, как в Windows XP со
// стандартными настройками. Если файлов шрифтов нет, используется
// встроенный растровый шрифт 5x7.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "region.h"
#include "surface.h"

namespace ui {

struct FontDesc {
    int height = 0;        // lfHeight
    int width = 0;         // lfWidth (0 — по умолчанию)
    int weight = 400;      // FW_NORMAL
    bool italic = false;
    bool underline = false;
    bool strikeout = false;
    uint8_t charset = 0;
    uint8_t pitchAndFamily = 0;
    std::string face;      // lfFaceName (UTF-8)

    bool operator<(const FontDesc& o) const;
};

struct TextMetrics {
    int height = 0, ascent = 0, descent = 0;
    int internalLeading = 0, externalLeading = 0;
    int aveCharWidth = 0, maxCharWidth = 0;
    int weight = 400;
    bool italic = false, underline = false, strikeout = false;
    bool fixedPitch = false;
    uint8_t charset = 0;
    uint8_t pitchAndFamily = 0;
    std::string faceName;
};

class Font {
public:
    const TextMetrics& Metrics() const { return metrics_; }
    const FontDesc& Desc() const { return desc_; }

    // Ширина символа (с учётом межсимвольного шага) в пикселях
    int Advance(uint32_t ch);
    int MeasureWidth(const std::u16string& text);

    // Рисует строку: (x, y) — левый верхний угол ячейки. Пиксели вне clip
    // не трогаются. extraSpacing — SetTextCharacterExtra. dx — массив
    // шагов ExtTextOut (может быть пуст). Возвращает итоговую ширину.
    int Draw(Surface& s, const Region& clip, int x, int y, const std::u16string& text, uint32_t rgb,
             int extraSpacing = 0, const std::vector<int>* dx = nullptr);

private:
    friend class FontCache;
    struct Glyph {
        int width = 0, height = 0, xoff = 0, yoff = 0, advance = 0;
        std::vector<uint8_t> mask; // 0/1 на пиксель
    };
    const Glyph& GetGlyph(uint32_t ch);

    FontDesc desc_;
    TextMetrics metrics_;
    struct FaceData* face_ = nullptr; // nullptr — встроенный растровый шрифт
    float scale_ = 1.0f;
    int bitmapScale_ = 1;             // для встроенного шрифта
    bool syntheticBold_ = false;
    std::map<uint32_t, Glyph> glyphs_;
};

class FontCache {
public:
    // Каталог с .ttf (Liberation*.ttf). Вызывать до первого текста.
    static void SetDirectory(const std::string& dir);
    static Font* Get(const FontDesc& desc);
};

} // namespace ui

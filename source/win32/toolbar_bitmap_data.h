// Автосгенерировано из настоящего RT_BITMAP ресурса ID=859 paint.exe
// (RLE4-декомпрессия сделана offline, здесь просто честные готовые пиксели).
// Полный битмап 448x16.
//
// ВАЖНО (найдено методичной ручной проверкой по тонкой пиксельной
// линейке — эта сессия): иконки инструментов НЕ имеют равномерную
// ширину — ни 20px, ни 28px, ни 14px не подходят как единая формула.
// Каждая иконка имеет СВОЮ ширину (от 10 до 26px). Настоящие 16
// инструментов занимают область x=0..256 — сразу после RoundRect (на
// x=256) резко меняется палитра (светло-серый→тёмно-серый) и стиль
// (пропадает чёрный контур) — это чётко подтверждает конец области
// инструментов и начало чего-то другого (похоже на выбор толщины линии
// — отдельная, contextная UI-область, не относящаяся к 16 инструментам).
//
// Таблица (srcX, srcWidth) для каждого инструмента, в порядке:
// FreeSel,RectSel,Eraser,Fill,Dropper,Zoom,Pencil,Brush,Airbrush,Text,Line,Curve,Rect,Polygon,Ellipse,RoundRect
#pragma once
#include <cstdint>
namespace win32 {
constexpr int TOOLBAR_BITMAP_W = 448;
constexpr int TOOLBAR_BITMAP_H = 16;
constexpr int TOOL_IMAGE_H = 16;
constexpr int TOOL_COUNT = 16;

struct ToolIconRect { int srcX; int srcW; };
constexpr ToolIconRect g_toolIconRects[16] = {
    {0,  16}, // FreeSel
    {16, 16}, // RectSel
    {32, 20}, // Eraser
    {52, 13}, // Fill
    {65, 25}, // Dropper
    {82, 26}, // Zoom
    {98, 12}, // Pencil
    {110,14}, // Brush
    {128,17}, // Airbrush
    {147,16}, // Text
    {163,15}, // Line
    {178,10}, // Curve
    {192,16}, // Rect
    {208,16}, // Polygon
    {224,14}, // Ellipse
    {238,18}, // RoundRect
};

// ARGB8888, маска (0xFF0000 фон) уже заменена на альфа=0 (прозрачно)
extern const uint32_t g_toolbarBitmap[448*16];
}
